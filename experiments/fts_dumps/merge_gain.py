#!/usr/bin/env python3
"""For the factor pairs (a, b) that label pairs differ in (exactly two factors): how many labels
would one merge of a and b followed by exact label reduction save? Labels equal outside {a, b}
become one label (their transitions in the product a x b are united). Prints the best pairs.
Usage: merge_gain.py DUMP [--top 5] [--max-bucket 3000]"""
import argparse, os, sys
from collections import Counter, defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fts_lib import read_fts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--top", type=int, default=5)
    ap.add_argument("--max-bucket", type=int, default=3000)
    a = ap.parse_args()
    fts = read_fts(a.dump)
    F, L = fts.F, fts.L
    full = [frozenset((x, x) for x in range(fts.size[f])) for f in range(F)]
    sets = [dict() for _ in range(L)]
    for f in range(F):
        for l, tr in fts.trans[f].items():
            s = frozenset(tr)
            if s != full[f]:
                sets[l][f] = s
    get = lambda l, f: sets[l].get(f, full[f])
    moving = [[f for f in sets[l] if any(x != y for x, y in sets[l][f])] for l in range(L)]
    by_factor = defaultdict(list)
    for l in range(L):
        for f in moving[l]:
            by_factor[f].append(l)
    pair_count = Counter()
    seen = set()
    for f, ls in by_factor.items():
        if len(ls) > a.max_bucket: continue
        for i in range(len(ls)):
            for j in range(i + 1, len(ls)):
                p = (ls[i], ls[j])
                if p in seen: continue
                seen.add(p)
                diff = [g for g in set(sets[p[0]]) | set(sets[p[1]]) if get(p[0], g) != get(p[1], g)]
                if len(diff) == 2:
                    pair_count[tuple(sorted(diff))] += 1
    # labels saved by merging (x, y): L - number of classes of labels by their sets outside {x, y}
    rows = []
    for (x, y), n in pair_count.most_common(200):
        classes = {frozenset((g, s) for g, s in sets[l].items() if g not in (x, y)) for l in range(L)}
        rows.append((L - len(classes), x, y, n))
    rows.sort(reverse=True)
    print(f"{os.path.basename(a.dump)} factors {F} labels {L} factor_pairs_with_2diff {len(pair_count)}")
    for saved, x, y, n in rows[:a.top]:
        print(f"  merge {x} ({fts.size[x]} states) x {y} ({fts.size[y]} states) = {fts.size[x] * fts.size[y]}: "
              f"{n} label pairs, saves {saved} of {L} labels")


if __name__ == "__main__":
    main()
