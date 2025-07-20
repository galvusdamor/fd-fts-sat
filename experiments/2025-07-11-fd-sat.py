#! /usr/bin/env python3

import os

from lab.environments import TetralithEnvironment
from lab.reports import Attribute, geometric_mean, arithmetic_mean

from downward.experiment import FastDownwardExperiment

from downward.reports.absolute import AbsoluteReport
from downward.reports.compare import ComparativeReport
from downward.reports.scatter import ScatterPlotReport

import common_setup

import filters

import sat_parser

from snellius import SnelliusEnvironment

DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]

BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
BENCHMARKS_DIR_AXIOMS = os.environ["BENCHMARKS_AXIOMS"]
BENCHMARKS_DIR_AXIOMS_CONDEFFS = os.environ["BENCHMARKS_AXIOMS_CONDEFFS"]

REPO = common_setup.get_repo_base()
REPO_SYMK = "/gpfs/home2/behnkeg/lab/symk"
REPO_DOWNWARD_SAT = "/gpfs/home2/behnkeg/lab/downward-sat"

REVISION = "a0441e9d47e9c34d4d2db1221ec0b8bd2bb93461"
REVISION_SYMK = "43b090cee3143d1fcd96a160d335066faf578a50"
REVISION_DOWNWARD_SAT = "cbf28a2384b29188d522fee3df67c2b9b90171b6"

BUILD_OPTS_SAT = ["-j16", "-s/gpfs/home2/behnkeg/software/kissat/build", "--kissat"] 

common_options = ["--search"]
searches = {
            "no-parallel-seq": common_options + ["sat(encoding=0)"],
            "Esat-seq": common_options + ["sat(encoding=2)"],
}

DRIVER_OPTS =    ["--overall-time-limit", "30m", '--validate-time-limit', '5m', '--validate-memory-limit', '1700M']
#DRIVER_OPTS_INC = ["--overall-time-limit", "7m", '--validate-time-limit', '5m', '--validate-memory-limit', '3500M']


ENVIRONMENT = SnelliusEnvironment(
        email="g.behnke@uva.nl",
        )

exp = FastDownwardExperiment(environment=ENVIRONMENT)

for s_name, s_opt in searches.items():
    exp.add_algorithm(f'{s_name}', REPO_DOWNWARD_SAT, REVISION_DOWNWARD_SAT, s_opt, driver_options=DRIVER_OPTS, build_options=BUILD_OPTS_SAT)

#exp.add_algorithm('lama-first', REPO, REVISION, [], driver_options=DRIVER_OPTS + ["--alias", "lama-first"], build_options=BUILD_OPTS_SAT)
#exp.add_algorithm("SymK-bd", REPO_SYMK, REVISION_SYMK, ["--search", "sym_bd(cost_type=one)"], driver_options=DRIVER_OPTS)

exp.add_suite(BENCHMARKS_DIR, common_setup.DEFAULT_SATISFICING_SUITE)

exp.add_parser(exp.EXITCODE_PARSER)
exp.add_parser(exp.TRANSLATOR_PARSER)
exp.add_parser(exp.SINGLE_SEARCH_PARSER)
exp.add_parser(exp.PLANNER_PARSER)
exp.add_parser(sat_parser.SATParser())

exp.add_step('build', exp.build)
exp.add_step('start', exp.start_runs)
exp.add_step("parse", exp.parse)

exp.add_fetcher(name='fetch')

common_setup.add_compress_and_delete_runs_step(exp)


FORMAT = "html"

# REPORT TABLES
attributes = common_setup.ATTRIBUTES

#### tex table
exp.add_report(AbsoluteReport(attributes=attributes, filter=[filters.remove_revision, filters.filter_kissat_known_unexplained_errors]), outfile=f"{SCRIPT_NAME}.html")
#### tex table
exp.add_report(AbsoluteReport(attributes=["coverage"], filter=[filters.remove_revision, filters.filter_kissat_known_unexplained_errors], format="tex"),
               outfile=f"{SCRIPT_NAME}-coverage.tex")


##
### SCATTER PLOTS
##
##PLOT_FORMAT = "png"
##
##def add_actual_runtime(run):
##    if run["coverage"] == 1:
##        run["actual_runtime"] = run["translator_time_done"] + run["total_time"]
##    return run
##
##for c1, c2 in [("lama-first", f"Esat-{suffix}")]:
##    for attr in ["actual_runtime", "plan_length"]:
##        exp.add_report(
##            ScatterPlotReport(
##                attributes=[attr],
##                filter=[filters.remove_revision, virtual_solver_filter.add_run, virtual_solver_filter.replace_config, add_actual_runtime],
##                filter_algorithm=[c1, c2],
##                get_category=lambda x,y: x["domain"] if PLOT_FORMAT == "tex" else None,
##                format=PLOT_FORMAT,
##                show_missing=attr == "actual_runtime",
##            ),
##            name=f"scatterplot-{attr}-{c1}-vs-{c2}",
##        )
##
##
exp.run_steps()


