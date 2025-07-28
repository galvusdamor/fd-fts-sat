#! /usr/bin/env python3

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
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "28715a344c2902f48313eae89b9e046a71486e03"
REVISIONS = [REVISION]

CONFIGS = []
searches = {
            "seq-basic-per-row": "sat(encoding=0,solver_quiet=true,label_order=label_order_linear)",
            "seq-elim-row-col": "sat(encoding=1,solver_quiet=true,label_order=label_order_linear)",
            "seq-elim-rnc-pairs": "sat(encoding=2,solver_quiet=true,label_order=label_order_linear)",
}

DRIVER_OPTS = ["--overall-time-limit", "30m", "--overall-memory-limit", "1750m"]
TRANSFORM_OPTS = {
#        "-notrans" : ["--transform", "cost(cost_type=one)"],
        "-onlyshrink" : ["--transform", "transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation(ignore_irrelevant_tau_groups=false),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=false,max_time=900,cost_type=one,prune_transitions_from_goal=true,prune_transitions_from_goal=true)"],
#        "-merge" : ["--transform", "transform_merge_and_shrink(shrink_strategy=shrink_bisimulation(greedy=false),merge_strategy=merge_stateless(merge_selector=score_based_filtering(scoring_functions=[product_size(1000),sf_miasm(shrink_strategy=shrink_bisimulation(greedy=false),max_states=100,threshold_before_merge=1),total_order(atomic_ts_order=reverse_level,product_ts_order=new_to_old,atomic_before_product=false)])),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=true,max_time=900)"],
        }

for s_name, s_opt in searches.items():
    for t_name, t_opt in TRANSFORM_OPTS.items():
        if s_name == "ff-pure":
            CONFIGS.append(IssueConfig(f'{s_name}{t_name}', ['--search',  f'{s_opt}'], driver_options=DRIVER_OPTS, build_options=["-j16", "-s/gpfs/home2/behnkeg/software/kissat-p/build", "--kissat"]))
        else:
            CONFIGS.append(IssueConfig(f'{s_name}{t_name}', t_opt + ['--search',  f'{s_opt}'], driver_options=DRIVER_OPTS, build_options=["-j16", "-s/gpfs/home2/behnkeg/software/kissat-p/build", "--kissat"]))


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

exp.add_step('build', exp.build)
exp.add_step('start', exp.start_runs)
exp.add_step("parse", exp.parse)
exp.add_parser(fts_parser.FTSParser())

## fetch for my own data
exp.add_fetcher(name='fetch', filter=[filters.remove_revision])

## zipping files
common_setup.add_compress_and_delete_runs_step(exp)


tofetch = [
#        ("2025-06-18-thrid-debugging-run", ["ff-trans"]),
#        ("2025-06-18-thrid-debugging-run-bdd", ["bdd-impl-comb-nocut", "bdd-impl-comb-cut", "bdd-biim-nocomb-nocut"]),
#        ("2025-06-13-second-debugging-run", ["bdd-impl-nocomb-nocut", "bdd-impl-nocomb-nocut-onetrans", "ff-pure"]),
        ]

for (expname,algos) in tofetch:
    exp.add_fetcher(
        f"../{expname}/data/{expname}-eval",  # (folder with the old experiments)
        filter=[filters.remove_revision],  # unnecessary but you can provide a function that will get rid of things that you don't need
        filter_algorithm=algos,   # This just tells which algorithms you want to fetch the data for
        #name=f"fetch-{new_algo}-from-{expname}", # some name for the step I think this is optional
        merge=True,  # Whether you want to overricde the results.
    )


FORMAT = "html"

# REPORT TABLES
attributes = common_setup.ATTRIBUTES


def remove_ff_configs(run):
    if "ff" in run["algorithm"]:
        return False
    return run

exp.add_report(AbsoluteReport(attributes=attributes, filter=[filters.filter_bdd_known_unexplained_errors,filters.filter_kissat_known_unexplained_errors]), outfile=f"{SCRIPT_NAME}-all.html")


# SCATTER PLOTS

PLOT_FORMAT = "png"

##for c1, c2 in [("blind-LP-F0.2s1M", "sat-LP-F0.2s1M"), ("blind-LP-L1.0s1M", "sat-LP-L1.0s1M")]:
##    exp.add_report(
##        ScatterPlotReport(
##            attributes=["planner_time"],
##            filter_algorithm=[c1, c2],
##            get_category=lambda x,y: x["domain"],
##            format=PLOT_FORMAT,
##            show_missing=True,
##        ),
##        name=f"scatterplot-planner-time-{c1}-vs-{c2}",
##    )

exp.run_steps()

