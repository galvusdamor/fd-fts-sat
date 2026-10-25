#!/usr/bin/env python3
"""label_union with labels that move several factors: a label is eligible if in all but at
most one of its moving factors it has a self-loop in every state (there it changes the factor
only optionally, so every pattern of moving/staying is a real firing). Groups as in
label_union: same cost and same transitions outside the label's moving factors; members need
pairwise disjoint moving sets (then neither has a precondition in the other's factors).
Counts: multi = labels moving >= 2 factors, eligible_multi, and labels saved by groups of
(a) single-factor labels only (today's label_union) and (b) single + eligible multi labels;
disjoint members are picked greedily, largest moving sets last.
Usage: union_multi.py DUMPDIR [--csv OUT]"""
import argparse, csv, glob, os, sys
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fts_lib import read_fts


def analyse(path):
    fts = read_fts(path)
    F, L = fts.F, fts.L
    full = [frozenset((x, x) for x in range(fts.size[f])) for f in range(F)]
    multi = eligible = 0
    groups_single, groups_all = defaultdict(list), defaultdict(list)
    for l in range(L):
        sets = {}
        for f in range(F):
            tr = fts.trans[f].get(l)
            if tr is None: continue
            s = frozenset(tr)
            if s != full[f]: sets[f] = s
        moving = [f for f, s in sets.items() if any(a != b for a, b in s)]
        if not moving: continue
        if len(moving) >= 2:
            multi += 1
            rigid = [f for f in moving if not full[f] <= sets[f]]   # no self-loop in some state
            if len(rigid) > 1: continue
            eligible += 1
        key = (fts.cost[l], frozenset((g, s) for g, s in sets.items() if g not in moving))
        groups_all[key].append((len(moving), l, frozenset(moving)))
        if len(moving) == 1:
            groups_single[key].append((1, l, frozenset(moving)))

    def saved(groups):
        total = 0
        for members in groups.values():
            members.sort(key=lambda m: m[0])
            used, picked = set(), 0
            for _, l, mv in members:
                if used & mv: continue
                used |= mv; picked += 1
            if picked >= 2: total += picked - 1
        return total
    return dict(task=os.path.basename(path)[:-7], labels=L, multi=multi, eligible_multi=eligible,
                saved_single=saved(groups_single), saved_with_multi=saved(groups_all))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumpdir")
    ap.add_argument("--csv")
    a = ap.parse_args()
    out = None
    for p in sorted(glob.glob(f"{a.dumpdir}/*.fts.gz")):
        r = analyse(p)
        if a.csv:
            if out is None:
                out = open(a.csv, "w", newline=""); w = csv.DictWriter(out, fieldnames=r.keys()); w.writeheader()
            w.writerow(r); out.flush()


if __name__ == "__main__":
    main()
