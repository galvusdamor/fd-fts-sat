#!/usr/bin/env python3
"""Label structure of FTS dumps: where could labels be compacted or unified?

Per task:
  moved          how many factors a label moves (has a non-self-loop transition in): mean, max
  closure        label_closure's classes (closure_candidates.py): single / elastic / one_rigid /
                 rigid -- only the first three can be closed. elastic: every moving transition
                 (s, l, t) has an l-self-loop at s or t, i.e. l may stay or repeat there
  dominated      moving labels l1 with T_f(l1) a subset of T_f(l2) in every factor f, for some
                 other l2 of no higher cost: any joint transition of l1 is one of l2, so l1 can
                 be dropped (plans map l1 -> l2)
  differ_k       pairs of moving labels that share a moving factor and whose transition sets
                 differ in exactly k factors (k = 1 is what exact label reduction merges; after
                 -shr it should be 0); after merging those k factors they would be one label
Only labels sharing a moving factor can dominate each other or differ in few factors in a
useful way, so pairs are enumerated per moving factor; a factor moved by more than
--max-bucket labels is skipped for the pairs (counted in skipped_buckets).
Rows are printed (and appended to --csv) as soon as a task is done.
Usage: label_structure.py DUMPDIR [--csv OUT] [--max-bucket N]"""
import argparse, csv, glob, os, sys
from collections import Counter, defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fts_lib import read_fts

FIELDS = ["task", "factors", "labels", "moved_mean", "moved_max", "single", "elastic", "one_rigid",
          "rigid", "noop", "dominated", "differ_1", "differ_2", "differ_3", "skipped_buckets"]


def classify(fts, l, moving):
    """label_closure's case for l (as in closure_candidates.py). A moving factor is rigid for l
    if some transition (s, l, t) has an l-self-loop neither at s nor at t."""
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


def analyse(path, max_bucket):
    fts = read_fts(path)
    F, L = fts.F, fts.L
    # per label: factor -> frozenset of transitions, only where it is not a self-loop in every
    # state (absent = self-loop everywhere)
    sets = [dict() for _ in range(L)]
    for f in range(F):
        for l, tr in fts.trans[f].items():
            s = frozenset(tr)
            if s != frozenset((x, x) for x in range(fts.size[f])):
                sets[l][f] = s
    full = [frozenset((x, x) for x in range(fts.size[f])) for f in range(F)]
    get = lambda l, f: sets[l].get(f, full[f])
    moving = [[f for f in sets[l] if any(a != b for a, b in sets[l][f])] for l in range(L)]
    classes = Counter(classify(fts, l, moving[l]) for l in range(L))
    by_factor = defaultdict(list)
    for l in range(L):
        for f in moving[l]:
            by_factor[f].append(l)
    skipped = sum(1 for f, ls in by_factor.items() if len(ls) > max_bucket)
    pairs = set()
    for f, ls in by_factor.items():
        if len(ls) > max_bucket:
            continue
        for i in range(len(ls)):
            for j in range(i + 1, len(ls)):
                pairs.add((ls[i], ls[j]))
    dominated = set()
    differ = Counter()
    for a, b in pairs:
        support = set(sets[a]) | set(sets[b])
        k = sum(get(a, f) != get(b, f) for f in support)
        if 1 <= k <= 3:
            differ[k] += 1
        if fts.cost[b] <= fts.cost[a] and all(get(a, f) <= get(b, f) for f in support) and k > 0:
            dominated.add(a)
        if fts.cost[a] <= fts.cost[b] and all(get(b, f) <= get(a, f) for f in support) and k > 0:
            dominated.add(b)
    mv = [len(m) for m in moving]
    return dict(task=os.path.basename(path)[:-7], factors=F, labels=L,
                moved_mean=f"{sum(mv) / max(1, L):.1f}", moved_max=max(mv, default=0),
                single=classes["single"], elastic=classes["elastic"], one_rigid=classes["one_rigid"],
                rigid=classes["rigid"], noop=classes["noop"], dominated=len(dominated),
                differ_1=differ[1], differ_2=differ[2], differ_3=differ[3], skipped_buckets=skipped)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumpdir")
    ap.add_argument("--csv")
    ap.add_argument("--max-bucket", type=int, default=3000)
    a = ap.parse_args()
    out = None
    if a.csv:
        new = not os.path.exists(a.csv)
        out = open(a.csv, "a", newline="")
        w = csv.DictWriter(out, fieldnames=FIELDS)
        if new:
            w.writeheader()
    for p in sorted(glob.glob(f"{a.dumpdir}/*.fts.gz")):
        r = analyse(p, a.max_bucket)
        print(" ".join(f"{k}={v}" for k, v in r.items()), flush=True)
        if out:
            w.writerow(r)
            out.flush()


if __name__ == "__main__":
    main()
