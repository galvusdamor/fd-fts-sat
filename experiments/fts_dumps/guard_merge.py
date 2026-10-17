#!/usr/bin/env python3
"""Local-guard merging, estimated on the dumps.

A factor i is locally driven if every label that moves i moves only i. Its guards are the
other factors in which those labels have preconditions. If i has exactly one guard g,
merging i into g turns all of i's labels into tau labels of the product (they move only the
product and self-loop everywhere else), so tau_baking can remove them. Local factors with
the same guard stack up: after merging one, the others are guarded by the product, so a
guard g and its dependents D(g) give the product |g| * prod |i|.

For every size limit, dependents are merged into their guard greedily, smallest first, as
long as the product stays within the limit. Reported per task: the largest full group,
how many labels become tau, how many states and transitions the merged factors have.
Usage: guard_merge.py VARIANT [--limits 100,1000,10000] [--csv OUT]"""
import argparse, csv, glob, os, sys
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fts_lib import read_fts

FTSDIR = os.environ.get("FTS_DUMPS", "/home/gregor/data/fts-dumps/autoscale-21.11-agile-303")


def local_factors(fts):
    """factor -> (guard factor, labels moving it) for locally driven factors with one guard"""
    moving = defaultdict(list)
    for f in range(fts.F):
        for l, tr in fts.trans[f].items():
            if any(s != t for s, t in tr):
                moving[l].append(f)
    movers = defaultdict(list)
    for l, fs in moving.items():
        if len(fs) == 1:
            movers[fs[0]].append(l)
    result = {}
    for i in range(fts.F):
        own = [l for l, fs in moving.items() if i in fs]
        if not own or any(len(moving[l]) > 1 for l in own):
            continue
        guards = {j for l in own for j in range(fts.F) if j != i and l in fts.trans[j]}
        if len(guards) == 1:
            result[i] = (guards.pop(), own)
    return result


def analyse(fts, limits):
    loc = local_factors(fts)
    dependents = defaultdict(list)
    for i, (g, own) in loc.items():
        dependents[g].append(i)
    out = {"local_single_guard": len(loc), "guards": len(dependents),
           "tau_labels_all": sum(len(own) for _, own in loc.values()), "labels": fts.L}
    full = 0
    for g, deps in dependents.items():
        size = fts.size[g]
        for i in deps:
            size *= fts.size[i]
        full = max(full, size)
    out["largest_full_group"] = full
    for lim in limits:
        merged = tau = states = trans = 0
        for g, deps in dependents.items():
            size = fts.size[g]
            take = []
            for i in sorted(deps, key=lambda i: fts.size[i]):
                if size * fts.size[i] <= lim:
                    size *= fts.size[i]
                    take.append(i)
            if not take:
                continue
            merged += len(take)
            tau += sum(len(loc[i][1]) for i in take)
            states += size
            # transitions of the product: every label of g or of a merged factor, as a product
            labels = set(fts.trans[g])
            for i in take:
                labels |= set(fts.trans[i])
            for l in labels:
                n = 1
                for f in [g] + take:
                    n *= len(fts.trans[f][l]) if l in fts.trans[f] else fts.size[f]
                trans += n
        out[f"merged@{lim}"] = merged
        out[f"tau@{lim}"] = tau
        out[f"states@{lim}"] = states
        out[f"transitions@{lim}"] = trans
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("variant")
    ap.add_argument("--limits", default="100,1000,10000")
    ap.add_argument("--csv")
    a = ap.parse_args()
    limits = [int(x) for x in a.limits.split(",")]
    rows = []
    for path in sorted(glob.glob(f"{FTSDIR}/{a.variant}/*.fts.gz")):
        task = os.path.basename(path)[:-len(".fts.gz")]
        fts = read_fts(path)
        r = analyse(fts, limits)
        r["task"] = task
        rows.append(r)
        if r["local_single_guard"]:
            print(task, {k: v for k, v in r.items() if k != "task"}, flush=True)
    if a.csv and rows:
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=["task"] + [k for k in rows[0] if k != "task"])
            w.writeheader(); w.writerows(rows)


if __name__ == "__main__":
    main()
