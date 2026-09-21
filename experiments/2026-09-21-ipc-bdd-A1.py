#! /usr/bin/env python3

"""
bdd_sat on the IPC/PDDL suite, algorithm A, -shr, with the h2 preprocessor.

Scope is deliberately narrow (see _bdd_common.py): algorithm A only, the full
transition relation only, -shr only. Compared against two reference
configurations, chains_slf____rcpol and fulltransitions_slf_transeff.

The question is where the BDD encoding wins and where it is problematic, so
the report carries the per-time-step formula size from encoding_size_parser.py
alongside coverage -- a configuration that solves less while producing a
smaller formula per step is a different finding from one that simply produces
a huge formula.
"""

import os

from lab.environments import LocalEnvironment
from lab.reports import Attribute, geometric_mean, arithmetic_mean

from downward.reports.absolute import AbsoluteReport
from downward.reports.scatter import ScatterPlotReport

import common_setup
from common_setup import IssueConfig, IssueExperiment

import fts_parser
import encoding_size_parser
import filters
from snellius import SnelliusEnvironment

import _bdd_common as B

DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "eb3a1b0ed54569d3efd69f1e2f220e505a3eb63f"
REVISIONS = [REVISION]

CONFIGS = []
for s_name, s_opt in B.SEARCHES:
    print("Search: " + s_name)
    for t_name, t_opt in B.TRANSFORM_OPTS.items():
        CONFIGS.append(IssueConfig(
            f"{s_name}{t_name}",
            t_opt + ["--search", s_opt],
            driver_options=B.driver_opts(h2=True),
            build_options=B.build_opts()))

SUITE = common_setup.DEFAULT_SATISFICING_SUITE

#ENVIRONMENT = LocalEnvironment(processes=1)
ENVIRONMENT = SnelliusEnvironment(
    email="g.behnke@uva.nl",
)

exp = IssueExperiment(
    revisions=REVISIONS,
    configs=CONFIGS,
    environment=ENVIRONMENT,
)

exp.add_suite(BENCHMARKS_DIR, SUITE)

exp.add_parser(exp.EXITCODE_PARSER)
exp.add_parser(exp.TRANSLATOR_PARSER)
exp.add_parser(exp.SINGLE_SEARCH_PARSER)
exp.add_parser(exp.PLANNER_PARSER)

exp.add_step("build", exp.build)
exp.add_step("start", exp.start_runs)
exp.add_step("parse", exp.parse)
exp.add_parser(fts_parser.FTSParser())
exp.add_parser(encoding_size_parser.EncodingSizeParser())

exp.add_fetcher(name="fetch")

ATTRIBUTES = common_setup.ATTRIBUTES + [
    Attribute(a, min_wins=True, function=arithmetic_mean, absolute=False)
    for a in B.ENCODING_ATTRIBUTES if a != "bdd_failure_reason"
] + ["bdd_failure_reason"]

exp.add_report(
    AbsoluteReport(attributes=ATTRIBUTES,
                   filter=[filters.filter_bdd_known_unexplained_errors,
                           filters.filter_kissat_known_unexplained_errors]),
    outfile=f"{SCRIPT_NAME}-all.html")

for c1, c2 in [("A_1__chains_slf____rcpol-shr", "A_1__bdd_full-shr"),
               ("A_1__fulltransitions_slf_transeff-shr", "A_1__bdd_full-shr")]:
    exp.add_report(
        ScatterPlotReport(
            attributes=["planner_time"],
            filter_algorithm=[f"{REVISION}-{c1}", f"{REVISION}-{c2}"],
            get_category=lambda x, y: x["domain"],
            format="png",
            show_missing=True,
        ),
        name=f"scatterplot-planner-time-{c1}-vs-{c2}",
    )
    exp.add_report(
        ScatterPlotReport(
            attributes=["enc_step_clauses_last"],
            filter_algorithm=[f"{REVISION}-{c1}", f"{REVISION}-{c2}"],
            get_category=lambda x, y: x["domain"],
            format="png",
            show_missing=False,
        ),
        name=f"scatterplot-step-clauses-{c1}-vs-{c2}",
    )

exp.run_steps()
