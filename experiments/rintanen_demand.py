#!/usr/bin/env python3
"""
Which (instance, length) profiles are still needed to simulate every candidate
schedule, and how much solve time each would have to be measured for.

For every profile and every configuration of the grid, simulate. A result is
determined if it neither touches an unprofiled length nor gives a TIMEOUT call
more solve time than it was profiled for. For undetermined ones, re-simulate
in "probe" mode: unprofiled lengths get a generation time extrapolated
linearly from the profiled ones and never finish; every length touched and
the solve time it received are recorded. The needed cap of a length is the
most solve time any configuration gives it -- if the true time is larger,
knowing "larger than that" is enough.
"""
import argparse, glob, json, math, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import rintanen_sim as R


def grid(small=True):
    strats = [("default", R.ByIteration())]
    for s in (1, 2, 3, 5):
        for m in (1.1, 1.2, 1.3, 1.41, 1.6, 2.0):
            strats.append((f"s{s}_m{m}", R.ByIteration(s, m, strictly_increasing=True)))
    for inc in (1, 2):
        strats.append((f"inc{inc}", R.ByIncrement(1, inc)))
    intervals = (0.05, 0.25, 1.0, 5.0) if small else (0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0)
    gammas = (1.0, 0.5) if small else (1.0, 0.8, 0.5)
    out = []
    for name, st in strats:
        out.append((f"{name}_p1", st, dict(interval=1.0, gamma=1.0, max_parallel=1)))
        for p in (2, 4, 8, 20):
            for iv in intervals:
                for g in gammas:
                    out.append((f"{name}_p{p}_i{iv}_g{g}", st, dict(interval=iv, gamma=g, max_parallel=p)))
    return out


class Probe(dict):
    """profile wrapper: unknown lengths get extrapolated generation time, never finish"""
    def __init__(self, prof):
        super().__init__(prof)
        ks = sorted(k for k in prof if prof[k]["encode"] > 0)
        self.g_per_step = (sum(prof[k]["encode"] for k in ks) / sum(ks)) if ks else 0.01
        self.c_per_step = (sum(prof[k].get("clauses", 0) for k in prof) / sum(prof)) if prof else 0
        self.touched = {}
    def get(self, L, default=None):
        if L in self:
            return dict.__getitem__(self, L)
        return dict(status="UNKNOWN", encode=self.g_per_step * L, solve=0.0, clauses=int(self.c_per_step * L))


def probe_sim(prof, strat, time_limit, **kw):
    """simulate treating unknown lengths as never finishing; return {length: solve time received}"""
    pr = Probe(prof)
    received = {}
    # reuse simulate by making unknown calls TIMEOUT with cap 0; track per-length time via a patched profile
    fake = {}
    for L in list(prof):
        fake[L] = prof[L]
    class P(dict):
        def get(self, L, default=None):
            v = fake.get(L)
            if v is not None:
                return v
            u = pr.get(L); u = dict(u, status="TIMEOUT", solve=0.0)
            return u
    res = R.simulate(P(), strat, time_limit=time_limit, received=received, **kw)
    return res, received


def analyse(job):
    f, time_limit, memory_limit, full = job
    G = grid(not full)
    d = json.load(open(f))
    name = os.path.basename(os.path.dirname(f))
    prof = {int(k): v for k, v in d["lengths"].items()}
    if not prof:
        return name, "no_profile", None   # trivial, or failed before the first SAT call
    need = {}
    for cname, st, kw in G:
        s = R.simulate(prof, st, time_limit=time_limit, memory_limit_mb=memory_limit, **kw)
        if s["solved"] is not None and not s["uncertain"]:
            continue
        _, rec = probe_sim(prof, st, time_limit, memory_limit_mb=memory_limit, **kw)
        for L, t in rec.items():
            p = prof.get(L)
            failed_below = any(v.get("status") == "FAILED" and k <= L for k, v in prof.items())
            known = failed_below or (p is not None and (p["status"] in ("SAT", "UNSAT") or p["solve"] >= t))
            if not known:
                need[L] = max(need.get(L, 0.0), t)
    return name, ("undetermined" if need else "determined"), need


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("profiles_dir")
    ap.add_argument("--time-limit", type=float, default=300)
    ap.add_argument("--memory-limit", type=float, default=3000)
    ap.add_argument("--out", required=True)
    ap.add_argument("--full-grid", action="store_true")
    ap.add_argument("--processes", type=int, default=1,
                    help="keep 1 while profiling runs on the same machine")
    args = ap.parse_args()
    files = sorted(glob.glob(f"{args.profiles_dir}/*/profile.json"))
    t0 = time.time()
    summary = dict(instances=0, no_profile=0, determined=0, undetermined=0,
                   configs=len(grid(not args.full_grid)), time_limit=args.time_limit)
    demand = {}
    from multiprocessing import Pool
    with Pool(args.processes) as pool:
        for name, kind, need in pool.imap_unordered(
                analyse, [(f, args.time_limit, args.memory_limit, args.full_grid) for f in files], chunksize=4):
            summary["instances"] += 1
            summary[kind] += 1
            if need:
                demand[name] = {str(L): round(t, 3) for L, t in sorted(need.items())}
    summary["seconds"] = round(time.time() - t0, 1)
    json.dump(dict(summary=summary, demand=demand), open(args.out, "w"), indent=1)
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
