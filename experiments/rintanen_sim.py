#!/usr/bin/env python3
"""
Offline simulator of rintanen(...) (search_engines/rintanen_search.cc).

The threads are scheduled cooperatively -- one runs at a time -- so a run is
determined by, per length k, the time to generate its formula G(k) and the
solver time to decide it S(k), measured by algorithm A in profiling mode:

  sat(..., continue_after_first_plan=true, step_time_limit=C,
      length_strategy=length_list(lengths=[...]))

whose STEP lines carry status (SAT / UNSAT / TIMEOUT), encode_time, solve_time.

Scheduling reproduced from the code (schedule_formula_as_one=true):
  * the first call is created at t=0 and runs;
  * a turn lasts turn_length = interval * gamma^position (position of the call
    among the open ones, 0 = shortest length); a call's first turn starts with
    uninterrupted formula generation, and if that takes longer than the turn,
    the turn ends right after it;
  * after a turn, the next larger open call runs; if there is none, a new
    length is created (while fewer than max_parallel_calls are open and the
    strategy has one) and runs, otherwise the smallest open call runs;
  * UNSAT at step s closes every open call with step <= s;
  * SAT ends the run.
  * memory (optional, memory_limit_mb given): a call allocates
    bytes_per_clause * clauses(k) while its formula is generated, one time step
    at a time; after each step, if base + allocated + leeway > limit and other
    calls are open, generation pauses and the smallest open call runs (the
    scheduler's forcePause path, which never opens a new length); memory is
    freed when a call is closed. Measured on depot/pfile7: ~140-155 B/clause,
    formula plus solver, nothing freed while calls are open.
Not modelled: kissat's callback granularity and cache effects of
interleaving. Timings must come from runs under the same machine load: on the
4-core/8-thread dev machine, 6 parallel jobs cost 1.6-2x the CPU time of 4.

A result is "uncertain" if it depends on something the profile does not know:
a call that timed out in the profile (S > C) receiving more than C of solve
time, or a length that was not profiled receiving any solve time.
"""

import json
import math
import sys


def lround(x):
    return int(math.floor(x + 0.5)) if x >= 0 else -int(math.floor(-x + 0.5))


class ByIteration:
    def __init__(self, start_length=5, multiplier=1.41, maximum_iteration=-1, strictly_increasing=False):
        self.start, self.mult, self.maxit, self.strict = start_length, multiplier, maximum_iteration, strictly_increasing

    def first(self):
        return self.start

    def next(self, step, prev):
        if step == self.maxit:
            return None
        v = lround(0.5 + self.start * self.mult ** step)
        if self.strict and v <= prev:
            return prev + 1
        return v


class ByIncrement:
    def __init__(self, start_length=1, increment=1, maximum_iteration=-1):
        self.start, self.inc, self.maxit = start_length, increment, maximum_iteration

    def first(self):
        return self.start

    def next(self, step, prev):
        if step == self.maxit:
            return None
        return self.start + step * self.inc


def lengths_of(strategy, n):
    """first n lengths the strategy produces (as create_next_length_run asks for them)"""
    out = [strategy.first()]
    while len(out) < n:
        v = strategy.next(len(out), out[-1])
        if v is None:
            break
        out.append(v)
    return out


