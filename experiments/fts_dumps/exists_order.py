#!/usr/bin/env python3
"""How much can a label order save in an exists-step encoding of an FTS?

Exists-step with one real move per factor and step, labels executed in a fixed order. For a
factor f going from s to s' (s != s') in a step, the mover a has the transition s -> s'; every
other firing label b runs at s if it comes before a and at s' if after a, as a self-loop:
  before a: b needs a self-loop at s        after a: b needs a self-loop at s'
strict (Rintanen): every firing label is also applicable at s (preconditions at the step start);
lax: no applicability requirement for labels after the mover.

Per move (f, s, s') with movers M (labels with s -> s'), the other labels b relevant in f are:
  kind 1  self-loop at s, none at s'            b must come before every mover   (b -> a)
  kind 2  no self-loop at s, self-loop at s'    b must come after every mover    (a -> b)
          (strict: only if b is applicable at s; otherwise b cannot fire with prev = s at all)
  kind 3  self-loop at neither                  b can only be a mover itself: at most one mover
                                                per factor, independent of the order
Kind 1 alone is the FDR case (Rintanen's disabling graph: an action that reads x=s is disabled by
one that changes x away from s). A label order that respects all kind-1/2 constraints between
two labels makes their chain unnecessary; a chain is needed only for pairs inside one SCC of the
constraint graph (Rintanen's construction, generalised by kind 2).

Reported per task (and variant strict / lax):
  labels, relevant (labels with a constraint), moves (distinct (f, s, s') with a mover),
  pairs1/pairs2   constraint pairs of kind 1 / 2 (mover, b), i.e. what full chains must cover
  chain_full      sum over moves of |M| + |B1| + |B2|: chain entries without any order
  scc_max, scc_nontriv, scc_labels   largest SCC (labels), number of SCCs with >= 2 labels,
                  labels in them -- for the kind-1-only graph (rintanen_*) and kind 1+2 (all_*)
  pairs_left      constraint pairs with both labels in one SCC (kind 1+2 graph)
  chain_left      chain entries restricted to SCC-internal pairs
  amo_moves       moves with >= 2 movers (at-most-one-mover needed there)
The graph uses one hub node per move and kind (B1 -> hub -> M, M -> hub -> B2), so its size is
linear in the input; a label can only reach itself through a hub, which merges nothing.

Usage: exists_order.py VARIANT_DIR [--csv OUT] [--jobs N] [--time-limit S]
"""
import argparse, csv, glob, os, signal, sys
from collections import defaultdict
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fts_lib import read_fts

FIELDS = ["task", "variant", "labels", "relevant", "moves", "pairs1", "pairs2", "chain_full",
          "rintanen_scc_max", "rintanen_scc_nontriv", "rintanen_scc_labels",
          "all_scc_max", "all_scc_nontriv", "all_scc_labels", "pairs_left", "chain_left",
          "amo_moves", "status"]


def sccs(n, adj):
    """Tarjan, iterative. Returns comp[v]."""
    index = [-1] * n; low = [0] * n; on = [False] * n; comp = [-1] * n
    stack = []; counter = 0; c = 0
    for root in range(n):
        if index[root] != -1:
            continue
        work = [(root, 0)]
        index[root] = low[root] = counter; counter += 1; stack.append(root); on[root] = True
        while work:
            v, i = work[-1]
            if i < len(adj[v]):
                work[-1] = (v, i + 1)
                w = adj[v][i]
                if index[w] == -1:
                    index[w] = low[w] = counter; counter += 1; stack.append(w); on[w] = True
                    work.append((w, 0))
                elif on[w]:
                    low[v] = min(low[v], index[w])
            else:
                work.pop()
                if work:
                    u = work[-1][0]; low[u] = min(low[u], low[v])
                if low[v] == index[v]:
                    while True:
                        w = stack.pop(); on[w] = False; comp[w] = c
                        if w == v:
                            break
                    c += 1
    return comp


def scc_stats(L, comp):
    size = defaultdict(int)
    for l in range(L):
        size[comp[l]] += 1
    nontriv = [s for s in size.values() if s >= 2]
    return (max(nontriv) if nontriv else 1), len(nontriv), sum(nontriv)


