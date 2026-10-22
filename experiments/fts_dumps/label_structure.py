#!/usr/bin/env python3
"""Label structure of FTS dumps: where could labels be compacted or unified?

Per task:
  moved          how many factors a label moves (has a non-self-loop transition in): mean, max
  closure        label_closure's classes (closure_candidates.py): single / elastic / one_rigid /
                 rigid -- only the first three can be closed. elastic: every moving transition
                 (s, l, t) has an l-self-loop at s or t, i.e. l may stay or repeat there
  dominated      labels l1 with T_f(l1) a subset of T_f(l2) in every factor f, for some other
                 l2 of no higher cost: any joint transition of l1 is one of l2, so l1 can be
                 dropped (plans map l1 -> l2)
  differ_k       pairs of labels whose transition sets differ in exactly k factors (k = 1 is
                 what exact label reduction merges; after -shr it should be 0), k = 2, 3
Usage: label_structure.py DUMPDIR [--csv OUT]"""
import argparse, csv, glob, os, sys
from collections import Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fts_lib import read_fts


def classify(fts, l):
    """label_closure's case for l (as in closure_candidates.py): single / elastic / one_rigid /
    rigid, or 'noop' if l moves no factor. A moving factor is rigid for l if some transition
    (s, l, t) has an l-self-loop neither at s nor at t."""
    moving = [f for f in range(fts.F) if fts.moves(f, l)]
    if not moving:
        return "noop"
    rigid = 0
    for f in moving:
        loop = {s for s, t in fts.trans[f][l] if s == t}
        if any(s not in loop and t not in loop for s, t in fts.trans[f][l]):
            rigid += 1
    if len(moving) == 1:
        return "single"
    return "elastic" if rigid == 0 else ("one_rigid" if rigid == 1 else "rigid")


def transitions(fts, f, l):
    """l's transitions in f; a label missing from fts.trans[f] self-loops everywhere"""
    tr = fts.trans[f].get(l)
    return frozenset(tr) if tr is not None else frozenset((s, s) for s in range(fts.size[f]))


def analyse(path):
    fts = read_fts(path)
    F, L = fts.F, fts.L
    sets = [[transitions(fts, f, l) for f in range(F)] for l in range(L)]
    moved = [sum(fts.moves(f, l) for f in range(F)) for l in range(L)]
    classes = Counter(classify(fts, l) for l in range(L))
    dominated = 0
    for l1 in range(L):
        for l2 in range(L):
            if l1 != l2 and fts.cost[l2] <= fts.cost[l1] and all(sets[l1][f] <= sets[l2][f] for f in range(F)) \
                    and (sets[l1] != sets[l2] or l2 < l1):
                dominated += 1
                break
    differ = Counter()
    if L <= 3000:
        for a in range(L):
            for b in range(a + 1, L):
                k = sum(sets[a][f] != sets[b][f] for f in range(F))
                if k <= 3:
                    differ[k] += 1
    return dict(task=os.path.basename(path)[:-7], factors=F, labels=L,
                moved_mean=f"{sum(moved) / max(1, L):.1f}", moved_max=max(moved, default=0),
                single=classes["single"], elastic=classes["elastic"], one_rigid=classes["one_rigid"],
                rigid=classes["rigid"], dominated=dominated,
                differ_1=differ[1], differ_2=differ[2], differ_3=differ[3])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumpdir")
    ap.add_argument("--csv")
    a = ap.parse_args()
    rows = [analyse(p) for p in sorted(glob.glob(f"{a.dumpdir}/*.fts.gz"))]
    if a.csv and rows:
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=rows[0].keys())
            w.writeheader()
            w.writerows(rows)
    for r in rows:
        print(" ".join(f"{k}={v}" for k, v in r.items()))


if __name__ == "__main__":
    main()
