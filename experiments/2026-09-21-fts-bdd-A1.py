#! /usr/bin/env python3

"""
bdd_sat on the FTS benchmarks.

The companion to 2026-09-17-bdd-reintegration.py, which runs the same encoder
on the PDDL suite. This one matters more: a local 928-run validation over 58
of the 431 FTS instances found that `bdd_sat(one_step_only=false)` solves a
*strict superset* of what the `label_sat` chains baseline solves under the
shrink transform (+4 instances), and +5/-2 without it -- the opposite of the
PDDL picture, where label_sat is ahead. This script is meant to check whether
that holds on the full suite and at a realistic time limit.

Both transforms are run, because the gap differed between them:

  -ntr  the FTS task as given, only re-costed. This is the setting the FTS
        benchmarks exist for.
  -shr  the same shrink/label-reduction used everywhere else on top of it.

Axes kept: one_step_only (the two semantics), combinebdds, the label order
(which is the BDD variable order) and cutbdds. Dropped relative to the PDDL
script: the Tseitin variants and bdd_size_limit, which did not separate the
configurations locally and would double the runtime.
"""

import re
import itertools
import math
import os
from pathlib import Path
import subprocess

from lab.environments import TetralithEnvironment, LocalEnvironment
from lab.reports import Attribute, geometric_mean, arithmetic_mean
from lab import tools
from lab.parser import Parser

from downward.reports.absolute import AbsoluteReport
from downward.reports.compare import ComparativeReport
from downward.reports.scatter import ScatterPlotReport

import common_setup
from common_setup import IssueConfig, IssueExperiment

import fts_parser

import filters
from snellius import SnelliusEnvironment

DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS_FTS_DIR = os.environ["FTS_BENCHMARKS"]
REVISION = "8fc09a408fb8a115722ccf111de83403a1244306"
REVISIONS = [REVISION]

KISSAT = "-s/gpfs/home2/behnkeg/software/kissat-p/build"
BUILD_OPTS = ["-j24", KISSAT, "--custom-kissat"]

CONFIGS = []

LABEL_BASELINE = ("label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,"
                  "use_label_group=false,use_empty_pillars=true,use_empty_rows=true,"
                  "use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,"
                  "force_at_least_one_action=false)")


def bdd(**kw):
    defaults = dict(one_step_only="true", combinebdds="false", impltseitsin="true",
                    omitforcedvariables="true", forcedvariablesthreshold=100,
                    bdd_size_limit=-1, cutbdds="false",
                    label_order="label_order_linear()")
    defaults.update(kw)
    return "bdd_sat(" + ",".join(f"{k}={v}" for k, v in defaults.items()) + ")"


encodings = {
    "label_chains___": LABEL_BASELINE,
    "bdd_1step______": bdd(),
    "bdd_full_______": bdd(one_step_only="false"),
    "bdd_full_comb__": bdd(one_step_only="false", combinebdds="true"),
    "bdd_1step_comb_": bdd(combinebdds="true"),
    "bdd_full_rev___": bdd(one_step_only="false", label_order="label_order_reverse()"),
    "bdd_full_rnd___": bdd(one_step_only="false", label_order="label_order_random()"),
    "bdd_full_cut___": bdd(one_step_only="false", cutbdds="true"),
}

RINTANEN_OPTS = ("solver_quiet=true,length_strategy=by_iteration(),memory_limit_mb=3500,"
                 "max_parallel_calls=20,scheduler_interval=1,schedule_formula_as_one=true")

searches = sum([
    [
        (("A1__" + name), f"sat(encoder={enc},solver_quiet=true,length_strategy=one_by_one())"),
        (("CMit" + name), f"rintanen(encoder={enc},{RINTANEN_OPTS})"),
    ]
    for name, enc in encodings.items()
], [])

DRIVER_OPTS = ["--overall-time-limit", "30m", "--overall-memory-limit", "3500m"]
TRANSFORM_OPTS = {
    "-ntr": ["--transform", "cost(cost_type=one)"],
    "-shr": ["--transform", "transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation(ignore_irrelevant_tau_groups=false),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=false,max_time=900,cost_type=one,prune_transitions_from_goal=true)"],
}

for s_name, s_opt in searches:
    print("Search: " + s_name)
    for t_name, t_opt in TRANSFORM_OPTS.items():
        CONFIGS.append(IssueConfig(
            f'{s_name}{t_name}',
            t_opt + ['--search', f'{s_opt}'],
            driver_options=DRIVER_OPTS,
            build_options=BUILD_OPTS))


SUITE = common_setup.FTS_SUITE

#ENVIRONMENT = LocalEnvironment(processes=1)
ENVIRONMENT = SnelliusEnvironment(
    email="g.behnke@uva.nl",
)

exp = IssueExperiment(
    revisions=REVISIONS,
    configs=CONFIGS,
    environment=ENVIRONMENT,
)

exp.add_suite(BENCHMARKS_FTS_DIR, SUITE)

exp.add_parser(exp.EXITCODE_PARSER)
exp.add_parser(exp.TRANSLATOR_PARSER)
exp.add_parser(exp.SINGLE_SEARCH_PARSER)
exp.add_parser(exp.PLANNER_PARSER)

exp.add_step('build', exp.build)
exp.add_step('start', exp.start_runs)
exp.add_step("parse", exp.parse)
exp.add_parser(fts_parser.FTSParser())

exp.add_fetcher(name='fetch')

FORMAT = "html"
attributes = common_setup.ATTRIBUTES

exp.add_report(
    AbsoluteReport(attributes=attributes,
                   filter=[filters.filter_bdd_known_unexplained_errors,
                           filters.filter_kissat_known_unexplained_errors]),
    outfile=f"{SCRIPT_NAME}-all.html")

# the comparison this script exists for
PLOT_FORMAT = "png"
for c1, c2 in [("A1__label_chains___-ntr", "A1__bdd_full_______-ntr"),
               ("A1__label_chains___-shr", "A1__bdd_full_______-shr"),
               ("CMitlabel_chains___-shr", "CMitbdd_full_______-shr")]:
    exp.add_report(
        ScatterPlotReport(
            attributes=["planner_time"],
            filter_algorithm=[c1, c2],
            get_category=lambda x, y: x["domain"],
            format=PLOT_FORMAT,
            show_missing=True,
        ),
        name=f"scatterplot-planner-time-{c1}-vs-{c2}",
    )

exp.run_steps()
