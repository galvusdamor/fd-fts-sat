#!/usr/bin/env python3
"""Estimate the profiling cost of a demand file written by rintanen_demand.py."""
import json, os, sys, statistics as S, collections
dem = json.load(open(sys.argv[1])); pdir = sys.argv[2]; jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 4
tot = 0.0; per_inst = {}; nlen = 0; big = 0.0; caps = 0.0; startup = 0.0
by_dom = collections.defaultdict(float)
for name, need in dem["demand"].items():
    d = json.load(open(f"{pdir}/{name}/profile.json"))
    prof = {int(k): v for k, v in d["lengths"].items()}
    ks = [k for k in prof if prof[k]["encode"] > 0]
    gps = sum(prof[k]["encode"] for k in ks) / sum(ks) if ks else 0.01
    r = d["rounds"][0]
    st = sum(r.get(k) or 0 for k in ("translate_time", "transform_time", "init_time"))
    c = st
    for L, t in need.items():
        L = int(L); g = gps * L
        c += g + t; big += g; caps += t; nlen += 1
    startup += st
    per_inst[name] = c; tot += c; by_dom[name.split("__")[0]] += c
s = dem["summary"]
print(f"instances {s['instances']}: no profile {s['no_profile']}, determined {s['determined']}, undetermined {s['undetermined']}")
print(f"demand: {nlen} (instance, length) pairs; estimated CPU {tot/3600:.1f} h = generation {big/3600:.1f} h + solve caps {caps/3600:.1f} h + startup {startup/3600:.1f} h")
print(f"wall at {jobs} jobs: {tot/3600/jobs:.1f} h (upper bound: every call runs to its cap)")
v = sorted(per_inst.values())
if v:
    print(f"per undetermined instance: median {S.median(v):.0f}s, p90 {v[int(.9*len(v))]:.0f}s, max {v[-1]:.0f}s")
print("most expensive domains:", sorted(((round(c/3600, 1), d) for d, c in by_dom.items()), reverse=True)[:8])
