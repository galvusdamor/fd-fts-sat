#! /usr/bin/env python3

import re
import itertools
import math
import os
from pathlib import Path
import subprocess

from lab.environments import TetralithEnvironment, LocalEnvironment
from lab.reports import Attribute, geometric_mean, arithmetic_mean
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
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "85180b4d2082f1b22760f4c11e1044b4033cd955"
REVISIONS = [REVISION]

CONFIGS = []
searches = {
            "R^2E noloop": "sat(encoding=2)",
}

DRIVER_OPTS = ["--overall-time-limit", "30m", "--overall-memory-limit", "1750m"]
TRANSFORM_OPTS = ["--transform", "transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation(ignore_irrelevant_tau_groups=false),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=false,max_time=900,cost_type=one,prune_transitions_from_goal=true,prune_transitions_from_goal=true)"]

for s_name, s_opt in searches.items():
    if s_name == "ff-pure":
        CONFIGS.append(IssueConfig(f'{s_name}', ['--search',  f'{s_opt}'], driver_options=DRIVER_OPTS, build_options=["-j16", "-s/gpfs/home2/behnkeg/software/kissat-p/build", "--kissat"]))
    else:
        CONFIGS.append(IssueConfig(f'{s_name}', TRANSFORM_OPTS + ['--search',  f'{s_opt}'], driver_options=DRIVER_OPTS, build_options=["-j16", "-s/gpfs/home2/behnkeg/software/kissat-p/build", "--kissat"]))


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
exp.add_parser(fts_parser.FTSParser())

exp.add_step('build', exp.build)
exp.add_step('start', exp.start_runs)
exp.add_step("parse", exp.parse)

exp.add_fetcher(name='fetch', filter=[filters.remove_revision])
common_setup.add_compress_and_delete_runs_step(exp)

FORMAT = "html"

# REPORT TABLES
attributes = common_setup.ATTRIBUTES

exp.add_report(AbsoluteReport(attributes=attributes, filter=[filters.filter_kissat_known_unexplained_errors]), outfile=f"{SCRIPT_NAME}-all.html")

exp.run_steps()
