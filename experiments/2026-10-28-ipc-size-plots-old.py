#! /usr/bin/env python3

"""
Report only, no runs: the scatter plots of 2026-10-28-ipc-size-plots.py on existing data, until
the self-loop fix run (2026-10-28-ipc-fulltrans-slf-fix-A1) is fetched.

  TAV vs TAV-Opt     2026-07-25-ipc-A1-all-transitions-encodings (revision 5a714924c, -shr,
                     no h2, 3500m, algorithm A): full_transitions_sat without and with
                     use_self_loop_optimisation. TAV-Opt is the version *before* the self-loop fix
                     (4fd06976f), which adds a few clauses per chain only. The run predates the
                     ENCSTAT line, so only solved_sat_variables / solved_sat_clauses (the formula
                     of the satisfiable call, i.e. totals) are compared.
  forall vs TAV-Opt  2026-09-24-ipc-bdd-A1-orders-eval, the reference configurations of the
                     2026-09-21 run (revision f27d84585, -shr, h2 preprocessing, algorithm A):
                     label_sat CHAINS_PARALLEL against full_transitions_sat with the self-loop
                     optimisation. Both have ENCSTAT, so per-step and total sizes are compared.

Only solved tasks are plotted.
"""

import os

from lab.experiment import Experiment
from downward.reports.scatter import ScatterPlotReport
from score_report import AbsoluteReport

import common_setup

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]

JULY_EVAL = "data/2026-07-25-ipc-A1-all-transitions-encodings-eval"
JULY_NAMES = {"A_1__fulltransitions_slf_transeff-shr": "TAV-Opt (jul)",
              "A_1__fulltransitions_____transeff-shr": "TAV (jul)"}

SEP_EVAL = "data/2026-09-24-ipc-bdd-A1-orders-eval"
SEP_NAMES = {"A_1__fulltransitions_slf_transeff-shr": "TAV-Opt (sep)",
             "A_1__chains_slf____rcpol-shr": "forall (sep)"}


def make_rename(names):
    """<revision>-<configuration> or <configuration> -> short name; drop everything else."""
    def rename(run):
        for config, short in names.items():
            if run["algorithm"].endswith(config):
                run["algorithm"] = short
                run["id"] = [short] + run["id"][1:]
                return run
        return False
    return rename


def solved_only(run):
    return run if run.get("coverage") == 1 else False


ENC_ATTRIBUTES = ["enc_step_variables_last", "enc_step_clauses_last", "enc_total_variables",
                  "enc_total_clauses"]
SOLVED_ATTRIBUTES = ["solved_sat_variables", "solved_sat_clauses"]

exp = Experiment()
exp.add_fetcher(JULY_EVAL, filter=[make_rename(JULY_NAMES)], name="fetch-july", merge=True)
exp.add_fetcher(SEP_EVAL, filter=[make_rename(SEP_NAMES)], name="fetch-sep", merge=True)

exp.add_report(AbsoluteReport(attributes=common_setup.ATTRIBUTES + ENC_ATTRIBUTES
                              + SOLVED_ATTRIBUTES,
                              filter_algorithm=["TAV (jul)", "TAV-Opt (jul)", "forall (sep)",
                                                "TAV-Opt (sep)"]),
               outfile=f"{SCRIPT_NAME}.html")

PLOTS = [
    (("TAV (jul)", "TAV-Opt (jul)"), SOLVED_ATTRIBUTES),
    (("forall (sep)", "TAV-Opt (sep)"), ENC_ATTRIBUTES + SOLVED_ATTRIBUTES),
]
for (x, y), attributes in PLOTS:
    for attribute in attributes:
        short = f"{x.split()[0]}-vs-{y.split()[0]}"
        exp.add_report(
            ScatterPlotReport(attributes=[attribute], filter_algorithm=[x, y], filter=[solved_only],
                              get_category=lambda run1, run2: run1["domain"], format="png",
                              scale="log"),
            name=f"{SCRIPT_NAME}-{attribute}-{short}")

exp.run_steps()
