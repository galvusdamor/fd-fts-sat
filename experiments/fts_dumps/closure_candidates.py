#!/usr/bin/env python3
"""How many labels can label_closure close, and in which case (see
task_transformation_label_closure.h)?

  single     l moves one factor                              -> full closure
  elastic    l moves several factors, all transitions elastic -> elastic closure everywhere
  one_rigid  exactly one moving factor has a non-elastic one  -> full closure in that factor
  rigid      two or more such factors                         -> left alone
A transition (s,t) of l is elastic if s or t has an l-self-loop. For every case the number of
labels whose closure actually adds transitions, and how many.
Usage: closure_candidates.py VARIANT [--csv OUT]"""
import argparse, csv, glob, os, sys
from collections import Counter, deque
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fts_lib import read_fts

FTSDIR = os.environ.get("FTS_DUMPS", "/home/gregor/data/fts-dumps/autoscale-21.11-agile-303")


def closure_size(n, trans, elastic_only):
    succ = [[] for _ in range(n)]
    loop = [False] * n
    for s, t in trans:
        if s == t: loop[s] = True
        else: succ[s].append(t)
    pairs = set()
    for s in range(n):
        seen = set()
        q = deque()
        for t in succ[s]:
            node = (t, loop[s] or loop[t])
            if node not in seen: seen.add(node); q.append(node)
        while q:
            x, flag = q.popleft()
            if x != s and (flag or not elastic_only): pairs.add((s, x))
            for y in succ[x]:
                node = (y, flag or loop[y])
                if node not in seen: seen.add(node); q.append(node)
    pairs |= set(trans)
    return len(pairs), loop


def analyse(fts):
    labels = {}
    for f in range(fts.F):
        for l, tr in fts.trans[f].items():
            if any(s != t for s, t in tr):
                labels.setdefault(l, []).append(f)
    cases, closed, added = Counter(), Counter(), Counter()
    for l, moving in labels.items():
        loops = {}
        rigid = []
        for f in moving:
            loop = [False] * fts.size[f]
            for s, t in fts.trans[f][l]:
                if s == t: loop[s] = True
            loops[f] = loop
            if any(not loop[s] and not loop[t] for s, t in fts.trans[f][l]): rigid.append(f)
        if len(moving) == 1: case, todo = "single", [(moving[0], False)]
        elif not rigid: case, todo = "elastic", [(f, True) for f in moving]
        elif len(rigid) == 1: case, todo = "one_rigid", [(rigid[0], False)]
        else: case, todo = "rigid", []
        cases[case] += 1
        extra = 0
        for f, elastic_only in todo:
            size, _ = closure_size(fts.size[f], fts.trans[f][l], elastic_only)
            extra += size - len(set(fts.trans[f][l]))
        if extra:
            closed[case] += 1
            added[case] += extra
    return cases, closed, added


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("variant")
    ap.add_argument("--csv")
    a = ap.parse_args()
    out = []
    total = Counter()
    for path in sorted(glob.glob(f"{FTSDIR}/{a.variant}/*.fts.gz")):
        task = os.path.basename(path)[:-len(".fts.gz")]
        cases, closed, added = analyse(read_fts(path))
        row = {"task": task}
        for c in ["single", "elastic", "one_rigid", "rigid"]:
            row[c] = cases[c]
            row[c + "_closed"] = closed[c]
            row[c + "_added"] = added[c]
            total[c + "_closed"] += closed[c] > 0
        out.append(row)
        print(task, " ".join(f"{c}={cases[c]}/{closed[c]}(+{added[c]})" for c in ["single", "elastic", "one_rigid", "rigid"]),
              flush=True)
    print("TASKS WITH CLOSABLE LABELS:", dict(total))
    if a.csv and out:
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(out[0].keys()))
            w.writeheader(); w.writerows(out)


if __name__ == "__main__":
    main()
