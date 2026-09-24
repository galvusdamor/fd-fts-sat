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
Not modelled: the memory limit (formula generation pauses near it), kissat's
callback granularity, and cache effects of interleaving.

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


def simulate(profile, strategy, interval=1.0, gamma=1.0, max_parallel=20, time_limit=1800.0):
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
            call = dict(step=next_step, L=L, G=None, S=None, status=None, done_gen=False, used=0.0)
        else:
            S = p["solve"] if p["status"] in ("SAT", "UNSAT") else INF
            call = dict(step=next_step, L=L, G=p["encode"], S=S, cap=p["solve"], status=p["status"],
                        done_gen=False, used=0.0)
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

    def pick_after(step):
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
            t += cur["G"]
            cur["done_gen"] = True
            quantum = max(0.0, budget - cur["G"])
        remaining = cur["S"] - cur["used"]
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
    for m in re.finditer(r"STEP \d+ length (\d+) (SAT|UNSAT|TIMEOUT) time \S+ .*?encode_time (\S+) solve_time (\S+)", log_text):
        prof[int(m.group(1))] = dict(status=m.group(2), encode=float(m.group(3)), solve=float(m.group(4)))
    return prof


if __name__ == "__main__":
    prof = {int(k): v for k, v in json.load(open(sys.argv[1])).items()}
    print(simulate(prof, ByIteration()))
