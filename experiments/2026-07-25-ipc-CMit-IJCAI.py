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
from report.report_utils.domain_coverage_table import DomainCoverageTable 
from report.report_utils.my_table import MyTable

import common_setup
from common_setup import IssueConfig, IssueExperiment

import fts_parser

import filters
from snellius import SnelliusEnvironment

DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
#BENCHMARKS_FTS_DIR = os.environ["FTS_BENCHMARKS"]
REVISION = "b9d8cd1ba434396d408cab7f3a85261c314f5e17"
REVISIONS = [REVISION]


CONFIGS = []

encodings = {
		#"chains_slf_________": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=false,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=false,force_at_least_one_action=FORCE)",
		#"chains_slf________l": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=false,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"chains_slf____rcpo_": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=false,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=false,force_at_least_one_action=FORCE)",
		"chains_slf____rcpol": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=false,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"chains_slf_lg_____l": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"chains_slf_lg___p_l": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"chains_slf_lg___pol": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=false,use_empty_cols=false,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"chains_slf_lg__c__l": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=true,use_positive_one=false,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"chains_slf_lg__c_ol": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"chains_slf_lg_r___l": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=false,use_empty_rows=true,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"chains_slf_lg_r__ol": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=false,use_empty_rows=true,use_empty_cols=false,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"chains_slf_lg_rcp__": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=false,use_ones_in_last_dimension=false,force_at_least_one_action=FORCE)",
		#"chains_slf_lg_rcp_l": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=false,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"chains_slf_lg_rcpo_": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=false,force_at_least_one_action=FORCE)",
		#"chains_slf_lg_rcpol": "label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"seq________________": "label_sat(encoding=SEQUENTIAL,use_self_loop_optimisation=false,use_label_group=false,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=false,force_at_least_one_action=FORCE)",
		#"seq_______________l": "label_sat(encoding=SEQUENTIAL,use_self_loop_optimisation=false,use_label_group=false,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"seq___________rcpol": "label_sat(encoding=SEQUENTIAL,use_self_loop_optimisation=false,use_label_group=false,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"seq________lg______": "label_sat(encoding=SEQUENTIAL,use_self_loop_optimisation=false,use_label_group=true,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=false,force_at_least_one_action=FORCE)",
		#"seq____slf_________": "label_sat(encoding=SEQUENTIAL,use_self_loop_optimisation=true,use_label_group=false,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=false,force_at_least_one_action=FORCE)",
		#"seq____slf_lg______": "label_sat(encoding=SEQUENTIAL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=false,force_at_least_one_action=FORCE)",
		#"seq____slf_lg_____l": "label_sat(encoding=SEQUENTIAL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"seq____slf_lg_rcpo_": "label_sat(encoding=SEQUENTIAL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=false,force_at_least_one_action=FORCE)",
		#"seq____slf_lg_rcpol": "label_sat(encoding=SEQUENTIAL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"seq____slf____rcpol": "label_sat(encoding=SEQUENTIAL,use_self_loop_optimisation=true,use_label_group=false,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"slflpp_slf_________": "label_sat(encoding=SELF_LOOP_PARALLEL,use_self_loop_optimisation=true,use_label_group=false,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=false,force_at_least_one_action=FORCE)",
		#"slflpp_slf____rcpol": "label_sat(encoding=SELF_LOOP_PARALLEL,use_self_loop_optimisation=true,use_label_group=false,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"slflpp_slf_lg______": "label_sat(encoding=SELF_LOOP_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=false,force_at_least_one_action=FORCE)",
		#"slflpp_slf_lg_____l": "label_sat(encoding=SELF_LOOP_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=false,use_empty_rows=false,use_empty_cols=false,use_positive_one=false,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
		#"slflpp_slf_lg_rcpol": "label_sat(encoding=SELF_LOOP_PARALLEL,use_self_loop_optimisation=true,use_label_group=true,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=FORCE)",
        }

searches = sum([
        [
            #(("A_1__" + name),("sat(encoder=" + encodings[name].replace("FORCE","true") + ",solver_quiet=true,length_strategy=one_by_one())")),
            #(("A_it_" + name), ("sat(encoder=" + encodings[name].replace("FORCE","false") + ",solver_quiet=true,continue_after_first_plan=false,length_strategy=by_iteration())")),
            (("CMit_" + name), ("rintanen(encoder=" + encodings[name].replace("FORCE","false") + ",solver_quiet=true,length_strategy=by_iteration(),memory_limit_mb=3500,max_parallel_calls=20,scheduler_interval=1,schedule_formula_as_one=true)"))]
            for name in encodings
        ],[]) ##+ [
        ##("ff",'lazy_greedy([ff(cost_type=one)], cost_type=one)')]


