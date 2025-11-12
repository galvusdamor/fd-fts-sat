#! /usr/bin/env python3

import itertools
import math
import os
from pathlib import Path
import subprocess

from lab.environments import TetralithEnvironment
from lab.reports import Attribute, geometric_mean, arithmetic_mean
from lab.experiment import Experiment

from downward import suites
from downward.reports.absolute import AbsoluteReport
from downward.reports.compare import ComparativeReport
from downward.reports.scatter import ScatterPlotReport

import common_setup
import filters
import madagascar_parser
from snellius import SnelliusEnvironment

DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]

CONFIGS = [
    ("MpC-no-parallel-seq", ["-P", "0", "-S", "1", "-A", "1"]),
    ("MpC-no-parallel-C", ["-P", "0"]),
    ("MpC-A-seq", ["-P", "1", "-S", "1", "-A", "1"]),
    ("MpC-A-C", ["-P", "1"]),
    ("MpC-E-seq", ["-P", "2", "-S", "1", "-A", "1"]),
    ("MpC-E-C", ["-P", "2"]),
]

TIME_LIMIT = 30 * 60 # 30 min
MEMORY_LIMIT = 3500 # 1700M


ENVIRONMENT = SnelliusEnvironment(
        email="g.behnke@uva.nl",
        )

exp = Experiment(environment=ENVIRONMENT)

SUITE = common_setup.DEFAULT_SATISFICING_SUITE

exp.add_resource("solver", "/home/behnkeg/software/madagascar/MpC")

common_options = ["-r", TIME_LIMIT, "-m", MEMORY_LIMIT, "-Q", "-o", "sas_plan"]

for name, config in CONFIGS:
    for task in suites.build_suite(BENCHMARKS_DIR, SUITE):
        run = exp.add_run()
        run.add_resource("domain", task.domain_file, "domain.pddl", symlink=True)
        run.add_resource("problem", task.problem_file, "problem.pddl", symlink=True)
        run.add_command(
            "planner",
            ["{solver}"] + config + common_options + ["{domain}", "{problem}"],
            time_limit=TIME_LIMIT,
            memory_limit=MEMORY_LIMIT,
        )
        run.add_command(
            "validate",
            ["validate", "{domain}", "{problem}", "sas_plan"],
            memory_limit=MEMORY_LIMIT,
        )
        run.set_property("domain", task.domain)
        run.set_property("problem", task.problem)
        run.set_property("algorithm", name)
        run.set_property("component_options", config)
        run.set_property("driver_options", common_options)
        run.set_property("time_limit", TIME_LIMIT)
        run.set_property("memory_limit", MEMORY_LIMIT * 1000)
        run.set_property("id", [name, task.domain, task.problem])


exp.add_parser(madagascar_parser.MadagascarParser())

exp.add_step('build', exp.build)
exp.add_step('start', exp.start_runs)
exp.add_step("parse", exp.parse)

exp.add_fetcher(name='fetch')

common_setup.add_compress_and_delete_runs_step(exp)

tofetch = [
        ("2025-11-05-good-configurations", ["CMit_chains_slf_lg_rcpol-shr","ff-shr"]),
        ("2025-11-06-good-configurations-2", ["CMit_chains_slf_lg_rcpol-ntr","ff-ntr"]),
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


FORMAT = "html"

# REPORT TABLES
attributes = common_setup.ATTRIBUTES

exp.add_report(AbsoluteReport(attributes=attributes, filter=[filters.filter_madagascar_known_unexplained_errors]), outfile=f"{SCRIPT_NAME}-all.html")



# SCATTER PLOTS

PLOT_FORMAT = "png"

#for c1, c2 in [(f"{REVISION}-Esat", f"{REVISION}-Esat-LP-F0.2s1M-clo")]:
#    exp.add_report(
#        ScatterPlotReport(
#            attributes=["planner_time"],
#            filter_algorithm=[c1, c2],
#            get_category=lambda x,y: x["domain"],
#            format=PLOT_FORMAT,
#            show_missing=True,
#        ),
#        name=f"scatterplot-planner-time-{c1}-vs-{c2}",
#    )

exp.run_steps()


