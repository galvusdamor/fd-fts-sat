#!/usr/bin/env python3
"""
Profiling campaign for tuning rintanen() offline (see rintanen_sim.py).

For every instance, algorithm A runs the given lengths in ascending order and
keeps going after the first plan (continue_after_first_plan), each call capped
by step_time_limit and the whole run by --overall-limit. The STEP lines give,
per length, status (SAT / UNSAT / TIMEOUT), encode and solve time and formula
size; they are written to <outdir>/<domain>__<problem>/profile.json.

Resumable: instances with a profile.json are skipped unless --lengths asks for
lengths they do not have yet, in which case only the missing lengths run and
are merged into the existing profile (round 2 and later).

Timing needs a constant machine load: on the 4-core/8-thread dev machine keep
--jobs at 4 (6 jobs cost 1.6-2x the CPU time per call).
"""

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, REPO)
import _bdd_common as B                        # noqa: E402
import rintanen_sim as R                       # noqa: E402

ENCODER = B.bdd(label_order="label_order_goal_chains(ancestor_depth=2,goal_pairs=2,leftover_layer=true,"
                            "state_budget=200000,max_states=20000,exact_time_limit=1)")
TRANSFORM = B.TRANSFORM_OPTS["-shr"]
FLOAT = r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?"


def h2_without_limits():
    """B.driver_opts(h2=True) minus its own time/memory limits, which would
    otherwise override ours (later options win)."""
    opts, out, skip = B.driver_opts(h2=True), [], False
    for o in opts:
        if skip:
            skip = False
            continue
        if o in ("--overall-time-limit", "--overall-memory-limit"):
            skip = True
            continue
        out.append(o)
    return out


def name_of(path):
    d, f = path.split("/")[-2:]
    return f"{d}__{f[:-5]}"


def profile_instance(path, lengths, args):
    rundir = os.path.join(args.outdir, name_of(path))
    os.makedirs(rundir, exist_ok=True)
    pf = os.path.join(rundir, "profile.json")
    old = json.load(open(pf)) if os.path.exists(pf) else None
    have = {int(k) for k in old["lengths"]} if old else set()
    todo = sorted(set(lengths) - have)
    if old and not todo:
        return name_of(path), "skipped", old
    search = (f"sat(encoder={ENCODER},solver_quiet=true,continue_after_first_plan=true,"
              f"step_time_limit={args.step_limit},length_strategy=length_list(lengths=[{','.join(map(str, todo))}]))")
    cmd = ([sys.executable, os.path.join(REPO, "fast-downward.py"), "--build", args.build,
            "--overall-time-limit", f"{args.overall_limit}s", "--overall-memory-limit", f"{args.memory_limit}m"]
           + h2_without_limits() + [path] + TRANSFORM + ["--search", search])
    t0 = time.time()
    res = subprocess.run(cmd, cwd=rundir, capture_output=True, text=True)
    log = res.stdout + res.stderr
    tag = f"round{len(old['rounds']) + 1 if old else 1}"
    open(os.path.join(rundir, f"{tag}.log"), "w").write(log)
    for junk in ("output.sas", "sas_plan", "output"):
        try:
            os.remove(os.path.join(rundir, junk))
        except OSError:
            pass
    prof = R.parse_profile(log)
    meta = dict(round=tag, requested=todo, exit=res.returncode, wall=round(time.time() - t0, 2),
                trivial="Task solved without search" in log,
                last_requested_done=bool(todo) and max(todo) in prof)
    for key, pat in (("transform_time", rf"Transform time: ({FLOAT})s"), ("init_time", rf"SAT init time: ({FLOAT})s"),
                     ("translate_time", rf"Done! \[({FLOAT})s CPU")):
        m = re.search(pat, log)
        if m:
            meta[key] = float(m.group(1))
    m = re.search(r"Main task: FTSTask with (\d+) variables, (\d+) labels", log)
    if m:
        meta["factors"], meta["labels"] = int(m.group(1)), int(m.group(2))
    merged = dict(instance=path, lengths={}, rounds=[])
    if old:
        merged = old
    merged["lengths"].update({str(k): v for k, v in prof.items()})
    merged["rounds"].append(meta)
    json.dump(merged, open(pf, "w"))
    return name_of(path), "done", merged


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("instances", help="JSON list of problem paths, or a text file with one path per line")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--lengths", default="1-30", help="e.g. 1-30 or 1-30,34,40 (applies to every instance)")
    ap.add_argument("--demand", help="JSON {instance_name: [lengths]} for targeted rounds; overrides --lengths")
    ap.add_argument("--step-limit", type=int, default=60)
    ap.add_argument("--overall-limit", type=int, default=300)
    ap.add_argument("--memory-limit", type=int, default=3000)
    ap.add_argument("--build", default="release64")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--limit", type=int, default=0, help="only the first N instances (testing)")
    args = ap.parse_args()
    try:
        paths = json.load(open(args.instances))
    except json.JSONDecodeError:
        paths = [l.strip() for l in open(args.instances) if l.strip() and not l.startswith("#")]
    lengths = []
    for part in args.lengths.split(","):
        a, _, b = part.partition("-")
        lengths += list(range(int(a), int(b) + 1)) if b else [int(a)]
    demand = json.load(open(args.demand)) if args.demand else None
    # interleave domains so partial results cover the whole suite early
    by_dom = {}
    for p in paths:
        by_dom.setdefault(p.split("/")[-2], []).append(p)
    order = []
    while any(by_dom.values()):
        for d in sorted(by_dom):
            if by_dom[d]:
                order.append(by_dom[d].pop(0))
    if demand is not None:
        order = [p for p in order if name_of(p) in demand]
    if args.limit:
        order = order[:args.limit]
    os.makedirs(args.outdir, exist_ok=True)
    done = 0
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        futs = {ex.submit(profile_instance, p, demand[name_of(p)] if demand is not None else lengths, args): p
                for p in order}
        for f in concurrent.futures.as_completed(futs):
            name, status, prof = f.result()
            done += 1
            L = prof["lengths"]
            print(f"[{done}/{len(order)}] {name:55s} {status:7s} lengths {len(L):3d} "
                  f"sat {sum(1 for v in L.values() if v['status']=='SAT'):3d} "
                  f"timeout {sum(1 for v in L.values() if v['status']=='TIMEOUT'):2d} "
                  f"wall {prof['rounds'][-1]['wall'] if status == 'done' else '-'}", flush=True)


if __name__ == "__main__":
    main()
