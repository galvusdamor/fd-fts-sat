#! /usr/bin/env python

from pathlib import Path
import os
from collections import OrderedDict
from downward.reports.absolute import AbsoluteReport
from downward.reports.scatter import ScatterPlotReport
from lab.experiment import Experiment
from report_utils.nice_scatter import NiceScatterPlotReport, add_nice_scatter_plot_step
from report_utils.remove_file_step import remove_file
from report_utils.table_relative_expansions import get_table_relative_expansions
from report_utils.report_filters import joint_domains, invert_min_negative_dominance, unsolvable_wo_mystery, ignore_unexplained_errors, FilterAtr


TARGET_DIR = f"{os.path.dirname(os.path.abspath(__file__))}/report"

exp = Experiment(TARGET_DIR)

def change_domain(run):
    run['domain'] = run['domain_category']
    return run

exp.add_report(AbsoluteReport(attributes=[
    "coverage",
    "cost",
    "planner_time",
    "unsolvable_wo_mystery"
], filter= [change_domain], #,filter_algorithm=['lmcut','lmcut-bisim-dfp50k-bissh-gen']),
                              ), name="report", outfile="report.html")


algo_to_latex = {
    'blind': r'\configblind',
    'blind-bisim-atomic-nosh-gen': r'\configbisim',
    'blind-sim-atomic-nosh-gen': r'\configsim',
    'blind-noopsim-atomic-nosh-gen': r'\confignoopsim',
    'blind-ldsimalt-atomic-nosh-gen': r'\configldsim',
    'blind-qual-10-atomic-nosh-gen': r'\configqual10',
    'blind-qpos-10-atomic-nosh-gen': r'\configqpos10',
    'blind-qtrade-10-atomic-nosh-gen': r'\configqtrade10',
    'blind-qrel-10-atomic-nosh-gen': r'\configqrel10',
    'blind-qrel-10-atomic-nosh-gensucc': r'\configqrel10as',
    'blind-bisim-dfp50k-nosh-gen': r'\configbisim',
    'blind-sim-dfp50k-nosh-gen': r'\configsim',
    'blind-noopsim-dfp50k-nosh-gen': r'\confignoopsim',
    'blind-ldsimalt-dfp50k-nosh-gen': r'\configldsim',
    'blind-qual-10-dfp50k-nosh-gen': r'\configqual10',
    'blind-qpos-10-dfp50k-nosh-gen': r'\configqpos10',
    'blind-qtrade-10-dfp50k-nosh-gen': r'\configqtrade10',
    'blind-qrel-10-dfp50k-nosh-gen': r'\configqrel10',
    'blind-qrel-10-dfp50k-nosh-gensucc': r'\configqrel10as'
}


def scatter_alg(name, alg1, alg2, atr, domain_category):
    plot_options = {
        'size': '10cm',
        'num_extra_diagonal_lines': 2,
        'algo_to_latex': algo_to_latex,
        'extra_preamble' : [r"\makeatletter",
                            r"\def\input@path{{../}{./}}",
                            r"\makeatother",
                            # r"\input{packages}",
                            # r"\input{macros}"
                            ]
    }
    return (os.path.join(TARGET_DIR, name),
            NiceScatterPlotReport(filter_algorithm=[alg1, alg2], attributes=[atr], format='tex',
                                  get_category=lambda run1, run2: run1[domain_category],
                                  **plot_options))


scatter_plots = [scatter_alg(f"{atr}-{config1}-{config2}", config1, config2, atr, "domain_category")
                 for atr in ['total_time', 'sat_clauses', 'sat_variables']
                 for (config1,config2) in [('seq','row_seq-shr')]]

add_nice_scatter_plot_step(exp, scatter_plots)

exp.run_steps()
