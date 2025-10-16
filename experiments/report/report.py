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
from report_utils.total_coverage_table import TotalCoverageTable


TARGET_DIR = f"{os.path.dirname(os.path.abspath(__file__))}/report"

exp = Experiment(TARGET_DIR)

def change_domain(run):
    run['domain'] = run['domain_category']
    return run

exp.add_report(AbsoluteReport(attributes=[
    "coverage",
    "cost",
    "planner_time",
    #'sat_variables', 'sat_clauses'
    #"unsolvable_wo_mystery"
], filter= [change_domain,ignore_unexplained_errors], filter_algorithm=['ff-trans','el_rnc_slfloopt_labgr_chain-shr', 'MpC-E-seq'],
                              ), name="report", outfile="report-coverage-against-FF.html")


exp.add_report(AbsoluteReport(attributes=[
    "coverage",
    "cost",
    "planner_time",
    'time_steps_with_label',
    #'sat_variables', 'sat_clauses'
    #"unsolvable_wo_mystery"
], filter= [change_domain,ignore_unexplained_errors], filter_algorithm=['el_rnc_slfloopt_labgr_seq-shr','el_rnc_slfloopt_labgr_chain-shr'],
                              ), name="report2", outfile="report-2.html")




exp.add_report(TotalCoverageTable(), name="cov_table", outfile="cov_table.txt")


algo_to_latex = {
    'blind': r'\configblind',
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
                 for atr in ['planner_time', 'sat_clauses', 'sat_variables', 'time_steps_with_label', 'cost', 'plan_length']
                 for (config1,config2) in [('el_rnc_slfloopt_labgr_seq-shr','el_rnc_slfloopt_labgr_chain-shr')]]

scatter_plots += [scatter_alg(f"{atr}-{config1}-{config2}", config1, config2, atr, "domain_category")
                 for atr in ['planner_time', 'sat_variables', 'time_steps_with_label']
                 for (config1,config2) in [('MpC-E-seq', 'el_rnc_slfloopt_labgr_chain-shr'), ('MpC-no-parallel-seq', 'el_rnc_slfloopt_labgr_chain-shr')]]

scatter_plots += [scatter_alg(f"{atr}-{config1}-{config2}", config1, config2, atr, "domain_category")
                 for atr in ['planner_time']
                 for (config1,config2) in [('ff-trans','el_rnc_slfloopt_labgr_chain-shr')]]

add_nice_scatter_plot_step(exp, scatter_plots)

exp.run_steps()