def simulate(profile, strategy, interval=1.0, gamma=1.0, max_parallel=20, time_limit=1800.0,
             memory_limit_mb=None, bytes_per_clause=150.0, base_mb=120.0, leeway_mb=1000.0,
             received=None):
    """
    profile: {length: {"status": "SAT"|"UNSAT"|"TIMEOUT", "encode": G, "solve": S}}
    Returns dict(solved, time, length, uncertain, created, reason).
    """
    INF = float("inf")
    t = 0.0
    open_calls = {}           # step -> call dict
    next_step = 0
    prev_len = -1
    uncertain = []
    created = []

    def create():
        nonlocal next_step, prev_len
        if next_step == 0:
            L = strategy.first()
        else:
            L = strategy.next(next_step, prev_len)
            if L is None:
                return None
        p = profile.get(L)
        if p is None:
            failed = [k for k, v in profile.items() if v.get("status") == "FAILED" and k < L]
            if failed:
                p = dict(profile[min(failed)])
        if p is None:
            call = dict(step=next_step, L=L, G=None, S=None, status=None, done_gen=False, used=0.0,
                        gen_steps=0, mem=0.0, mem_total=0.0)
        else:
            # FAILED: the formula could not be handled within the memory limit
            # (profiling died on it); like rintanen, the call never finishes
            S = p["solve"] if p["status"] in ("SAT", "UNSAT") else INF
            call = dict(step=next_step, L=L, G=p["encode"], S=S, cap=p["solve"], status=p["status"],
                        done_gen=False, used=0.0, gen_steps=0, mem=0.0,
                        mem_total=bytes_per_clause * p.get("clauses", 0) / 2**20)
        open_calls[next_step] = call
        created.append(L)
        next_step += 1
        prev_len = L
        return call

    def turn_length(step):
        if gamma == 1.0:
            return interval
        pos = sorted(open_calls).index(step)
        return max(0.001, lround(interval * 1000 * gamma ** pos) / 1000.0)

    def memory_in_use():
        return base_mb + sum(c["mem"] for c in open_calls.values())

    def pick_after(step, force_pause=False):
        if force_pause:
            # generation paused for memory: never opens a new length, goes to the smallest
            return open_calls[min(open_calls)]
        larger = [s for s in open_calls if s > step]
        if larger:
            return open_calls[min(larger)]
        if len(open_calls) < max_parallel:
            c = create()
            if c is not None:
                return c
        if not open_calls:
            return None
        return open_calls[min(open_calls)]

    cur = create()
    while True:
        if t > time_limit:
            return dict(solved=False, time=time_limit, length=None, uncertain=uncertain, created=created, reason="time limit")
        budget = turn_length(cur["step"])
        quantum = budget
        if not cur["done_gen"]:
            if cur["G"] is None:
                # length never profiled: its generation time and outcome are unknown
                uncertain.append(("unprofiled", cur["L"]))
                return dict(solved=None, time=t, length=None, uncertain=uncertain, created=created,
                            reason=f"length {cur['L']} not profiled")
            if memory_limit_mb is None:
                t += cur["G"]
                cur["done_gen"] = True
                quantum = max(0.0, budget - cur["G"])
            else:
                spent = 0.0
                paused = False
                # a paused call re-checks memory before encoding its next step
                while cur["gen_steps"] < cur["L"]:
                    if (cur["gen_steps"] > 0 and memory_in_use() + leeway_mb > memory_limit_mb
                            and len(open_calls) > 1):
                        paused = True
                        break
                    dt = cur["G"] / cur["L"]
                    t += dt; spent += dt
                    cur["gen_steps"] += 1
                    cur["mem"] = cur["mem_total"] * cur["gen_steps"] / cur["L"]
                    if (memory_in_use() + leeway_mb > memory_limit_mb and len(open_calls) > 1
                            and cur["gen_steps"] < cur["L"]):
                        paused = True
                        break
                if paused:
                    if t > time_limit:
                        return dict(solved=False, time=time_limit, length=None, uncertain=uncertain,
                                    created=created, reason="time limit")
                    if spent == 0.0:
                        # nothing can progress until memory is freed; the paused
                        # call is skipped, which costs nothing in the real scheduler
                        t += 1e-6
                    cur = pick_after(cur["step"], force_pause=True)
                    continue
                cur["done_gen"] = True
                quantum = max(0.0, budget - spent)
        remaining = cur["S"] - cur["used"]
        if received is not None:
            received[cur["L"]] = received.get(cur["L"], 0.0) + min(quantum, remaining, max(0.0, time_limit - t))
        if remaining <= quantum:
            t += remaining
            cur["used"] = cur["S"]
            if t > time_limit:
                return dict(solved=False, time=time_limit, length=None, uncertain=uncertain, created=created, reason="time limit")
            if cur["status"] == "SAT":
                return dict(solved=True, time=t, length=cur["L"], uncertain=uncertain, created=created, reason="sat")
            # UNSAT: close every call with step <= this one
            s = cur["step"]
            for k in [k for k in open_calls if k <= s]:
                del open_calls[k]
            nxt = pick_after(s)
        else:
            t += quantum
            cur["used"] += quantum
            if cur["status"] == "TIMEOUT" and cur["used"] > cur["cap"]:
                if ("timeout", cur["L"]) not in uncertain:
                    uncertain.append(("timeout", cur["L"]))
            nxt = pick_after(cur["step"])
        if nxt is None:
            return dict(solved=False, time=t, length=None, uncertain=uncertain, created=created, reason="no calls left")
        cur = nxt


def parse_profile(log_text):
    """STEP lines of a profiling run -> profile dict"""
    import re
    prof = {}
    for m in re.finditer(r"STEP \d+ length (\d+) (SAT|UNSAT|TIMEOUT) time \S+ clauses (\d+) vars (\d+).*?"
                         r"encode_time (\S+) solve_time (\S+)", log_text):
        prof[int(m.group(1))] = dict(status=m.group(2), encode=float(m.group(5)), solve=float(m.group(6)),
                                     clauses=int(m.group(3)), vars=int(m.group(4)))
    return prof


if __name__ == "__main__":
    prof = {int(k): v for k, v in json.load(open(sys.argv[1])).items()}
    print(simulate(prof, ByIteration()))
