#!/usr/bin/env python3
"""
How much is there to gain from a better label order?

For each instance: (1) solve it with bdd_sat under a baseline label order and
dump the plan on the transformed task; (2) compute the label order that packs
*that plan* into the fewest time steps (optimal_label_order.py, exact); (3)
solve the instance again with that order, leftover labels in relaxed order;
(4) compare horizons and times. Step (2) gives a horizon the planner is
guaranteed to reach or beat in step (3) -- the plan exists at that horizon --
so (3) measures what the solver gains from it.

The optimal order is tailored to one plan of one baseline run; a different
plan might permit an even shorter horizon. It is a lower bound on "the best
order for this task" only in the sense of being the best for a plan we have.
"""

import argparse
import concurrent.futures
import csv
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, REPO)
import _bdd_common as B                      # noqa: E402
from driver.util import find_domain_filename  # noqa: E402

TRANSFORM = ("transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation("
             "ignore_irrelevant_tau_groups=false),label_reduction=exact(max_time=300,"
             "atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,"
             "run_main_loop=false,max_time=900,cost_type=one,prune_transitions_from_goal=true)")

FLOAT = r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?"


def run_planner(problem, order, rundir, args):
    os.makedirs(rundir, exist_ok=True)
    plan = os.path.join(rundir, "plan")
    fts_plan = os.path.join(rundir, "plan.fts")
    enc = B.bdd(label_order=order)
    cmd = [sys.executable, os.path.join(REPO, "fast-downward.py"), "--build", args.build,
           "--plan-file", plan,
           "--overall-time-limit", f"{args.time_limit}s",
           "--overall-memory-limit", f"{args.memory_limit}m",
           problem, "--internal-fts-plan-file", fts_plan,
           "--transform", TRANSFORM,
           "--search", f"sat(encoder={enc},solver_quiet=true,length_strategy=one_by_one())"]
    t0 = time.time()
    res = subprocess.run(cmd, cwd=rundir, capture_output=True, text=True)
    wall = time.time() - t0
    log = res.stdout + res.stderr
    open(os.path.join(rundir, "run.log"), "w").write(log)
    r = dict(exit=res.returncode, wall=round(wall, 2), horizon=None, total=None, search=None,
             solved="Solution found." in log, trivial="Task solved without search" in log,
             plan=plan if os.path.exists(plan) else None,
             fts_plan=fts_plan if os.path.exists(fts_plan) else None, valid=None)
    lengths = re.findall(r"ENCSTAT length (\d+) ", log)
    if lengths and r["solved"]:
        r["horizon"] = int(lengths[-1])
    m = re.search(rf"Total time: ({FLOAT})s", log)
    if m:
        r["total"] = float(m.group(1))
    m = re.search(rf"Search time: ({FLOAT})s", log)
    if m:
        r["search"] = float(m.group(1))
    if r["plan"] and args.val:
        domain = find_domain_filename(problem)
        v = subprocess.run([args.val, domain, problem, plan], capture_output=True, text=True)
        r["valid"] = "Plan valid" in v.stdout
    return r


