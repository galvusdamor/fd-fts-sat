#! /usr/bin/env python3

import itertools
import math
import os
from pathlib import Path
import subprocess

from lab.environments import TetralithEnvironment, LocalEnvironment
from lab.reports import Attribute, geometric_mean, arithmetic_mean

from downward.reports.absolute import AbsoluteReport
from downward.reports.compare import ComparativeReport
from downward.reports.scatter import ScatterPlotReport

import common_setup
from common_setup import IssueConfig, IssueExperiment

import filters

DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]

SUITE = common_setup.DEFAULT_SATISFICING_SUITE

exp = IssueExperiment()

exp.add_suite(BENCHMARKS_DIR, SUITE)

tofetch = [
        ("2025-06-18-thrid-debugging-run-bdd", ["bdd-impl-comb-nocut", "bdd-impl-comb-cut", "bdd-biim-nocomb-nocut"]),
        ("2025-06-27-bdd-omit-vars", ["bdd-impl-nocomb-nocut-omit-1", "bdd-impl-nocomb-nocut-omit-2", "bdd-impl-nocomb-nocut-omit-1000000"]),
        ("2025-07-03-time-slicing", ["R^2E noloop-C","bdd-impl-nocomb-nocut-omit-1-C"]),
        ]

tofetch = [
        ("2025-06-18-thrid-debugging-run", ["ff-trans"]),
        ("2025-06-13-second-debugging-run", ["ff-pure"]),
        ("2025-06-23-fourth-debugging-run", ["seq", "R^2E"]),
        ("2025-07-03-r2e-noloop-debugging", ["R^2E noloop"]),
        ("2025-07-09-bdd-other-orders", ["bdd-impl-nocomb-nocut-omit-1-lin-shrink"]),
        ("2025-07-09-bdd-other-orders", ["bdd-impl-nocomb-nocut-linear-onlyshrink"]),
        ("2025-07-11-fd-sat", ["no-parallel-seq", "Esat-seq"]),
        ("2025-07-11-madagascar", ["MpC-no-parallel-seq", "MpC-no-parallel-RR", "MpC-E-seq", "MpC-E-RR"]),
        ("2025-07-28-encoding-variants-eval", ["forall-basic-per-row-onlyshrink", "forall-elim-rnc-pairs-onlyshrink", "forall-elim-row-col-onlyshrink"])

        ]

for (idd,(expname,algos)) in enumerate(tofetch):
    exp.add_fetcher(
        f"data/{expname}-eval",  # (folder with the old experiments)
        filter=[filters.remove_revision],  # unnecessary but you can provide a function that will get rid of things that you don't need
        filter_algorithm=algos,   # This just tells which algorithms you want to fetch the data for
        name=f"fetch-{expname}-{idd}", # some name for the step I think this is optional
        merge=True,  # Whether you want to overricde the results.
    )


FORMAT = "html"

# REPORT TABLES
attributes = common_setup.ATTRIBUTES 

print(attributes)

exp.add_report(AbsoluteReport(attributes=attributes, filter=[filters.filter_kissat_known_unexplained_errors, filters.filter_madagascar_known_unexplained_errors, filters.filter_bdd_known_unexplained_errors,filters.compute_label_compression]), outfile=f"{SCRIPT_NAME}-all.html")


#
# SCATTER PLOTS
PLOT_FORMAT = "png"
for c1, c2 in [("no-parallel-seq", "seq"), ("no-parallel-seq", "MpC-no-parallel-seq"), ("seq", "MpC-no-parallel-seq"),
        ("bdd-impl-nocomb-nocut-omit-1-lin-shrink", "R^2E noloop"),("bdd-impl-nocomb-nocut-omit-1-lin-shrink", "Esat-seq"),("bdd-impl-nocomb-nocut-omit-1-lin-shrink", "MpC-E-seq"),
        ("R^2E noloop", "Esat-seq"),("R^2E noloop", "MpC-E-seq"),
        ("Esat-seq", "MpC-E-seq")]:
    for attr in ["planner_time", "plan_length","sat_variables", "sat_clauses"]:
        if (attr == "sat_clauses") and ("MpC" in c1 or "MpC" in c2): continue # madagascar's runtime is buggy
        exp.add_report(
            ScatterPlotReport(
                attributes=[attr],
                filter=[filters.remove_revision],
                filter_algorithm=[c1, c2],
                get_category=lambda x,y: x["domain"] if PLOT_FORMAT == "tex" else None,
                format=PLOT_FORMAT,
                show_missing=attr == "actual_runtime",
            ),
            name=f"scatterplot-{attr}-{c1}-vs-{c2}",
        )


for c1, c2 in [("bdd-impl-nocomb-nocut-omit-1-lin-shrink", "R^2E noloop")]:
    for attr in ["time_steps_with_label"]:
        if (attr == "total_time" or attr == "sat_clauses") and ("MpC" in c1 or "MpC" in c2): continue # madagascar's runtime is buggy
        exp.add_report(
            ScatterPlotReport(
                attributes=[attr],
                filter=[filters.remove_revision],
                filter_algorithm=[c1, c2],
                get_category=lambda x,y: x["domain"] if PLOT_FORMAT == "tex" else None,
                format=PLOT_FORMAT,
                show_missing=attr == "actual_runtime",
            ),
            name=f"scatterplot-{attr}-{c1}-vs-{c2}",
        )




exp.run_steps()


