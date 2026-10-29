#! /usr/bin/env python3

"""
Report only, no runs: scatter plots of formula sizes on the IPC satisficing suite, 2026-10-29.
All runs -shr, no h2, 3500m; the per-step size of an encoding does not depend on the algorithm
(A and C generate the same formula for a length), so the forall data comes from Algorithm C.

  TAV, TAV-Opt  2026-10-28-ipc-fulltrans-slf-fix-A1 (algorithm A, revision 4fd06976f):
                full_transitions_sat without / with use_self_loop_optimisation
  forall        2026-10-27-ipc-algorithm-c-january-fix (Algorithm C, January parameters):
                CMit_chains_slf____rcpol_janfix-shr, label_sat CHAINS_PARALLEL with the label_sat fixes

Sizes are read from the run logs by formula_sizes.py (step formula-sizes), for every run that
generated a formula, solved or not:

  fs_step_clauses, fs_step_variables    one time step (exact: difference of two formulas)
  fs_total_clauses, fs_total_variables  the formula of the largest length generated

Plots:
  per step   TAV vs TAV-Opt and forall vs TAV-Opt, on every task where both have the value
             (unsolved tasks included)
  totals     TAV vs TAV-Opt only, on tasks both solve: both use algorithm A, so the largest
             formula is the one that found the plan. forall's largest formula is at a length of
             Algorithm C's schedule, not comparable.

The step attribute-counts prints, per configuration, how many runs have each attribute.
Replaces 2026-10-28-ipc-size-plots{,-2,-3}.py and 2026-10-29-ipc-size-plots-4.py.
"""

import json
import os
import subprocess
import sys

from lab.experiment import Experiment
from downward.reports.scatter import ScatterPlotReport
from score_report import AbsoluteReport

import common_setup

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
DIR = os.path.dirname(os.path.abspath(__file__))

FIX = "data/2026-10-28-ipc-fulltrans-slf-fix-A1"
FORALL = "data/2026-10-27-ipc-algorithm-c-january-fix"
NAMES = {FIX: {"A_1__fulltrans_transeff_slf_fix-shr": "TAV-Opt",
               "A_1__fulltrans_transeff_noslf_fix-shr": "TAV"},
         FORALL: {"CMit_chains_slf____rcpol_janfix-shr": "forall"}}
STEP = ["fs_step_clauses", "fs_step_variables"]
TOTAL = ["fs_total_clauses", "fs_total_variables"]
SIZES_FILE = os.path.join(DIR, "data", f"{SCRIPT_NAME}-formula-sizes.json")


def source(experiment):
    """the runs directory, or its archive if the compress step has removed it"""
    path = os.path.join(DIR, experiment)
    if os.path.isdir(path):
        return path
    if os.path.exists(path + ".tar.bz2"):
        return path + ".tar.bz2"
    raise SystemExit(f"neither {path} nor {path}.tar.bz2 exists")


def formula_sizes():
    subprocess.run([sys.executable, os.path.join(DIR, "formula_sizes.py"), SIZES_FILE,
                    source(FIX), source(FORALL)], check=True)


_SIZES = {}


def make_filter(names):
    """add the fs_* attributes of the run, rename it; drop the configurations not in names"""
    def f(run):
        if not _SIZES:
            _SIZES.update(json.load(open(SIZES_FILE)))
        for config, short in names.items():
            if run["algorithm"].endswith(config):
                for k, v in _SIZES.get(f"{run['algorithm']}:{run['domain']}:{run['problem']}", {}).items():
                    run[k] = v
                run["algorithm"] = short
                run["id"] = [short] + run["id"][1:]
                return run
        return False
    return f


def _load_properties(eval_dir):
    import lzma
    import tarfile
    from lab import tools
    for name in ["properties", "properties.tar.xz", "properties.xz"]:
        path = os.path.join(eval_dir, name)
        if not os.path.exists(path):
            continue
        try:
            return dict(tools.Properties(filename=path))
        except Exception:
            pass
        if name.endswith(".tar.xz"):
            with tarfile.open(path) as tar:
                member = next(m for m in tar.getmembers() if m.isfile())
                return json.load(tar.extractfile(member))
        opener = lzma.open if name.endswith(".xz") else open
        with opener(path, "rt") as f:
            return json.load(f)
    raise FileNotFoundError(f"no properties file in {eval_dir}")


_KEEP = {}


def both(x, y, attribute, solved):
    """Filter: the runs of x and y on tasks where both have attribute (and, with solved, both
    solved). lab's scatter plot needs both runs of every task it gets."""
    def keep(run):
        key = (x, y, attribute, solved)
        if key not in _KEEP:
            have = {}
            for r in _load_properties(exp.eval_dir).values():
                if r.get(attribute) is None or (solved and r.get("coverage") != 1):
                    continue
                have.setdefault(r["algorithm"], set()).add((r["domain"], r["problem"]))
            _KEEP[key] = have.get(x, set()) & have.get(y, set())
        return (run["domain"], run["problem"]) in _KEEP[key]
    return keep


def attribute_counts():
    count = {}
    for r in _load_properties(exp.eval_dir).values():
        c = count.setdefault(r["algorithm"], {"runs": 0, "solved": 0})
        c["runs"] += 1
        c["solved"] += r.get("coverage") == 1
        for a in STEP + TOTAL:
            if r.get(a) is not None:
                c[a] = c.get(a, 0) + 1
    for alg, c in sorted(count.items()):
        print(alg, " ".join(f"{a}={c.get(a, 0)}" for a in ["runs", "solved"] + STEP + TOTAL))


exp = Experiment()
exp.add_step("formula-sizes", formula_sizes)
exp.add_fetcher(FIX + "-eval", filter=[make_filter(NAMES[FIX])], name="fetch-fix", merge=True)
exp.add_fetcher(FORALL + "-eval", filter=[make_filter(NAMES[FORALL])], name="fetch-forall", merge=True)
exp.add_step("attribute-counts", attribute_counts)
exp.add_report(AbsoluteReport(attributes=common_setup.ATTRIBUTES + STEP + TOTAL,
                              filter_algorithm=["forall", "TAV", "TAV-Opt"]),
               outfile=f"{SCRIPT_NAME}.html")

PLOTS = [(("TAV", "TAV-Opt"), STEP, False), (("forall", "TAV-Opt"), STEP, False),
         (("TAV", "TAV-Opt"), TOTAL, True)]
for (x, y), attributes, solved in PLOTS:
    for attribute in attributes:
        exp.add_report(
            ScatterPlotReport(attributes=[attribute], filter_algorithm=[x, y],
                              filter=[both(x, y, attribute, solved)],
                              get_category=lambda run1, run2: run1["domain"], format="png",
                              scale="log"),
            name=f"{SCRIPT_NAME}-{attribute}-{x}-vs-{y}")

exp.run_steps()
