#!/usr/bin/env python3
"""How far would an order-based label_union reach? Pairs of labels l_i, l_j that each move one,
different factor (f_i != f_j) and have the same cost and the same transitions in every factor
other than f_i and f_j:
  exact      neither has a precondition in the other's factor (label_union covers them)
  order_full not exact, but in one order (l_a first, then l_b) every move fits: every move of
             l_a in f_a ends in l_b's precondition there, every move of l_b in f_b starts in
             l_a's precondition there
  order_part some, not all, moves fit in the better order
  enabling   l_a has a move s -> t in f_a with s outside and t inside l_b's precondition
             (l_a enables l_b: a chain the order could put into one firing)
A precondition of l in factor g = the states with an l-self-loop, if that is not every state.
Usage: union_order_pairs.py DUMPDIR [--csv OUT] [--max-bucket N]"""
import argparse, csv, glob, os, sys
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fts_lib import read_fts


def analyse(path, max_bucket):
    fts = read_fts(path)
    F, L = fts.F, fts.L
    full = [frozenset((x, x) for x in range(fts.size[f])) for f in range(F)]
    info = {}   # label -> (own factor, moves, items {g: transitions} for g != own, non-identity)
    for l in range(L):
        own, n = -1, 0
        for f in range(F):
            tr = fts.trans[f].get(l)
            if tr is not None and any(a != b for a, b in tr):
                own = f; n += 1
                if n > 1: break
        if n != 1: continue
        items = {}
        for f in range(F):
            if f == own: continue
            tr = fts.trans[f].get(l)
            if tr is not None:
                s = frozenset(tr)
                if s != full[f]: items[f] = s
        moves = [(a, b) for a, b in fts.trans[own][l] if a != b]
        info[l] = (own, moves, items)
    # bucket entries: (cost, transitions outside own factor minus at most one 'partner' factor)
    buckets = defaultdict(list)
    for l, (own, moves, items) in info.items():
        base = frozenset(items.items())
        buckets[(fts.cost[l], base)].append((l, None))
        for g in items:
            buckets[(fts.cost[l], frozenset((k, v) for k, v in items.items() if k != g))].append((l, g))
    seen = set()
    counts = dict(exact=0, order_full=0, order_part=0, enabling=0, skipped_buckets=0)
    for key, entries in buckets.items():
        if len(entries) > max_bucket:
            counts["skipped_buckets"] += 1
            continue
        for x in range(len(entries)):
            li, ri = entries[x]
            fi, mi, ii = info[li]
            for y in range(x + 1, len(entries)):
                lj, rj = entries[y]
                fj, mj, ij = info[lj]
                if fi == fj or li == lj: continue
                # the removed key must be exactly the partner's factor (or nothing to remove)
                if not ((ri == fj) or (ri is None and fj not in ii)): continue
                if not ((rj == fi) or (rj is None and fi not in ij)): continue
                pair = (min(li, lj), max(li, lj))
                if pair in seen: continue
                seen.add(pair)
                pi = {a for a, b in ii[fj]} if fj in ii else None   # l_i's precondition in f_j
                pj = {a for a, b in ij[fi]} if fi in ij else None   # l_j's precondition in f_i
                if pi is None and pj is None:
                    counts["exact"] += 1
                    continue
                best, total, enable = 0, 0, False
                for (la, fa, ma, pre_b_in_fa, lb, fb, mb, pre_a_in_fb) in (
                        (li, fi, mi, pj, lj, fj, mj, pi), (lj, fj, mj, pi, li, fi, mi, pj)):
                    # l_a first: its moves in f_a must end in l_b's precondition there (pre_b_in_fa);
                    # l_b's moves in f_b must start in l_a's precondition there (pre_a_in_fb)
                    ok = sum(1 for s, t in ma if pre_b_in_fa is None or t in pre_b_in_fa) + \
                         sum(1 for s, t in mb if pre_a_in_fb is None or s in pre_a_in_fb)
                    best = max(best, ok); total = len(ma) + len(mb)
                    if pre_b_in_fa is not None and any(s not in pre_b_in_fa and t in pre_b_in_fa for s, t in ma):
                        enable = True
                if best == total: counts["order_full"] += 1
                elif best > 0: counts["order_part"] += 1
                if enable: counts["enabling"] += 1
    return dict(task=os.path.basename(path)[:-7], labels=L, single=len(info), **counts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumpdir")
    ap.add_argument("--csv")
    ap.add_argument("--max-bucket", type=int, default=3000)
    a = ap.parse_args()
    out = None
    for p in sorted(glob.glob(f"{a.dumpdir}/*.fts.gz")):
        r = analyse(p, a.max_bucket)
        print(" ".join(f"{k}={v}" for k, v in r.items()), flush=True)
        if a.csv:
            if out is None:
                out = open(a.csv, "w", newline=""); w = csv.DictWriter(out, fieldnames=r.keys()); w.writeheader()
            w.writerow(r); out.flush()


if __name__ == "__main__":
    main()
