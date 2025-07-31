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

exp.add_report(AbsoluteReport(attributes=[
    "coverage",
    "cost",
    "planner_time",
    "unsolvable_wo_mystery"
] #,filter_algorithm=['lmcut','lmcut-bisim-dfp50k-bissh-gen']),
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
                            r"\input{packages}",
                            r"\input{macros}"]
    }
    return (os.path.join(TARGET_DIR, name),
            NiceScatterPlotReport(filter_algorithm=[alg1, alg2], attributes=[atr], format='tex',
                                  get_category=lambda run1, run2: run1[domain_category],
                                  **plot_options))

# Analysis on k
# for k in [5,10,50,100,1000]:
#     for atr in ['time_ldsim', 'time_completed_preprocessing', 'generated', 'evaluated', 'expansions', 'expansions_until_last_jump', 'search_time', 'total_time']:
#         exp.add_report(ScatterPlotReport(filter_algorithm=['blind-qrel-1-atomic-nosh-gen', f'blind-qrel-{k}-atomic-nosh-gen'], attributes=[atr]), name=f"{atr}_k1_vs_k{k:>04}")



scatter_plots_atomic = [scatter_alg("expansions-base-vs-ldsim-atomic", "blind", "blind-ldsimalt-atomic-nosh-gen",
                                    "expansions_until_last_jump", "domain_category_2"),
                        scatter_alg("expansions-ldsim-vs-qrel-atomic", "blind-ldsimalt-atomic-nosh-gen",
                                    "blind-qrel-10-atomic-nosh-gen", "expansions_until_last_jump", "domain_category_1"),
                        scatter_alg("expansions-base-vs-qrel-dfpbis", "blind",
                                    "blind-qrel-10-dfp50k-bissh-gen", "expansions_until_last_jump",
                                    "domain_category_1"),
                        ]

scatter_plots_time_ldsim = [scatter_alg("timesim-ldsim-vs-qrel-atomic", "blind-ldsimalt-atomic-nosh-gen",
                                    "blind-qrel-10-atomic-nosh-gen", "time_ldsim", "domain_category"),
                            scatter_alg("timepreprocess-ldsim-vs-qrel-atomic", "blind-ldsimalt-atomic-nosh-gen",
                                    "blind-qrel-10-atomic-nosh-gen", "time_completed_preprocessing", "domain_category"),]



scatter_plots_atomic_action_selection = [
    scatter_alg(f"{atribute}-qrel-vs-qrelas-{transformation}",f"blind-qrel-10-{transformation}-gen", f"blind-qrel-10-{transformation}-gensucc", atribute, "domain_category")
    for transformation in ['atomic-nosh', 'dfp50k-bissh']
    for atribute in ['expansions_until_last_jump', 'generated', 'evaluated', 'expansions']
]

scatter_plots_atomic_action_selection = [
    scatter_alg(f"{atribute}-qrel-vs-qrelas-{transformation}",f"blind-qrel-10-{transformation}-gen", f"blind-qrel-10-{transformation}-gensucc", atribute, "domain_category")
    for transformation in ['atomic-nosh', 'dfp50k-bissh']
    for atribute in ['expansions_until_last_jump', 'generated', 'evaluated', 'expansions']
]

scatter_plots_atomic_usage = [
    scatter_alg(f"{atribute}-{usage1}-vs-{usage2}-{transformation}",f"blind-qrel-10-{transformation}-{usage1}", f"blind-qrel-10-{transformation}-{usage2}", atribute, "domain_category")
    for transformation in ['atomic-nosh', 'dfp50k-bissh']
    for atribute in ['expansions_until_last_jump', 'generated', 'evaluated', 'expansions', 'search_time', 'total_time']
    for (usage1, usage2) in [('exp', 'parsucc'), ('exp', 'gen')]
]

scatter_plots_atomic_usage_baseline = [
    scatter_alg(f"{atribute}-baseline-vs-{usage}-{transformation}",f"blind", f"blind-qrel-10-{transformation}-{usage}", atribute, "domain_category")
    for transformation in ['atomic-nosh', 'dfp50k-bissh']
    for atribute in ['expansions_until_last_jump', 'generated', 'evaluated', 'expansions', 'search_time', 'total_time']
    for usage in ['exp', 'parsucc', 'gen', 'gensucc']
]

add_nice_scatter_plot_step(exp, scatter_plots_atomic + scatter_plots_atomic_action_selection + scatter_plots_atomic_usage + scatter_plots_atomic_usage_baseline)

exp.run_steps()
