#!/usr/bin/env python3
"""
Run bdd_sat(one_step_only=false) with several label orders on a set of PDDL
instances (-shr transform, sat() + one_by_one()) and tabulate horizon, search
and total time, VAL validity and the GOALCHAINS statistics of
label_order_goal_chains. Results go to <outdir>/results.csv.
"""

import argparse
import concurrent.futures
import csv
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, REPO)
import _bdd_common as B                      # noqa: E402
from driver.util import find_domain_filename  # noqa: E402
from compare_optimal_order import TRANSFORM, FLOAT   # noqa: E402

ORDERS = {
    "linear": "label_order_linear()",
    "relaxed": "label_order_relaxed()",
    "goal_chains": "label_order_goal_chains()",
}


def run(problem, order_name, args):
    name = "/".join(problem.split("/")[-2:]).replace(".pddl", "")
    rundir = os.path.join(args.outdir, name.replace("/", "__"), order_name)
    os.makedirs(rundir, exist_ok=True)
    plan = os.path.join(rundir, "plan")
    enc = B.bdd(label_order=args.specs[order_name])
    cmd = [sys.executable, os.path.join(REPO, "fast-downward.py"), "--build", args.build,
           "--plan-file", plan, "--overall-time-limit", f"{args.time_limit}s",
           "--overall-memory-limit", f"{args.memory_limit}m", problem,
           "--transform", TRANSFORM,
           "--search", f"sat(encoder={enc},solver_quiet=true,length_strategy=one_by_one())"]
    res = subprocess.run(cmd, cwd=rundir, capture_output=True, text=True)
    log = res.stdout + res.stderr
    open(os.path.join(rundir, "run.log"), "w").write(log)
    row = dict(instance=name, order=order_name, exit=res.returncode,
               solved="Solution found." in log, trivial="Task solved without search" in log)
    lengths = re.findall(r"ENCSTAT length (\d+) ", log)
    row["horizon"] = int(lengths[-1]) if lengths and row["solved"] else None
    for key, pat in (("total", rf"Total time: ({FLOAT})s"), ("search", rf"Search time: ({FLOAT})s")):
        m = re.search(pat, log)
        row[key] = float(m.group(1)) if m else None
    m = re.search(r"GOALCHAINS goal_factors .*", log)
    if m:
        toks = m.group(0).split()[1:]
        for k, v in zip(toks[::2], toks[1::2]):
            row["gc_" + k] = v
    row["valid"] = None
    if os.path.exists(plan) and args.val:
        v = subprocess.run([args.val, find_domain_filename(problem), problem, plan],
                           capture_output=True, text=True)
        row["valid"] = "Plan valid" in v.stdout
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("instances", help="file with domain/problem per line")
    ap.add_argument("--benchmarks", default="/home/gregor/data/lisat/classical-domains/classical")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--orders", default=",".join(ORDERS), help="names from ORDERS")
    ap.add_argument("--config", action="append", default=[], metavar="NAME=LABEL_ORDER",
                    help="extra configuration, e.g. gc_d2=label_order_goal_chains(ancestor_depth=2); "
                         "when given, only these (and --orders if set explicitly) run")
    ap.add_argument("--build", default="release64")
    ap.add_argument("--time-limit", type=int, default=600)
    ap.add_argument("--memory-limit", type=int, default=1500)
    ap.add_argument("--val", default="/home/gregor/data/lisat/VAL/build/bin/Validate")
    ap.add_argument("--jobs", type=int, default=8)
    args = ap.parse_args()
    if not os.path.exists(args.val):
        args.val = None
    probs = []
    for line in open(args.instances):
        line = line.strip()
        if line and not line.startswith("#"):
            p = os.path.join(args.benchmarks, line)
            probs.append(p if p.endswith(".pddl") else p + ".pddl")
    args.specs = dict(ORDERS)
    names = [] if args.config and "--orders" not in sys.argv else args.orders.split(",")
    for c in args.config:
        name, spec = c.split("=", 1)
        args.specs[name] = spec
        names.append(name)
    jobs = [(p, o) for o in names for p in probs]
    rows = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        for f in concurrent.futures.as_completed([ex.submit(run, p, o, args) for p, o in jobs]):
            r = f.result()
            rows.append(r)
            print(f"done {r['instance']:45s} {r['order']:12s} h={r['horizon']} search={r['search']} "
                  f"valid={r['valid']}", flush=True)
    rows.sort(key=lambda r: (r["instance"], r["order"]))
    cols = sorted({k for r in rows for k in r}, key=lambda k: (k.startswith("gc_"), k))
    with open(os.path.join(args.outdir, "results.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)


if __name__ == "__main__":
    main()
