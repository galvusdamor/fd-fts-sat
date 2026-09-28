#!/usr/bin/env python3
"""
Lower-bound coverage of rintanen() schedules vs. algorithm A, assuming every
measurement not taken yet would time out: unprofiled lengths, TIMEOUTs and
FAILED lengths never finish. Algorithm A (one_by_one) runs lengths 1,2,3,...
to completion, so a length it cannot finish blocks it for good.
Both get the same budget of schedule time (startup excluded).
"""
import argparse, collections, glob, json, os, statistics as S, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import rintanen_sim as R
from rintanen_demand import grid, Probe


class Pessimistic(dict):
    """unknown lengths: extrapolated generation time, never finish"""
    def __init__(self, prof):
        super().__init__(prof); self.probe = Probe(prof)
    def get(self, L, default=None):
        if L in self:
            return dict.__getitem__(self, L)
        u = self.probe.get(L)
        return dict(u, status="TIMEOUT", solve=0.0)


def algorithm_a(prof, T):
    t = 0.0
    for k in range(1, 10**6):
        p = prof.get(k)
        if p is None or p["status"] not in ("SAT", "UNSAT"):
            return None                       # blocked: this length never finishes (pessimistic)
        t += p["encode"] + p["solve"]
        if t > T:
            return None
        if p["status"] == "SAT":
            return t
    return None


def job(args_):
    f, T, full, mem = args_
    d = json.load(open(f)); name = os.path.basename(os.path.dirname(f))
    prof = {int(k): v for k, v in d["lengths"].items()}
    if not prof:
        return name, None, None
    a = algorithm_a(prof, T)
    res = {}
    P = Pessimistic(prof)
    for cname, st, kw in grid(not full):
        s = R.simulate(P, st, time_limit=T, memory_limit_mb=mem, **kw)
        res[cname] = s["time"] if s["solved"] else None
    return name, a, res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("profiles_dir"); ap.add_argument("--time-limit", type=float, default=300)
    ap.add_argument("--memory-limit", type=float, default=3000); ap.add_argument("--full-grid", action="store_true")
    ap.add_argument("--processes", type=int, default=1); ap.add_argument("--out", required=True)
    args = ap.parse_args()
    files = sorted(glob.glob(f"{args.profiles_dir}/*/profile.json"))
    from multiprocessing import Pool
    out = {}
    with Pool(args.processes) as pool:
        for name, a, res in pool.imap_unordered(job, [(f, args.time_limit, args.full_grid, args.memory_limit) for f in files], chunksize=8):
            out[name] = dict(A=a, configs=res)
    json.dump(out, open(args.out, "w"))
    report(out, args.time_limit)


def report(out, T):
    prof = {k: v for k, v in out.items() if v["configs"] is not None}
    names = sorted(prof); cfgs = sorted(next(iter(prof.values()))["configs"])
    covA = sum(1 for n in names if prof[n]["A"] is not None)
    cov = {c: sum(1 for n in names if prof[n]["configs"][c] is not None) for c in cfgs}
    best = max(cfgs, key=lambda c: cov[c])
    vbs = sum(1 for n in names if any(prof[n]["configs"][c] is not None for c in cfgs))
    both_a_or = sum(1 for n in names if prof[n]["A"] is not None or prof[n]["configs"][best] is not None)
    print(f"instances with a profile: {len(names)} (of {len(out)}); simulated budget {T:.0f}s of schedule time, pessimistic")
    print(f"algorithm A                  coverage {covA}")
    dflt = [c for c in cfgs if c.startswith('default_p20_i1.0_g1.0')][0]
    print(f"rintanen default ({dflt}) coverage {cov[dflt]}")
    print(f"best single config ({best}) coverage {cov[best]}  (+{cov[best]-covA} vs A; A-or-best {both_a_or})")
    print(f"virtual best over all {len(cfgs)} configs coverage {vbs}")
    top = sorted(cfgs, key=lambda c: -cov[c])[:10]
    print("top 10:", [(c, cov[c]) for c in top])
    only_c = [n for n in names if prof[n]["A"] is None and prof[n]["configs"][best] is not None]
    only_a = [n for n in names if prof[n]["A"] is not None and prof[n]["configs"][best] is None]
    print(f"best config solves {len(only_c)} that A does not, A solves {len(only_a)} that it does not")
    dom = collections.Counter(n.split("__")[0] for n in only_c); print("  gains by domain:", dict(dom.most_common(12)))
    domA = collections.Counter(n.split("__")[0] for n in only_a); print("  losses by domain:", dict(domA.most_common(12)))
    common = [n for n in names if prof[n]["A"] is not None and prof[n]["configs"][best] is not None]
    if common:
        ta = [max(prof[n]["A"], 1e-3) for n in common]; tb = [max(prof[n]["configs"][best], 1e-3) for n in common]
        print(f"on {len(common)} solved by both: sum A {sum(ta):.0f}s vs best {sum(tb):.0f}s; geomean ratio A/best x{S.geometric_mean([a/b for a, b in zip(ta, tb)]):.2f}")
    # coverage by parameter family
    fam = collections.defaultdict(list)
    for c in cfgs:
        fam[c.split("_p")[0]].append(cov[c])
    print("best coverage per length strategy:", sorted(((max(v), k) for k, v in fam.items()), reverse=True)[:10])


if __name__ == "__main__":
    main()
