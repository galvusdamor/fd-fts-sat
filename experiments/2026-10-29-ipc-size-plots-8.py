#! /usr/bin/env python3

"""
2026-10-29-ipc-size-plots-8.py: as -7, with TAC (full_transitions_sat with label variables in the
effect clauses, use_labels_in_effects_constraints=true) and TAC-Opt from the same fix run.
Pairs: TAV vs TAV-Opt, forall vs TAV-Opt, TAC vs TAC-Opt, forall vs TAC-Opt, TAV-Opt vs TAC-Opt;
each per step (all tasks) and at the horizon (tasks both solve with algorithm A).

Report only, no runs: formula sizes on the IPC satisficing suite, 2026-10-29. As
2026-10-29-ipc-size-plots-5.py (per-step sizes of all tasks, solved or not), plus the size of the
formula at the horizon -- the first satisfiable length, i.e. what algorithm A solves -- on the
tasks algorithm A solves.

  TAV, TAV-Opt  2026-10-28-ipc-fulltrans-slf-fix-A1 (algorithm A, revision 4fd06976f, -shr, no h2,
                3500m): horizon and sizes from their own logs
  forall        sizes from 2026-10-27-ipc-algorithm-c-january-fix (Algorithm C,
                CMit_chains_slf____rcpol_janfix-shr, -shr, no h2, 3500m); horizon from the eval
                of the algorithm A run 2025-11-07-good-configurations-3 (A_1__chains_slf____rcpol-shr;
                its runs directory no longer exists): time_steps_with_label. Algorithm A tries
                the lengths one by one, so its plan has the minimal horizon h, and at h no step is
                empty (an empty forall step could be dropped, giving a plan of length h - 1), so
                time_steps_with_label = h (checked locally: 53 on airport p01, 3 on childsnack p09).
                The label_sat fixes since then changed variable numbering and memory, not the
                semantics, so the horizon is the same. The formula grows linearly in the length,
                so its size at h is base + (h - 1) * step from the C data. The step consistency
                checks the horizon against the C run: the largest length C proved unsatisfiable
                must be below h, the length at which C found its plan at or above h.

Attributes (formula_sizes.py; every run with a formula, solved or not):
  fs_step_clauses/_variables       one time step
  fs_horizon                       the horizon (algorithm A)
  fs_horizon_clauses/_variables    the whole formula at the horizon

Plots:
  per step     TAV vs TAV-Opt, forall vs TAV-Opt, every task where both have the value
  at horizon   TAV vs TAV-Opt, forall vs TAV-Opt, tasks both solve with algorithm A;
               fs_horizon itself for forall vs TAV-Opt and TAV vs TAV-Opt
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
FORALL_A_EVAL = "data/2025-11-07-good-configurations-3-eval"   # horizons only
FORALL_A_ALGORITHM = "A_1__chains_slf____rcpol-shr"
NAMES = {FIX: {"A_1__fulltrans_transeff_slf_fix-shr": "TAV-Opt",
               "A_1__fulltrans_transeff_noslf_fix-shr": "TAV",
               "A_1__fulltrans_labeleff_slf_fix-shr": "TAC-Opt",
               "A_1__fulltrans_labeleff_noslf_fix-shr": "TAC"},
         FORALL: {"CMit_chains_slf____rcpol_janfix-shr": "forall"}}
STEP = ["fs_step_clauses", "fs_step_variables"]
HORIZON = ["fs_horizon_clauses", "fs_horizon_variables"]
TOTAL = HORIZON + ["fs_horizon"]
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
_FORALL_HORIZON = {}


def forall_horizons():
    """(domain, problem) -> horizon of the forall algorithm A run (time_steps_with_label)"""
    horizons = {}
    for r in _load_properties(os.path.join(DIR, FORALL_A_EVAL)).values():
        if (r["algorithm"].endswith(FORALL_A_ALGORITHM) and r.get("coverage") == 1
                and r.get("time_steps_with_label")):
            horizons[(r["domain"], r["problem"])] = r["time_steps_with_label"]
    return horizons


def consistency():
    """forall horizon (November A) against the January-fix C run: unsat_max < h <= sat_length"""
    sizes = json.load(open(SIZES_FILE))
    horizons = forall_horizons()
    ok = bad = 0
    examples = []
    for key, v in sizes.items():
        alg, dom, prob = key.split(":", 2)
        h = horizons.get((dom, prob))
        if not alg.endswith("CMit_chains_slf____rcpol_janfix-shr") or h is None:
            continue
        if v.get("fs_unsat_max", 0) < h <= v.get("fs_sat_length", 10**9):
            ok += 1
        else:
            bad += 1
            examples.append((dom, prob, h, v.get("fs_unsat_max"), v.get("fs_sat_length")))
    print(f"consistency: {len(horizons)} forall horizons, {ok} consistent with the C run, {bad} not",
          examples[:10])


def make_filter(names):
    """add the fs_* attributes of the run, rename it; drop the configurations not in names"""
    def f(run):
        if not _SIZES:
            _SIZES.update(json.load(open(SIZES_FILE)))
            _FORALL_HORIZON.update(forall_horizons())
        for config, short in names.items():
            if run["algorithm"].endswith(config):
                s = _SIZES.get(f"{run['algorithm']}:{run['domain']}:{run['problem']}", {})
                for k, v in s.items():
                    if k != "formulas":
                        run[k] = v
                # horizon: own algorithm A result, for forall (Algorithm C) the November A run
                h = _FORALL_HORIZON.get((run["domain"], run["problem"])) if short == "forall" else \
                    s.get("fs_sat_length")
                if h is not None and "fs_base_clauses" in s:
                    run["fs_horizon"] = h
                    run["fs_horizon_clauses"] = s["fs_base_clauses"] + (h - 1) * s["fs_step_clauses"]
                    run["fs_horizon_variables"] = s["fs_base_variables"] + (h - 1) * s["fs_step_variables"]
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


class CountingScatterPlotReport(ScatterPlotReport):
    """ScatterPlotReport whose title says on how many tasks the two values are identical (and
    lower for either side), counted on the runs the plot gets after its filters."""
    def __init__(self, x, y, attribute, **kwargs):
        self._x, self._y, self._attribute = x, y, attribute
        super().__init__(attributes=[attribute], filter_algorithm=[x, y], **kwargs)

    def write(self):
        runs = getattr(self, "runs", None) or self.props
        values = {}
        for run in runs.values():
            values.setdefault((run["domain"], run["problem"]), {})[run["algorithm"]] = run.get(self._attribute)
        pairs = [(v.get(self._x), v.get(self._y)) for v in values.values()]
        pairs = [(a, b) for a, b in pairs if a is not None and b is not None]
        identical = sum(a == b for a, b in pairs)
        x_lower = sum(a < b for a, b in pairs)
        y_lower = sum(b < a for a, b in pairs)
        self.title = (f"{self._attribute}: {self._x} lower in {x_lower}, {self._y} lower in {y_lower}, "
                      f"identical in {identical} of {len(pairs)} tasks")
        print(self.title)
        super().write()


exp = Experiment()
exp.add_step("formula-sizes", formula_sizes)
exp.add_fetcher(FIX + "-eval", filter=[make_filter(NAMES[FIX])], name="fetch-fix", merge=True)
exp.add_fetcher(FORALL + "-eval", filter=[make_filter(NAMES[FORALL])], name="fetch-forall", merge=True)
exp.add_step("attribute-counts", attribute_counts)
exp.add_step("consistency", consistency)
exp.add_report(AbsoluteReport(attributes=common_setup.ATTRIBUTES + STEP + TOTAL,
                              filter_algorithm=["forall", "TAV", "TAV-Opt", "TAC", "TAC-Opt"]),
               outfile=f"{SCRIPT_NAME}.html")

PAIRS = [("TAV", "TAV-Opt"), ("forall", "TAV-Opt"), ("TAC", "TAC-Opt"), ("forall", "TAC-Opt"),
         ("TAV-Opt", "TAC-Opt")]
PLOTS = [(pair, attributes, False) for pair in PAIRS for attributes in (STEP, TOTAL)]
for (x, y), attributes, solved in PLOTS:
    for attribute in attributes:
        exp.add_report(
            CountingScatterPlotReport(x, y, attribute,
                              filter=[both(x, y, attribute, solved)],
                              get_category=lambda run1, run2: run1["domain"], format="png",
                              scale="log"),
            name=f"{SCRIPT_NAME}-{attribute}-{x}-vs-{y}")

exp.run_steps()
