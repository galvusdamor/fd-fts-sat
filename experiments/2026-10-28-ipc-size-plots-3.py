#! /usr/bin/env python3

"""
Report only, no runs: scatter plots of formula sizes on the IPC satisficing suite, 2026-10-28.

  TAV vs TAV-Opt   full_transitions_sat (transition variables in the effect clauses) without and
                   with use_self_loop_optimisation, from 2026-10-28-ipc-fulltrans-slf-fix-A1
                   (revision 4fd06976f, -shr, no h2, 3500m, algorithm A).
                   Attributes: enc_step_variables_last, enc_step_clauses_last,
                   enc_total_variables, enc_total_clauses (ENCSTAT, the last SAT call: on solved
                   tasks the one that found the plan).
  forall vs TAV-Opt  label_sat CHAINS_PARALLEL (algorithm A, the run with 1196 solved) against
                   TAV-Opt. That run predates the ENCSTAT line (2026-09-21), so it has no per-step
                   sizes; compared are solved_sat_variables / solved_sat_clauses (fts_parser: the
                   formula of the satisfiable call, i.e. the totals). TAV-Opt has them too.

Only tasks that both configurations of a plot solve are plotted. lab's scatter plot needs both runs
of every task it gets and some tasks have a run of only one configuration, so the filter reads the
eval dir's properties once and drops every task not solved by both (2026-10-28-ipc-size-plots.py
and -2 aborted on agricola p11).
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

# label_sat forall-step, algorithm A, -shr, 3500m, no h2 (adjust if the 1196 run is elsewhere)
FORALL_EVAL = "data/2025-11-07-good-configurations-3-eval"
FORALL_ALGORITHM = "A_1__chains_slf____rcpol-shr"

NAMES = {TAV_OPT: "TAV-Opt", TAV: "TAV", FORALL_ALGORITHM: "forall"}


def rename(run):
    """<revision>-<configuration> or <configuration> -> short name; drop everything else."""
    for config, short in NAMES.items():
        if run["algorithm"].endswith(config):
            run["algorithm"] = short
            return run
    return False


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

PLOTS = [
    (("TAV", "TAV-Opt"), ["enc_step_variables_last", "enc_step_clauses_last",
                          "enc_total_variables", "enc_total_clauses", "solved_sat_variables",
                          "solved_sat_clauses"]),
    (("forall", "TAV-Opt"), ["solved_sat_variables", "solved_sat_clauses"]),
]
for (x, y), attributes in PLOTS:
    for attribute in attributes:
        exp.add_report(
            ScatterPlotReport(attributes=[attribute], filter_algorithm=[x, y], filter=[solved_by_both(x, y)],
                              get_category=lambda run1, run2: run1["domain"], format="png",
                              scale="log"),
            name=f"{SCRIPT_NAME}-{attribute}-{x}-vs-{y}")

exp.run_steps()
