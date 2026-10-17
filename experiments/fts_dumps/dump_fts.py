#!/usr/bin/env python3
"""Dumps the FTS of every task: atomic (-ntr), after -shr, after -shr + tau_baking, gzipped, to
FTSDIR/<variant>/<domain>__<task>.fts.gz, via the dump_fts transformation. No search.
Usage: dump_fts.py TASKS [--build release64] [--time-limit 600]"""
import argparse, gzip, os, shutil, subprocess, sys, time
REPO = "/home/gregor/data/claude/fd-fts-sat"
sys.path.insert(0, f"{REPO}/experiments")
import _bdd_common as B

SHR = B.TRANSFORM_OPTS["-shr"][1]
FTSDIR = os.environ.get("FTS_DUMPS", "/home/gregor/data/fts-dumps/autoscale-21.11-agile-303")
VARIANTS = ["ntr", "shr", "shr_tau"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tasks")
    ap.add_argument("--build", default="release64")
    ap.add_argument("--time-limit", type=int, default=600)
    a = ap.parse_args()
    for v in VARIANTS:
        os.makedirs(f"{FTSDIR}/{v}", exist_ok=True)
    os.makedirs(f"{FTSDIR}/logs", exist_ok=True)
    for p in [l.strip() for l in open(a.tasks) if l.strip()]:
        name = p.split("/")[-2] + "__" + os.path.basename(p)[:-5]
        if all(os.path.exists(f"{FTSDIR}/{v}/{name}.fts.gz") for v in VARIANTS):
            continue
        work = f"{FTSDIR}/work/{name}"
        os.makedirs(work, exist_ok=True)
        files = {v: f"{work}/{v}.fts" for v in VARIANTS}
        transform = (f"seq(transformations=[cost(cost_type=one),dump_fts(filename={files['ntr']}),"
                     f"{SHR},dump_fts(filename={files['shr']}),"
                     f"tau_baking(),dump_fts(filename={files['shr_tau']},exit_after=true)])")
        cmd = [sys.executable, f"{REPO}/fast-downward.py", "--build", a.build,
               "--overall-time-limit", f"{a.time_limit}s", "--overall-memory-limit", "3000m", p,
               "--transform", transform, "--search", "sat(encoder=bdd_sat(),length_strategy=one_by_one())"]
        t0 = time.time()
        with open(f"{FTSDIR}/logs/{name}.log", "w") as log:
            rc = subprocess.call(cmd, stdout=log, stderr=subprocess.STDOUT, cwd=work)
        done = []
        for v in VARIANTS:
            if os.path.exists(files[v]):
                with open(files[v], "rb") as src, gzip.open(f"{FTSDIR}/{v}/{name}.fts.gz", "wb") as dst:
                    shutil.copyfileobj(src, dst)
                done.append(v)
        shutil.rmtree(work, ignore_errors=True)
        print(name, rc, " ".join(done), f"{time.time() - t0:.1f}s", flush=True)


if __name__ == "__main__":
    main()