def infer_order(fts_plan, order_file, args):
    cmd = [sys.executable, os.path.join(HERE, "optimal_label_order.py"), fts_plan, "-o", order_file,
           "--kissat", args.kissat, "--time-limit", str(args.infer_time_limit)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    m = re.search(r"OPTORDER (.*)", res.stdout)
    if not m:
        return dict(error=res.stderr.strip()[-300:])
    toks = m.group(1).split()
    st = dict(zip(toks[::2], toks[1::2]))
    return {k: (float(v) if k == "time" else int(v)) for k, v in st.items()}


def one_instance(problem, base_order, args):
    name = "/".join(problem.split("/")[-2:]).replace(".pddl", "")
    tag = re.sub(r"\W+", "_", base_order).strip("_")
    # FD's option parser lower-cases the whole --search string, file names in
    # label_order_file(filename=...) included, so the run directory must be
    # lower-case for the planner to find the order file.
    rundir = os.path.join(args.outdir, (name.replace("/", "__") + "/" + tag).lower())
    row = dict(instance=name, base_order=base_order)
    base = run_planner(problem, base_order, os.path.join(rundir, "base"), args)
    row.update(base_horizon=base["horizon"], base_total=base["total"], base_search=base["search"],
               base_valid=base["valid"], base_trivial=base["trivial"])
    if not base["fts_plan"] or base["trivial"]:
        row["note"] = "trivial" if base["trivial"] else "base unsolved"
        return row
    # Re-infer whenever the planner beats the prediction: it then found a
    # different plan at a shorter horizon, whose own optimal order may allow
    # shorter still. Horizons are non-increasing along the chain, so this
    # terminates; stop once achieved == predicted (nothing new to learn).
    plan_file = base["fts_plan"]
    chain = []
    for it in range(args.max_iterations):
        order_file = os.path.join(rundir, f"optimal.{it}.order")
        st = infer_order(plan_file, order_file, args)
        if "error" in st:
            row["note"] = "infer failed: " + st["error"]
            break
        opt = run_planner(problem, f"label_order_file(filename={order_file},leftover={args.leftover})",
                          os.path.join(rundir, f"opt{it}"), args)
        chain.append(dict(st=st, run=opt))
        if it == 0:
            row.update(plan_labels=st["plan_labels"], distinct=st["distinct"], fixed_breaks=st["fixed_breaks"],
                       predicted_horizon=st["predicted_horizon"],
                       first_occ_horizon=st["first_occurrence_breaks"] + 1,
                       infer_optimal=st["optimal"], infer_time=st["time"],
                       opt_horizon=opt["horizon"], opt_total=opt["total"], opt_search=opt["search"],
                       opt_valid=opt["valid"])
        if opt["horizon"] is None or not opt["fts_plan"]:
            row["note"] = f"opt run {it} unsolved"
            break
        if opt["horizon"] >= st["predicted_horizon"]:
            break
        plan_file = opt["fts_plan"]
    if chain:
        last = chain[-1]
        row.update(iterations=len(chain),
                   horizon_chain=">".join(f"{c['st']['predicted_horizon']}/{c['run']['horizon']}" for c in chain),
                   final_predicted=last["st"]["predicted_horizon"], final_horizon=last["run"]["horizon"],
                   final_total=last["run"]["total"], final_search=last["run"]["search"],
                   final_valid=last["run"]["valid"],
                   infer_time_total=round(sum(c["st"]["time"] for c in chain), 3),
                   all_valid=all(c["run"]["valid"] for c in chain))
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("instances", nargs="+", help="problem PDDL paths, or domain/problem relative to --benchmarks")
    ap.add_argument("--benchmarks", default="/home/gregor/data/lisat/classical-domains/classical")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--build", default="release64")
    ap.add_argument("--base-order", action="append", help="baseline LabelOrderFinder(s); default label_order_linear()")
    ap.add_argument("--leftover", default="label_order_relaxed()", help="order for labels not in the plan")
    ap.add_argument("--time-limit", type=int, default=600, help="per planner run, seconds")
    ap.add_argument("--memory-limit", type=int, default=3500, help="per planner run, MB")
    ap.add_argument("--infer-time-limit", type=float, default=300)
    ap.add_argument("--max-iterations", type=int, default=6, help="re-inference rounds per instance")
    ap.add_argument("--kissat", default=os.environ.get("KISSAT", "kissat"))
    ap.add_argument("--val", default="/home/gregor/data/lisat/VAL/build/bin/Validate")
    ap.add_argument("--jobs", type=int, default=4)
    args = ap.parse_args()
    if not args.base_order:
        args.base_order = ["label_order_linear()"]
    if not os.path.exists(args.val):
        args.val = None
    if args.outdir != args.outdir.lower():
        sys.exit("--outdir must not contain upper-case letters: the planner lower-cases the "
                 "--search string and would not find label_order_file(filename=...) under it")

    problems = []
    for inst in args.instances:
        p = inst if os.path.exists(inst) else os.path.join(args.benchmarks, inst)
        if not p.endswith(".pddl"):
            p += ".pddl"
        problems.append(p)
    os.makedirs(args.outdir, exist_ok=True)

    jobs = [(p, o) for o in args.base_order for p in problems]  # first baseline finishes first
    rows = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        futs = {ex.submit(one_instance, p, o, args): (p, o) for p, o in jobs}
        for f in concurrent.futures.as_completed(futs):
            row = f.result()
            rows.append(row)
            print(f"done {row['instance']:40s} {row['base_order'][:24]:24s} base h={row.get('base_horizon')} "
                  f"t={row.get('base_total')}  pred h={row.get('predicted_horizon')}  "
                  f"chain {row.get('horizon_chain')} final h={row.get('final_horizon')} "
                  f"t={row.get('final_total')} valid={row.get('all_valid')} {row.get('note', '')}", flush=True)
    rows.sort(key=lambda r: (r["instance"], r["base_order"]))
    cols = ["instance", "base_order", "base_horizon", "base_total", "base_search", "base_valid",
            "plan_labels", "distinct", "fixed_breaks", "first_occ_horizon", "predicted_horizon",
            "infer_optimal", "infer_time", "opt_horizon", "opt_total", "opt_search", "opt_valid",
            "iterations", "horizon_chain", "final_predicted", "final_horizon", "final_total",
            "final_search", "final_valid", "infer_time_total", "all_valid", "note"]
    with open(os.path.join(args.outdir, "results.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print()
    print(f"{'instance':40s} {'base':>5s} {'search':>8s} | {'labels':>6s} {'pred/achieved chain':>22s} | "
          f"{'final':>5s} {'search':>8s} valid")
    for r in rows:
        print(f"{r['instance']:40s} {str(r.get('base_horizon')):>5s} {str(r.get('base_search')):>8s} | "
              f"{str(r.get('plan_labels')):>6s} {str(r.get('horizon_chain')):>22s} | "
              f"{str(r.get('final_horizon')):>5s} {str(r.get('final_search')):>8s} {r.get('all_valid')} {r.get('note', '')}")


if __name__ == "__main__":
    main()
