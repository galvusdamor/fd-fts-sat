#!/usr/bin/env python3
"""Summarise rintanen_validate.py output; re-simulate with profile times scaled by f."""
import json, os, statistics as S, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import rintanen_sim as R
from rintanen_validate import CONFIGS

d = sys.argv[1]; T = float(sys.argv[2]) if len(sys.argv) > 2 else 60
# usage: rintanen_validate_report.py OUTDIR [TIME_LIMIT]; SIM_MEMORY=0 disables the memory model
import re
real = json.load(open(f"{d}/real.json"))
# re-parse the profiling logs (clause counts are needed by the memory model)
profiles = {}
for r in real:
    log = f"{d}/{r['instance'].replace('/', '__')}/profile/run.log"
    if r["instance"] not in profiles and os.path.exists(log):
        profiles[r["instance"]] = R.parse_profile(open(log).read())
# base memory: process memory when the first formula generation starts, from any real run
base = {}
for r in real:
    log = f"{d}/{r['instance'].replace('/', '__')}/{r['config']}/run.log"
    m = re.search(r"Step 0 .*formula generation starts with (\d+)MB", open(log).read()) if os.path.exists(log) else None
    if m: base.setdefault(r["instance"], float(m.group(1)))
MEM = float(os.environ.get("RINTANEN_MEMORY", "3000"))
USE_MEM = os.environ.get("SIM_MEMORY", "1") == "1"
print(f"real runs {len(real)}, invalid plans {sum(1 for r in real if r['valid'] is False)}, "
      f"memory pauses {sum(1 for r in real if r['paused'])}")

def scaled(prof, f, fe=None):
    fe = f if fe is None else fe
    return {k: dict(v, solve=v['solve'] * f, encode=v['encode'] * fe) for k, v in prof.items()}

for f in (1.0, 1.1, 1.2, 1.3, 1.5):
    rows = []
    for r in real:
        prof = profiles.get(r["instance"])
        if not prof: continue
        _, strat, kw = CONFIGS[r["config"]]
        mk = dict(memory_limit_mb=MEM, base_mb=base.get(r["instance"], 120.0)) if USE_MEM else {}
        s = R.simulate(scaled(prof, f), strat, time_limit=T, **kw, **mk)
        rows.append((r, s))
    both = [(r, s) for r, s in rows if r["solved"] and s["solved"]]
    agree_solved = sum(1 for r, s in rows if bool(r["solved"]) == bool(s["solved"]) and s["solved"] is not None)
    unknown = sum(1 for r, s in rows if s["solved"] is None)
    same_len = sum(1 for r, s in both if r["length"] == s["length"])
    ratios = [s["time"] / r["time"] for r, s in both if r["time"] > 0.05]
    within = sum(1 for x in ratios if 0.67 <= x <= 1.5)
    print(f"f={f:.1f}: comparable {len(rows)-unknown}/{len(rows)} (unknown {unknown}), solved/unsolved agree {agree_solved}, "
          f"both solved {len(both)}, same solving length {same_len}, "
          f"sim/real time (real>0.05s, n={len(ratios)}): median {S.median(ratios):.2f} geomean {S.geometric_mean(ratios):.2f}, "
          f"within x1.5 {within}; uncertain {sum(1 for r, s in rows if s['uncertain'])}")
    if f == 1.0:
        print("  per config (f=1):")
        for c in CONFIGS:
            cr = [(r, s) for r, s in both if r["config"] == c]
            rr = [s["time"] / r["time"] for r, s in cr if r["time"] > 0.05]
            print(f"    {c:24s} both {len(cr):3d} same length {sum(1 for r, s in cr if r['length']==s['length']):3d} "
                  f"median ratio {S.median(rr) if rr else float('nan'):.2f}  "
                  f"real solved {sum(1 for r in real if r['config']==c and r['solved'])} sim solved {sum(1 for r, s in rows if r['config']==c and s['solved'])}")
        worst = sorted(both, key=lambda x: -abs(__import__('math').log(max(x[1]['time'], 1e-3) / max(x[0]['time'], 1e-3))))[:10]
        print("  largest time mismatches (f=1):")
        for r, s in worst:
            print(f"    {r['instance']:40s} {r['config']:24s} real {r['time']:7.2f}s len {r['length']:3d}  sim {s['time']:7.2f}s len {s['length']}")
        dis = [(r, s) for r, s in rows if s["solved"] is not None and bool(r["solved"]) != bool(s["solved"])]
        print(f"  solved/unsolved disagreements: {[(r['instance'], r['config'], r['solved'], s['solved'], s['reason']) for r, s in dis][:12]}")
