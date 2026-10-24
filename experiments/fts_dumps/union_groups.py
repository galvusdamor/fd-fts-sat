#!/usr/bin/env python3
"""label_union candidates: labels that move exactly one factor, grouped by their transitions in
all other factors (their preconditions there, as self-loops). A group of labels with the same
preconditions and pairwise different moving factors can become one label that moves any subset
of the members' factors (it may stay in each). Exact label reduction has already merged labels
of one group that move the same factor (they differ in one factor only).
Usage: union_groups.py DUMPDIR [--csv OUT]"""
import argparse, csv, glob, os, sys
from collections import Counter, defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fts_lib import read_fts


def analyse(path):
    fts = read_fts(path)
    F, L = fts.F, fts.L
    full = [frozenset((x, x) for x in range(fts.size[f])) for f in range(F)]
    sets = [dict() for _ in range(L)]
    for f in range(F):
        for l, tr in fts.trans[f].items():
            s = frozenset(tr)
            if s != full[f]:
                sets[l][f] = s
    groups = defaultdict(list)
    single = 0
    for l in range(L):
        moving = [f for f, s in sets[l].items() if any(a != b for a, b in s)]
        if len(moving) != 1:
            continue
        single += 1
        f = moving[0]
        sig = frozenset((g, s) for g, s in sets[l].items() if g != f)
        groups[sig].append((f, l))
    big = [g for g in groups.values() if len(g) >= 2]
    # within a group, labels moving the same factor would be one label after label reduction
    saved = sum(len(g) - 1 for g in big)
    sizes = Counter(len(g) for g in big)
    return dict(task=os.path.basename(path)[:-7], labels=L, single=single, groups=len(big),
                labels_in_groups=sum(len(g) for g in big), saved=saved,
                largest=max((len(g) for g in big), default=0),
                same_factor_pairs=sum(len(g) - len({f for f, _ in g}) for g in big))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumpdir")
    ap.add_argument("--csv")
    a = ap.parse_args()
    rows = []
    for p in sorted(glob.glob(f"{a.dumpdir}/*.fts.gz")):
        r = analyse(p)
        rows.append(r)
        print(" ".join(f"{k}={v}" for k, v in r.items()), flush=True)
    if a.csv and rows:
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=rows[0].keys())
            w.writeheader()
            w.writerows(rows)


if __name__ == "__main__":
    main()
