#! /usr/bin/env python3

"""
Report only, no runs: scatter plots of formula sizes on the IPC satisficing suite, 2026-10-29.
All runs: algorithm A, -shr, no h2, 3500m, revision 4fd06976f.

  TAV vs TAV-Opt     2026-10-28-ipc-fulltrans-slf-fix-A1 (full_transitions_sat without and with
                     use_self_loop_optimisation)
  forall vs TAV-Opt  2026-10-29-ipc-forall-A1 (label_sat CHAINS_PARALLEL) against TAV-Opt

Attributes: enc_step_variables_last, enc_step_clauses_last (one time step of the last SAT call)
and enc_total_variables, enc_total_clauses (the whole formula of the last SAT call), from the
ENCSTAT line; on solved tasks the last call is the one that found the plan. Only tasks both
configurations of a plot solve are plotted.

Replaces 2026-10-28-ipc-size-plots{,-2,-3}.py, whose forall run (2025-11-07) predates ENCSTAT.
The step attribute-counts prints, per configuration, on how many solved runs each size attribute
is present (a missing attribute is plotted at the edge of the axis by lab).
"""

import os

from lab.experiment import Experiment
from downward.reports.scatter import ScatterPlotReport
from score_report import AbsoluteReport

import common_setup

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]

FIX_EVAL = "data/2026-10-28-ipc-fulltrans-slf-fix-A1-eval"
FIX_REVISION = "4fd06976f404c9538b8cae28d0991421db549226"
TAV_OPT = "A_1__fulltrans_transeff_slf_fix-shr"
TAV = "A_1__fulltrans_transeff_noslf_fix-shr"

FORALL_EVAL = "data/2026-10-29-ipc-forall-A1-eval"
FORALL_ALGORITHM = "A_1__chains_slf____rcpol_fix-shr"

NAMES = {TAV_OPT: "TAV-Opt", TAV: "TAV", FORALL_ALGORITHM: "forall"}


def rename(run):
    """<revision>-<configuration> or <configuration> -> short name; drop everything else."""
    for config, short in NAMES.items():
        if run["algorithm"].endswith(config):
            run["algorithm"] = short
            return run
    return False


SIZE_ATTRIBUTES = ["enc_step_variables_last", "enc_step_clauses_last", "enc_total_variables",
                   "enc_total_clauses", "solved_sat_variables", "solved_sat_clauses"]


def _load_properties(eval_dir):
    import json
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


_SOLVED = {}


def solved_by_both(x, y):
    """Filter: keep the runs of x and y on tasks both solve."""
    def keep(run):
        if (x, y) not in _SOLVED:
            props = _load_properties(exp.eval_dir)
            solved = {}
            for r in props.values():
                if r.get("coverage") == 1:
                    solved.setdefault(r["algorithm"], set()).add((r["domain"], r["problem"]))
            _SOLVED[(x, y)] = solved.get(x, set()) & solved.get(y, set())
        return (run["domain"], run["problem"]) in _SOLVED[(x, y)]
    return keep


exp = Experiment()
exp.add_fetcher(FIX_EVAL, filter=[rename], name="fetch-fix", merge=True)
exp.add_fetcher(FORALL_EVAL, filter=[rename], name="fetch-forall", merge=True)

exp.add_report(AbsoluteReport(attributes=common_setup.ATTRIBUTES + [
                   "enc_step_variables_last", "enc_step_clauses_last", "enc_total_variables",
                   "enc_total_clauses", "solved_sat_variables", "solved_sat_clauses"],
                   filter_algorithm=["forall", "TAV", "TAV-Opt"]),
               outfile=f"{SCRIPT_NAME}.html")

ENC = ["enc_step_variables_last", "enc_step_clauses_last", "enc_total_variables", "enc_total_clauses"]
PLOTS = [
    (("TAV", "TAV-Opt"), ENC),
    (("forall", "TAV-Opt"), ENC),
]


def attribute_counts():
    props = _load_properties(exp.eval_dir)
    count = {}
    for r in props.values():
        if r.get("coverage") != 1:
            continue
        c = count.setdefault(r["algorithm"], {"solved": 0})
        c["solved"] += 1
        for a in SIZE_ATTRIBUTES:
            if r.get(a) is not None:
                c[a] = c.get(a, 0) + 1
    for alg, c in sorted(count.items()):
        print(alg, " ".join(f"{a}={c.get(a, 0)}" for a in ["solved"] + SIZE_ATTRIBUTES))


exp.add_step("attribute-counts", attribute_counts)
for (x, y), attributes in PLOTS:
    for attribute in attributes:
        exp.add_report(
            ScatterPlotReport(attributes=[attribute], filter_algorithm=[x, y], filter=[solved_by_both(x, y)],
                              get_category=lambda run1, run2: run1["domain"], format="png",
                              scale="log"),
            name=f"{SCRIPT_NAME}-{attribute}-{x}-vs-{y}")

exp.run_steps()
