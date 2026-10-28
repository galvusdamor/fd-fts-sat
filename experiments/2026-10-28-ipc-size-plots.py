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

Only solved tasks are plotted (on unsolved ones "last" is the last length tried, which differs
between configurations).

FORALL_EVAL / FORALL_ALGORITHM name the forall run; check that this is the run with 1196 solved
before using the plots (the coverage table in the report shows it).
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


def solved_only(run):
    return run if run.get("coverage") == 1 else False


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
            ScatterPlotReport(attributes=[attribute], filter_algorithm=[x, y], filter=[solved_only],
                              get_category=lambda run1, run2: run1["domain"], format="png",
                              scale="log"),
            name=f"{SCRIPT_NAME}-{attribute}-{x}-vs-{y}")

exp.run_steps()
