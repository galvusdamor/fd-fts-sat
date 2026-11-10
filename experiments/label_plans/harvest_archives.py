#!/usr/bin/env python3
"""
Raw plans from lab run archives (*.tar.bz2 / *.tar.gz of an experiment's run directories), for the
label-plan dataset. Streams every archive once (no unpacking) and writes, per task,

  OUT/<domain>__<task>.jsonl   one line per run with a plan:
      {"archive": ..., "run": ..., "algorithm": <configuration>, "revision": ...,
       "label_steps": [[labels], ...] | null,   "Time t" / "Label : l" lines of run.log (SAT
                                                encodings; label ids of that run's transformation)
       "operators": [names] | null}             sas_plan (any planner, any transformation)
  OUT/summary.json             runs and plans per archive

Operator plans become label plans of a task's -shr dump through its label map (dump_and_ff.py);
label steps are only usable where the run's label numbering is the dump's (validation decides).

Usage: harvest_archives.py OUT ARCHIVE...    (run throttled, e.g. under a CPU quota)
"""
import json, os, re, sys, tarfile
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harvest import parse_plans


def flush(run, out, counts, archive):
    props = run.get("static-properties")
    if not props:
        return
    try:
        p = json.loads(props)
    except ValueError:
        return
    steps = parse_plans(run["run.log"]) if "run.log" in run else []
    ops = None
    if "sas_plan" in run:
        ops = [l.strip()[1:-1] for l in run["sas_plan"].splitlines() if l.startswith("(")]
    if not steps and not ops:
        return
    algorithm = p.get("algorithm", "")
    revision, _, config = algorithm.partition("-")
    task = p["domain"] + "__" + p["problem"][:-len(".pddl")]
    rec = {"archive": archive, "run": run["dir"], "algorithm": config, "revision": revision,
           "label_steps": steps[-1] if steps else None, "operators": ops}
    with open(os.path.join(out, task + ".jsonl"), "a") as f:
        f.write(json.dumps(rec) + "\n")
    counts["plans"] += 1
    counts["label_plans"] += bool(steps)
    counts["operator_plans"] += bool(ops)


def main():
    out, archives = sys.argv[1], sys.argv[2:]
    os.makedirs(out, exist_ok=True)
    summary_path = os.path.join(out, "summary.json")
    summary = json.load(open(summary_path)) if os.path.exists(summary_path) else {}
    wanted = {"static-properties", "run.log", "sas_plan"}
    for path in archives:
        archive = os.path.basename(path).split(".tar")[0]
        if archive in summary:
            continue
        counts = defaultdict(int)
        run = {}
        with tarfile.open(path, "r:*") as tar:
            for m in tar:
                if not m.isfile():
                    continue
                d, name = os.path.split(m.name)
                if name not in wanted:
                    continue
                if run.get("dir") != d:
                    flush(run, out, counts, archive)
                    counts["runs"] += bool(run)
                    run = {"dir": d}
                run[name] = tar.extractfile(m).read().decode(errors="replace")
            flush(run, out, counts, archive)
            counts["runs"] += bool(run)
        summary[archive] = dict(counts)
        json.dump(summary, open(summary_path, "w"), indent=1)
        print(archive, dict(counts), flush=True)


if __name__ == "__main__":
    main()
