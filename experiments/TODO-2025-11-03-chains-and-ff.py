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
REVISION = "f4c2d11353a4a2bd1389f5251029940d82853f9c"
REVISIONS = [REVISION]

CONFIGS = []

encodings = {
		"chains_slf_lg_rcpol": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		"chains_slf_lg_rcp_l": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=false,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		"chains_slf_lg_____l": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		"seq____slf_lg_rcpol": "label_sat(encoding=SEQUENTIAL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
        }

searches = sum([
        [
            (("A_1__" + name),("sat(encoder=" + encodings[name].replace("FORCE","true") + ",length_strategy=one_by_one())")),
            (("A_it_" + name), ("sat(encoder=" + encodings[name].replace("FORCE","false") + ",solver_quiet=true,continue_after_first_plan=false,length_strategy=by_iteration())")),
            (("CMit_" + name), ("rintanen(encoder=" + encodings[name].replace("FORCE","false") + ",solver_quiet=true,length_strategy=by_iteration(),memory_limit_mb=3500,max_parallel_calls=20,scheduler_interval=1,schedule_formula_as_one=true)"))]
            for name in encodings
        ],[]) + [
        ("ff",'lazy_greedy([ff(cost_type=one)], cost_type=one)')]


DRIVER_OPTS = ["--overall-time-limit", "30m", "--overall-memory-limit", "3500m"]
TRANSFORM_OPTS = {
        "-ntr" : ["--transform", "cost(cost_type=one)"],
        "-shr" : ["--transform", "transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation(ignore_irrelevant_tau_groups=false),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=false,max_time=900,cost_type=one,prune_transitions_from_goal=true,prune_transitions_from_goal=true)"],
#        "-mrg" : ["--transform", "transform_merge_and_shrink(shrink_strategy=shrink_bisimulation(greedy=false),merge_strategy=merge_stateless(merge_selector=score_based_filtering(scoring_functions=[product_size(1000),sf_miasm(shrink_strategy=shrink_bisimulation(greedy=false),max_states=100,threshold_before_merge=1),total_order(atomic_ts_order=reverse_level,product_ts_order=new_to_old,atomic_before_product=false)])),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=true,max_time=900)"],
        }

for s_name, s_opt in searches:
    print(s_name + " -> " + s_opt)
    for t_name, t_opt in TRANSFORM_OPTS.items():
        CONFIGS.append(IssueConfig(f'{s_name}{t_name}', t_opt + ['--search',  f'{s_opt}'], driver_options=DRIVER_OPTS, build_options=["-j16", "-s/gpfs/home2/behnkeg/software/kissat-p/build", "--custom-kissat"]))


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


tofetch = [
        ("2025-06-13-second-debugging-run", ["ff-pure"]),
        ("2025-06-18-thrid-debugging-run", ["ff-trans"]),
        #("2025-10-23-row-col-pillar-ones", ["seq_---_--_----shr", "chains_slf_lg_rcp_-shr",
        #        "chains_slf_lg_rc__-shr", "chains_slf_lg_r_p_-shr", "chains_slf_lg__cp_-shr",
        #        "chains_slf_lg_r___-shr", "chains_slf_lg__c__-shr", "chains_slf_lg___p_-shr",
        #        "chains_slf_lg_____-shr"
        #    ]),
        #("2025-10-23-ones-fixed", ["chains_slf_lg_rcpo-shr"]),
        #("2025-10-28-ones-algorithmC", ["C-chains_slf_lg_rcpo-shr"]),
        #("2025-06-13-second-debugging-run", ["ff-pure"]),
        #("2025-06-23-fourth-debugging-run", ["seq"]),
        ]

for (idd,(expname,algos)) in enumerate(tofetch):
    exp.add_fetcher(
        f"data/{expname}-eval",  # (folder with the old experiments)
        filter=[filters.remove_revision],  # unnecessary but you can provide a function that will get rid of things that you don't need
        filter_algorithm=algos,   # This just tells which algorithms you want to fetch the data for
        name=f"fetch-{expname}-{idd}", # some name for the step I think this is optional
        merge=True,  # Whether you want to overricde the results.
    )

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

exp.add_report(AbsoluteReport(attributes=attributes, filter=[filters.filter_bdd_known_unexplained_errors,filters.filter_kissat_known_unexplained_errors,filters.filter_exitcode250_unexplained_errors]), outfile=f"{SCRIPT_NAME}-all.html")
#exp.add_report(AbsoluteReport(attributes=attributes, filter_algorithm=["C-chains_slf_lg_rcpo-shr","CM2-chains_slf_lg_rcpo-shr"], filter=[filters.filter_bdd_known_unexplained_errors,filters.filter_kissat_known_unexplained_errors,filters.filter_exitcode250_unexplained_errors]), outfile=f"{SCRIPT_NAME}-memory-variants.html")


# SCATTER PLOTS

##PLOT_FORMAT = "png"
##
##for c1, c2 in [("forall-basic-per-row-onlyshrink", "forall-elim-row-col-onlyshrink"), ("forall-basic-per-row-onlyshrink", "forall-elim-rnc-pairs-onlyshrink"), ("forall-elim-row-col-onlyshrink",  "forall-elim-rnc-pairs-onlyshrink")]:
##    exp.add_report(
##        ScatterPlotReport(
##            attributes=["sat_clauses"],
##            filter_algorithm=[c1, c2],
##            get_category=lambda x,y: x["domain"],
##            format=PLOT_FORMAT,
##            show_missing=True,
##        ),
##        name=f"scatterplot_sat_clauses-{c1}-vs-{c2}",
##    )
##
##    exp.add_report(
##        ScatterPlotReport(
##            attributes=["sat_variables"],
##            filter_algorithm=[c1, c2],
##            get_category=lambda x,y: x["domain"],
##            format=PLOT_FORMAT,
##            show_missing=True,
##        ),
##        name=f"scatterplot-sat_variables-{c1}-vs-{c2}",
##    )
##
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
##
exp.run_steps()