DRIVER_OPTS = ["--overall-time-limit", "30m", "--overall-memory-limit", "3500m"]
TRANSFORM_OPTS = {
        #"-ntr" : ["--transform", "cost(cost_type=one)"],
        "-shr" : ["--transform-task", "./builds/release64/bin/preprocess-h2", "--transform", "transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation(ignore_irrelevant_tau_groups=false),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=false,max_time=900,cost_type=one,prune_transitions_from_goal=true,prune_transitions_from_goal=true)"],
#        "-mrg" : ["--transform", "transform_merge_and_shrink(shrink_strategy=shrink_bisimulation(greedy=false),merge_strategy=merge_stateless(merge_selector=score_based_filtering(scoring_functions=[product_size(1000),sf_miasm(shrink_strategy=shrink_bisimulation(greedy=false),max_states=100,threshold_before_merge=1),total_order(atomic_ts_order=reverse_level,product_ts_order=new_to_old,atomic_before_product=false)])),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=true,max_time=900)"],
        #'-lr' : ['--transform', 'transform_merge_and_shrink(label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=false,run_main_loop=false,max_time=900,cost_type=one,prune_transitions_from_goal=false)'],
        #'-fwb-lr' : ['--transform', 'transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation(ignore_irrelevant_tau_groups=true, apply_haslum_rule=true),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=false,max_time=900,cost_type=one,prune_transitions_from_goal=true)'],
        #'-fwb-lr-d100' :["--transform", 'transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation(ignore_irrelevant_tau_groups=true, apply_haslum_rule=true),merge_strategy=merge_stateless(merge_selector=score_based_filtering(scoring_functions=[product_size(100),goal_relevance,dfp,total_order(atomic_ts_order=reverse_level,product_ts_order=new_to_old,atomic_before_product=false)])),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=true,max_time=900,cost_type=one,prune_transitions_from_goal=true)'],
        #'-fwb-lr-m100': ["--transform", 'transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation(ignore_irrelevant_tau_groups=true, apply_haslum_rule=true),merge_strategy=merge_stateless(merge_selector=score_based_filtering(scoring_functions=[product_size(100),sf_miasm(shrink_strategy=shrink_own_bisimulation,threshold_before_merge=1,max_states=100),total_order(atomic_ts_order=reverse_level,product_ts_order=new_to_old,atomic_before_product=false)])),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=true,max_time=900,cost_type=one,prune_transitions_from_goal=true)'],
    }

for s_name, s_opt in searches:
    print("Search: " + s_name)
    for t_name, t_opt in TRANSFORM_OPTS.items():
        CONFIGS.append(IssueConfig(f'{s_name}{t_name}', t_opt + ['--search',  f'{s_opt}'], driver_options=DRIVER_OPTS, build_options=["-j24", "-s/gpfs/home5/jsa/software/kissat-p/build", "--custom-kissat"]))


#print("Search: " + "ff-pref")
#for t_name, t_opt in TRANSFORM_OPTS.items():
   #CONFIGS.append(IssueConfig(f'ff-pref{t_name}', t_opt + ["--heuristic", "hff=ff(cost_type=one)", "--search", "lazy_greedy([hff], preferred=[hff],cost_type=one)"], driver_options=DRIVER_OPTS, build_options=["-j24", "-s/gpfs/home2/behnkeg/software/kissat-p/build", "--custom-kissat"]))

#print("Search: " + "ff")
#for t_name, t_opt in TRANSFORM_OPTS.items():
#    CONFIGS.append(IssueConfig(f'ff{t_name}', t_opt + ["--heuristic", "hff=ff(cost_type=one)", "--search", "lazy_greedy([hff],cost_type=one)"], driver_options=DRIVER_OPTS, build_options=["-j24", "-s/gpfs/home2/behnkeg/software/kissat-p/build", "--custom-kissat"]))


SUITE = common_setup.DEFAULT_SATISFICING_SUITE


#ENVIRONMENT = LocalEnvironment(processes=1)
ENVIRONMENT = SnelliusEnvironment(
        email="j.sa@uva.nl",
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
exp.add_fetcher(name='fetch') #, filter=[filters.remove_revision])







tofetch = [
        ("2025-10-31-fts-benchmarks-with-new_algos-loop-seq-chain", ["A1--chains_slf_lg_rcpol-ntr", "A1--chains_slf_lg_rcpol-shr", "A1--loop_slf_lg_rcpol-ntr", "A1--loop_slf_lg_rcpol-shr", "A1--chains_slf_lg_____l-ntr", "A1--chains_slf_lg_____l-shr", "A1--loop_slf_lg_____l-ntr", "A1--loop_slf_lg_____l-shr"]),
        ("2025-10-31-fts-benchmarks-with-new_algos-loop-seq-chain-C", ["CMitchains_slf_lg_rcpol-ntr", "CMitchains_slf_lg_rcpol-shr", "CMitloop_slf_lg_rcpol-ntr", "CMitloop_slf_lg_rcpol-shr", "CMitchains_slf_lg_____l-ntr", "CMitchains_slf_lg_____l-shr", "CMitloop_slf_lg_____l-ntr", "CMitloop_slf_lg_____l-shr"])
        #("2025-10-22-fts-benchmarks-three-tests", ["chains_slf_lg_rcp-onlyshrink","loopparallel_slf_lg_rcp-onlyshrink","seq_slf_lg_rcp-onlyshrink","seq_---_--_----onlyshrink"]),
        #("2025-06-13-second-debugging-run", ["ff-pure"]),
        #("2025-06-18-thrid-debugging-run", ["ff-trans"]),
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


def name_config2 (alg):
    return alg

exp.add_report(DomainCoverageTable(["ff-shr","ff-pref-shr","CMit_chains_slf____rcpol-shr","CMit_seq____slf____rcpol-shr","CMit_slflpp_slf____rcpol-shr"],name_config2), outfile=f"{SCRIPT_NAME}-coverage.tex")


def name_config (row, column):
    prefix = "" #if 'm100' in row else 'a938b5d2d5697b3c17c045512fe56824101e2b6c-'
    if column.startswith('MpC'):
        if row == "ntr":
            return column
        else:
            return "nothing"
    elif column.startswith('ff'):
        print(f"{prefix}{column}-{row}")
        return f"{prefix}{column}-{row}"
    else:
        print(f"{prefix}{column}_slf____rcpol-{row}")
        return f"{prefix}{column}_slf____rcpol-{row}"
    
exp.add_report(MyTable(['ntr','lr','shr','fwb-lr-m100'],['CMit_seq___', 'CMit_slflpp', 'CMit_chains', 'ff','ff-pref'], name_config), name="cov_table_sas", outfile="cov_table_sas.txt")


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

