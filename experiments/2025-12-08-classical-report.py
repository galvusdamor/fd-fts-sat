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
from lab.experiment import Experiment

from downward.reports.absolute import AbsoluteReport
from downward.reports.compare import ComparativeReport
from downward.reports.scatter import ScatterPlotReport


from report.report_utils.total_coverage_table import TotalCoverageTable
from report.report_utils.my_table import MyTable

import common_setup
from common_setup import IssueConfig, IssueExperiment

import fts_parser

import filters
from snellius import SnelliusEnvironment

DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "a938b5d2d5697b3c17c045512fe56824101e2b6c"
REVISIONS = [REVISION]

CONFIGS = []

SUITE = common_setup.DEFAULT_SATISFICING_SUITE


#ENVIRONMENT = LocalEnvironment(processes=1)
ENVIRONMENT = SnelliusEnvironment(
        email="g.behnke@uva.nl",
        )

exp = Experiment(
)


## fetch for my own data
exp.add_fetcher(name='fetch', filter=[filters.remove_revision])


tofetch = [
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


tofetchAll = [
        "2025-11-05-good-configurations",
        "2025-11-06-good-configurations-2",
        "2025-11-06-madagascar",
        "2025-11-07-good-configurations-3",
        "2025-11-07-good-configurations-4",
        "2025-12-03-remaining-configs"
        ]

for expname in tofetchAll:
    exp.add_fetcher(
        f"data/{expname}-eval",  # (folder with the old experiments)
        filter=[filters.remove_revision],  # unnecessary but you can provide a function that will get rid of things that you don't need
        #filter_algorithm=algos,   # This just tells which algorithms you want to fetch the data for
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



### rcpo table
exp.add_report(MyTable(['slf________l', 'slf____rcpol', 'slf_lg_____l', 'slf_lg___p_l', 'slf_lg___pol', 'slf_lg__c__l', 'slf_lg__c_ol', 'slf_lg_r___l', 'slf_lg_r__ol', 'slf_lg_rcp_l', 'slf_lg_rcpol'],['A_1__chains_ntr', 'CMit_chains_ntr','A_1__chains_shr', 'CMit_chains_shr'], lambda row,column : f"{column[:-4]}_{row}-{column[-3:]}"), name="cov_table_rcpol", outfile="cov_table_rcpol.txt")

exp.add_report(MyTable(['slf_lg_rcpol_shr','slf____rcpol_shr','slf________l_shr','slf__________shr','slf_lg_rcpol_ntr','slf____rcpol_ntr','slf________l_ntr','slf__________ntr'],['A_1__chains', 'CMit_chains','A_1__slflpp', 'CMit_slflpp','A_1__seq___', 'CMit_seq___'], lambda row,column : f"{column}_{row[:-4]}-{row[-3:]}"), name="cov_table_shr_vs_ntr", outfile="cov_table_rcpol.txt")


### base table
exp.add_report(MyTable(['____________', '___________l', '_______rcpol', '____lg______', 'slf_________', 'slf________l', 'slf____rcpo_', 'slf____rcpol', 'slf_lg______', 'slf_lg_____l', 'slf_lg_rcpo_', 'slf_lg_rcpol'],['A_1__chains', 'A_1__seq___', 'A_1__slflpp', 'CMit_chains', 'CMit_seq___', 'CMit_slflpp'], lambda row,column : f"{column}_{row}-ntr"), name="cov_table_ntr", outfile="cov_table_ntr.txt")
exp.add_report(MyTable(['____________', '___________l', '_______rcpol', '____lg______', 'slf_________', 'slf________l', 'slf____rcpo_', 'slf____rcpol', 'slf_lg______', 'slf_lg_____l', 'slf_lg_rcpo_', 'slf_lg_rcpol'],['A_1__chains', 'A_1__seq___', 'A_1__slflpp', 'CMit_chains', 'CMit_seq___', 'CMit_slflpp'], lambda row,column : f"{column}_{row}-shr"), name="cov_table_shr", outfile="cov_table_shr.txt")


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

