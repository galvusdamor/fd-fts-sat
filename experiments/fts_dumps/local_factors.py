#!/usr/bin/env python3
"""Do factors occur that are moved only by "local" labels?

A label l is local to factor i if it moves i and only self-loops in every other factor (where it
is applicable at all; it may have preconditions there). A factor i is locally driven if every
label that moves i is local to i. For such a factor the script reports:
  goal        i has a goal (not every state is a goal state)
  needed_by   labels of other factors with a precondition on i (no-op-free self-loops in i)
  guards      the factors in which i's local labels have preconditions, and their sizes
  tau         how many of i's labels are tau (no precondition anywhere else)
Usage: local_factors.py VARIANT [--csv OUT]   (VARIANT = ntr | shr | shr_tau under fts/)"""
import argparse, csv, glob, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fts_lib import read_fts

FTSDIR = os.environ.get("FTS_DUMPS", "/home/gregor/data/fts-dumps/autoscale-21.11-agile-303")


def analyse(fts):
    movers = [[l for l in fts.trans[f] if fts.moves(f, l)] for f in range(fts.F)]
    moved_by = {}   # label -> factors it moves
    for f in range(fts.F):
        for l in movers[f]:
            moved_by.setdefault(l, []).append(f)
    rows = []
    for i in range(fts.F):
        if not movers[i] or any(len(moved_by[l]) > 1 for l in movers[i]):
            continue
        guards = set()
        tau = 0
        for l in movers[i]:
            g = {j for j in range(fts.F) if j != i and l in fts.trans[j]}
            guards |= g
            tau += not g
        own = set(movers[i])
        needed_by = sum(1 for l in fts.trans[i] if l not in own)
        rows.append(dict(factor=i, states=fts.size[i], labels=len(movers[i]), tau=tau,
                         goal=int(len(fts.goals[i]) < fts.size[i]), needed_by=needed_by,
                         guards=len(guards), guard_states="+".join(str(fts.size[j]) for j in sorted(guards)),
                         product=fts.size[i] * max([1] + [fts.size[j] for j in guards]) if len(guards) <= 1 else ""))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("variant")
    ap.add_argument("--csv")
    a = ap.parse_args()
    out = []
    for path in sorted(glob.glob(f"{FTSDIR}/{a.variant}/*.fts.gz")):
        task = os.path.basename(path)[:-len(".fts.gz")]
        fts = read_fts(path)
        rows = analyse(fts)
        for r in rows:
            r.update(task=task, task_factors=fts.F, task_labels=fts.L)
        out += rows
        print(f"{task}: {fts.F} factors, {len(rows)} locally driven"
              + (": " + ", ".join(f"f{r['factor']}({r['states']}st,{r['labels']}lab,tau{r['tau']},"
                                   f"goal{r['goal']},need{r['needed_by']},guards{r['guards']}:{r['guard_states']})"
                                   for r in rows[:6]) if rows else ""), flush=True)
    if a.csv and out:
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(out[0].keys()))
            w.writeheader(); w.writerows(out)


if __name__ == "__main__":
    main()
