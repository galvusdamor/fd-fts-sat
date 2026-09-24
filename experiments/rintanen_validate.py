#!/usr/bin/env python3
"""
Validate rintanen_sim.py against real rintanen runs.

Per instance: (1) run rintanen(trace_schedule=true) for every configuration;
(2) profile, with algorithm A, exactly the lengths those runs created;
(3) simulate every configuration on that profile and compare predicted vs.
measured time-to-plan (RSCHED t of the "sat" event) and solving length.
"""

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, REPO)
import rintanen_sim as R                      # noqa: E402
from compare_optimal_order import TRANSFORM   # noqa: E402
from driver.util import find_domain_filename  # noqa: E402

ENC = ("bdd_sat(one_step_only=false,combinebdds=false,impltseitsin=true,omitforcedvariables=true,"
       "forcedvariablesthreshold=1,bdd_size_limit=-1,cutbdds=false,"
       "label_order=label_order_goal_chains(ancestor_depth=2,goal_pairs=2,leftover_layer=true,"
       "state_budget=200000,max_states=20000,exact_time_limit=1),"
       "bdd_init_time_limit=600,bdd_node_limit=60000000,cudd_init_nodes=100000,cudd_cache_size=1000000)")

# name -> (rintanen options, simulator strategy, sim kwargs)
CONFIGS = {
    "V1_default": ("length_strategy=by_iteration(),scheduler_interval=1",
                   R.ByIteration(), dict(interval=1.0, max_parallel=20)),
    "V2_s1_m141": ("length_strategy=by_iteration(start_length=1,multiplier=1.41,strictly_increasing=true),scheduler_interval=1",
                   R.ByIteration(1, 1.41, strictly_increasing=True), dict(interval=1.0, max_parallel=20)),
    "V3_s1_m12_100ms_p8": ("length_strategy=by_iteration(start_length=1,multiplier=1.2,strictly_increasing=true),"
                           "scheduler_interval_ms=100,max_parallel_calls=8",
                           R.ByIteration(1, 1.2, strictly_increasing=True), dict(interval=0.1, max_parallel=8)),
    "V4_s2_m141_250ms_g08": ("length_strategy=by_iteration(start_length=2,multiplier=1.41,strictly_increasing=true),"
                             "scheduler_interval_ms=250,gamma=0.8",
                             R.ByIteration(2, 1.41, strictly_increasing=True), dict(interval=0.25, gamma=0.8, max_parallel=20)),
    "V5_inc1_500ms_p4": ("length_strategy=by_increment(start_length=1,increment=1),scheduler_interval_ms=500,max_parallel_calls=4",
                         R.ByIncrement(1, 1), dict(interval=0.5, max_parallel=4)),
}


def fd(problem, search, rundir, args, plan=True):
    os.makedirs(rundir, exist_ok=True)
    cmd = [sys.executable, os.path.join(REPO, "fast-downward.py"), "--build", args.build,
           "--overall-time-limit", f"{args.time_limit + 60}s", "--overall-memory-limit", f"{args.memory_limit}m"]
    if plan:
        cmd += ["--plan-file", os.path.join(rundir, "plan")]
    cmd += [problem, "--transform", TRANSFORM, "--search", search]
    res = subprocess.run(cmd, cwd=rundir, capture_output=True, text=True)
    log = res.stdout + res.stderr
    open(os.path.join(rundir, "run.log"), "w").write(log)
    return log


def run_real(problem, name, args):
    opts, _, _ = CONFIGS[name]
    inst = "/".join(problem.split("/")[-2:]).replace(".pddl", "")
    rundir = os.path.join(args.outdir, inst.replace("/", "__"), name)
    search = (f"rintanen(encoder={ENC},solver_quiet=true,{opts},memory_limit_mb={args.rintanen_memory},"
              f"trace_schedule=true)")
    log = fd(problem, search, rundir, args)
    ev = [(float(m.group(1)), m.group(2), int(m.group(3)), int(m.group(4)))
          for m in re.finditer(r"RSCHED t (\S+) (\w+) step (\d+) length (\d+)", log)]
    sat = [e for e in ev if e[1] == "sat"]
    lengths = sorted({e[3] for e in ev if e[1] == "turn"})
    valid = None
    plan = os.path.join(rundir, "plan")
    if sat and os.path.exists(plan):
        v = subprocess.run([args.val, find_domain_filename(problem), problem, plan], capture_output=True, text=True)
        valid = "Plan valid" in v.stdout
    return dict(instance=inst, config=name, solved=bool(sat), time=sat[0][0] if sat else None,
                length=sat[0][3] if sat else None, lengths=lengths, valid=valid,
                max_open=None, paused="paused" in log)


