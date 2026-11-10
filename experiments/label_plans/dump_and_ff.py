#!/usr/bin/env python3
"""
Step 1 of the label-plan dataset: per task, one planner call that transforms with -shr, writes the
transformed FTS (dump_fts) and runs lazy greedy best-first search with FF and preferred operators
on it (as the alias seq-agl-fts-ff), writing the plan as label ids of the transformed task
(--internal-fts-plan-file). Output, resumable:

  OUT/shr/<domain>__<task>.fts.gz    the FTS (format: experiments/fts_dumps/README of the dumps)
  OUT/ff/<domain>__<task>.txt        the FF plan, one label id per line (missing: not solved)
  OUT/labelmap/<domain>__<task>.txt.gz  atomic label (= SAS operator) -> transformed label, and
                                     the operator names (FTS_DUMP_LABEL_MAP of the -shr step): maps
                                     operator plans (sas_plan) of any planner to label plans
  OUT/sasplan/<domain>__<task>.txt   the FF plan as operators (after plan reconstruction)
  OUT/logs/<domain>__<task>.log      planner output

Usage: dump_and_ff.py TASKS OUT [--build release64-unified-5] [--jobs 1] [--time-limit 300]
                                 [--memory-limit 3500m]
TASKS: one PDDL problem path per line. Use the build of the plans the dataset is to match: label
ids depend on the transformation code.
"""
import argparse, gzip, os, shutil, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

REPO = "/home/gregor/data/claude/fd-fts-sat"
sys.path.insert(0, f"{REPO}/experiments")
import _bdd_common as B

SHR = B.TRANSFORM_OPTS_2026_10["-shr"][1]
SEARCH = ["--heuristic", "hff=ff(cost_type=one)",
          "--search", "lazy_greedy([hff], preferred=[hff], cost_type=one)"]


def name_of(problem):
    return problem.rstrip("/").split("/")[-2] + "__" + os.path.basename(problem)[:-len(".pddl")]


def run(problem, a):
    name = name_of(problem)
    dump = f"{a.out}/shr/{name}.fts.gz"
    log = f"{a.out}/logs/{name}.log"
    if os.path.exists(log):
        return name, "done before"
    work = f"{a.out}/work/{name}"
    os.makedirs(work, exist_ok=True)
    cmd = [sys.executable, f"{REPO}/fast-downward.py", "--build", a.build,
           "--overall-time-limit", f"{a.time_limit}s", "--overall-memory-limit", a.memory_limit,
           problem, "--internal-fts-plan-file", "ff_plan.txt",
           "--transform", f"seq(transformations=[{SHR},dump_fts(filename=fts.txt)])"] + SEARCH
    t0 = time.time()
    env = dict(os.environ, FTS_DUMP_LABEL_MAP=f"{work}/label_map.txt")
    with open(f"{work}/log", "w") as f:
        rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=work, env=env)
    if os.path.exists(f"{work}/label_map.txt"):
        with open(f"{work}/label_map.txt", "rb") as src, gzip.open(f"{a.out}/labelmap/{name}.txt.gz", "wb") as dst:
            shutil.copyfileobj(src, dst)
    if os.path.exists(f"{work}/fts.txt"):
        with open(f"{work}/fts.txt", "rb") as src, gzip.open(dump + ".part", "wb") as dst:
            shutil.copyfileobj(src, dst)
        os.replace(dump + ".part", dump)
    if os.path.exists(f"{work}/ff_plan.txt"):
        shutil.copy(f"{work}/ff_plan.txt", f"{a.out}/ff/{name}.txt")
    if os.path.exists(f"{work}/sas_plan"):
        shutil.copy(f"{work}/sas_plan", f"{a.out}/sasplan/{name}.txt")
    shutil.copy(f"{work}/log", log + ".part")
    os.replace(log + ".part", log)
    shutil.rmtree(work)
    return name, f"rc {rc} {time.time() - t0:.0f}s dump {os.path.exists(dump)} ff {os.path.exists(f'{a.out}/ff/{name}.txt')}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tasks")
    ap.add_argument("out")
    ap.add_argument("--build", default="release64-unified-5")
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--time-limit", type=int, default=300)
    ap.add_argument("--memory-limit", default="3500m")
    a = ap.parse_args()
    for d in ["shr", "ff", "labelmap", "sasplan", "logs", "work"]:
        os.makedirs(f"{a.out}/{d}", exist_ok=True)
    problems = [l.strip() for l in open(a.tasks) if l.strip()]
    with ThreadPoolExecutor(a.jobs) as ex:
        for name, status in ex.map(lambda p: run(p, a), problems):
            print(name, status, flush=True)


if __name__ == "__main__":
    main()