def analyse(path, variant, strict):
    fts = read_fts(path)
    L = fts.L
    groups = []   # (M, B1, B2)
    relevant = set()
    amo = 0
    for f in range(fts.F):
        sl = defaultdict(set); app = defaultdict(set); movers = defaultdict(set)
        for l, tr in fts.trans[f].items():
            for s, t in tr:
                app[s].add(l)
                if s == t:
                    sl[s].add(l)
                else:
                    movers[(s, t)].add(l)
        for (s, t), M in movers.items():
            B1 = sl[s] - sl[t]
            B2 = sl[t] - sl[s]
            if strict:
                B2 &= app[s]
            if not B1 and not B2 and len(M) < 2:
                continue
            groups.append((M, B1, B2))
            relevant |= M | B1 | B2
            if len(M) >= 2:
                amo += 1
    # graphs: labels 0..L-1, hubs after
    def build(with_kind2):
        adj = [[] for _ in range(L)]
        for M, B1, B2 in groups:
            if B1:
                h = len(adj); adj.append(list(M))
                for b in B1:
                    adj[b].append(h)
            if with_kind2 and B2:
                h = len(adj); adj.append(list(B2))
                for a in M:
                    adj[a].append(h)
        return adj
    adj_r = build(False)
    comp_r = sccs(len(adj_r), adj_r)
    r_max, r_nt, r_lab = scc_stats(L, comp_r)
    adj_a = build(True)
    comp_a = sccs(len(adj_a), adj_a)
    a_max, a_nt, a_lab = scc_stats(L, comp_a)
    pairs1 = pairs2 = chain_full = pairs_left = chain_left = 0
    for M, B1, B2 in groups:
        pairs1 += len(M) * len(B1) - len(M & B1)
        pairs2 += len(M) * len(B2) - len(M & B2)
        chain_full += len(M) + len(B1) + len(B2)
        cm = defaultdict(int); cb = defaultdict(int); cself = defaultdict(int)
        for a in M:
            cm[comp_a[a]] += 1
        for b in B1 | B2:
            cb[comp_a[b]] += 1
        for x in M & (B1 | B2):
            cself[comp_a[x]] += 1
        for c, nm in cm.items():
            nb = cb.get(c, 0)
            p = nm * nb - cself.get(c, 0)
            if p > 0:
                pairs_left += p
                chain_left += nm + nb
    return dict(task=os.path.basename(path)[:-7], variant=variant + ("-strict" if strict else "-lax"),
                labels=L, relevant=len(relevant), moves=len(groups), pairs1=pairs1, pairs2=pairs2,
                chain_full=chain_full, rintanen_scc_max=r_max, rintanen_scc_nontriv=r_nt,
                rintanen_scc_labels=r_lab, all_scc_max=a_max, all_scc_nontriv=a_nt,
                all_scc_labels=a_lab, pairs_left=pairs_left, chain_left=chain_left, amo_moves=amo,
                status="ok")


def job(args):
    path, variant, strict, limit = args
    def alarm(*_):
        raise TimeoutError
    signal.signal(signal.SIGALRM, alarm)
    signal.alarm(limit)
    try:
        r = analyse(path, variant, strict)
    except TimeoutError:
        r = dict(task=os.path.basename(path)[:-7], variant=variant + ("-strict" if strict else "-lax"),
                 status="timeout")
    except MemoryError:
        r = dict(task=os.path.basename(path)[:-7], variant=variant + ("-strict" if strict else "-lax"),
                 status="memory")
    signal.alarm(0)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--csv", required=True)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--time-limit", type=int, default=300)
    a = ap.parse_args()
    jobs = []
    for d in a.dirs:
        variant = os.path.basename(d.rstrip("/"))
        for p in sorted(glob.glob(f"{d}/*.fts.gz")):
            for strict in (True, False):
                jobs.append((p, variant, strict, a.time_limit))
    new = not os.path.exists(a.csv)
    with open(a.csv, "a", newline="") as out:
        w = csv.DictWriter(out, fieldnames=FIELDS)
        if new:
            w.writeheader()
        with Pool(a.jobs) as pool:
            for r in pool.imap_unordered(job, jobs):
                w.writerow(r); out.flush()
                print(r["task"], r["variant"], r["status"], flush=True)


if __name__ == "__main__":
    main()