def run_profile(problem, lengths, args):
    inst = "/".join(problem.split("/")[-2:]).replace(".pddl", "")
    rundir = os.path.join(args.outdir, inst.replace("/", "__"), "profile")
    search = (f"sat(encoder={ENC},solver_quiet=true,continue_after_first_plan=true,"
              f"step_time_limit={args.time_limit},length_strategy=length_list(lengths=[{','.join(map(str, lengths))}]))")
    log = fd(problem, search, rundir, args, plan=False)
    prof = R.parse_profile(log)
    json.dump(prof, open(os.path.join(rundir, "profile.json"), "w"), indent=0)
    return inst, prof


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("instances")
    ap.add_argument("--benchmarks", default="/home/gregor/data/lisat/classical-domains/classical")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--build", default="release64")
    ap.add_argument("--time-limit", type=int, default=60, help="rintanen run / per-length profile cap, seconds")
    ap.add_argument("--memory-limit", type=int, default=3500)
    ap.add_argument("--rintanen-memory", type=int, default=3000)
    # real runs and profiles should run under the same machine load, otherwise
    # their solve times are not comparable (measured: ~1.2-1.3x at 5 vs 2 jobs)
    ap.add_argument("--jobs-real", type=int, default=4)
    ap.add_argument("--jobs-profile", type=int, default=4)
    ap.add_argument("--val", default="/home/gregor/data/lisat/VAL/build/bin/Validate")
    args = ap.parse_args()
    probs = [os.path.join(args.benchmarks, l.strip() + ".pddl") for l in open(args.instances) if l.strip() and not l.startswith("#")]

    real = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs_real) as ex:
        for f in concurrent.futures.as_completed([ex.submit(run_real, p, c, args) for p in probs for c in CONFIGS]):
            r = f.result(); real.append(r)
            print(f"real {r['instance']:40s} {r['config']:22s} solved={r['solved']} t={r['time']} len={r['length']} "
                  f"valid={r['valid']} lengths={len(r['lengths'])}", flush=True)
    json.dump(real, open(os.path.join(args.outdir, "real.json"), "w"), indent=1)

    need = {}
    for r in real:
        need.setdefault(r["instance"], set()).update(r["lengths"])
    profiles = {}
    with concurrent.futures.ThreadPoolExecutor(args.jobs_profile) as ex:
        futs = [ex.submit(run_profile, p, sorted(need["/".join(p.split("/")[-2:]).replace(".pddl", "")]), args)
                for p in probs if need.get("/".join(p.split("/")[-2:]).replace(".pddl", ""))]
        for f in concurrent.futures.as_completed(futs):
            inst, prof = f.result(); profiles[inst] = prof
            print(f"profile {inst:40s} {len(prof)} lengths", flush=True)
    json.dump({k: {str(a): b for a, b in v.items()} for k, v in profiles.items()},
              open(os.path.join(args.outdir, "profiles.json"), "w"), indent=0)

    rows = []
    for r in real:
        prof = profiles.get(r["instance"])
        if prof is None:
            continue
        _, strat, kw = CONFIGS[r["config"]]
        s = R.simulate(prof, strat, time_limit=args.time_limit, **kw)
        rows.append(dict(r, sim_solved=s["solved"], sim_time=s["time"], sim_length=s["length"],
                         sim_uncertain=s["uncertain"], sim_reason=s["reason"]))
    json.dump(rows, open(os.path.join(args.outdir, "comparison.json"), "w"), indent=1)
    print(f"\n{'instance':40s} {'config':22s} {'real t/len':>14s} {'sim t/len':>14s}  note")
    for r in sorted(rows, key=lambda r: (r["instance"], r["config"])):
        rt = f"{r['time']:.2f}/{r['length']}" if r["solved"] else "-"
        st = f"{r['sim_time']:.2f}/{r['sim_length']}" if r["sim_solved"] else ("?" if r["sim_solved"] is None else "-")
        print(f"{r['instance']:40s} {r['config']:22s} {rt:>14s} {st:>14s}  {r['sim_reason'] if not r['sim_solved'] else ''} {r['sim_uncertain'] or ''}")


if __name__ == "__main__":
    main()
