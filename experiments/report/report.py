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
from report_utils.my_table import MyTable


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
], filter= [change_domain,ignore_unexplained_errors], # filter_algorithm=['ff-trans','el_rnc_slfloopt_labgr_chain-shr', 'MpC-E-seq'],
                              ), name="report", outfile="report-all.html")


# exp.add_report(AbsoluteReport(attributes=[
#     "coverage",
#     "cost",
#     "planner_time",
#     'time_steps_with_label',
#     #'sat_variables', 'sat_clauses'
#     #"unsolvable_wo_mystery"
# ], filter= [change_domain,ignore_unexplained_errors], filter_algorithm=['el_rnc_slfloopt_labgr_seq-shr','el_rnc_slfloopt_labgr_chain-shr'],
#                               ), name="report2", outfile="report-2.html")




exp.add_report(TotalCoverageTable(), name="cov_table", outfile="cov_table.txt")


exp.add_report(MyTable(['____________', '___________l', '_______rcpol', '____lg______', 'slf_________', 'slf________l', 'slf____rcpo_', 'slf____rcpol', 'slf_lg______', 'slf_lg_____l', 'slf_lg___p_l', 'slf_lg___pol', 'slf_lg__c__l', 'slf_lg__c_ol', 'slf_lg_r___l', 'slf_lg_r__ol', 'slf_lg_rcp__', 'slf_lg_rcp_l', 'slf_lg_rcpo_', 'slf_lg_rcpol'],['A_1__chains', 'A_1__seq___', 'A_1__slflpp', 'CMit_chains', 'CMit_seq___', 'CMit_slflpp'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column}_{row}-ntr"), name="cov_table_ntr", outfile="cov_table_ntr.txt")

exp.add_report(MyTable(['____________', '___________l', '_______rcpol', '____lg______', 'slf_________', 'slf________l', 'slf____rcpo_', 'slf____rcpol', 'slf_lg______', 'slf_lg_____l', 'slf_lg___p_l', 'slf_lg___pol', 'slf_lg__c__l', 'slf_lg__c_ol', 'slf_lg_r___l', 'slf_lg_r__ol', 'slf_lg_rcp__', 'slf_lg_rcp_l', 'slf_lg_rcpo_', 'slf_lg_rcpol'],['A_1__chains', 'A_1__seq___', 'A_1__slflpp', 'CMit_chains', 'CMit_seq___', 'CMit_slflpp'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column}_{row}-shr"), name="cov_table_shr", outfile="cov_table_shr.txt")

exp.add_report(MyTable(['slf________l', 'slf____rcpol', 'slf_lg_____l', 'slf_lg___p_l', 'slf_lg___pol', 'slf_lg__c__l', 'slf_lg__c_ol', 'slf_lg_r___l', 'slf_lg_r__ol', 'slf_lg_rcp_l', 'slf_lg_rcpol'],['A_1__chains_ntr', 'CMit_chains_ntr','A_1__chains_shr', 'CMit_chains_shr'], lambda row,column : f"{a938b5d2d5697b3c17c045512fe56824101e2b6c-column[:-4]}_{row}-{column[-3:]}"), name="cov_table_rcpol", outfile="cov_table_rcpol.txt")

exp.add_report(MyTable(['slf_lg_rcpol_shr','slf____rcpol_shr','slf________l_shr','slf__________shr','slf_lg_rcpol_ntr','slf____rcpol_ntr','slf________l_ntr','slf__________ntr'],['A_1__chains', 'CMit_chains','A_1__slflpp', 'CMit_slflpp','A_1__seq___', 'CMit_seq___'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column}_{row[:-4]}-{row[-3:]}"), name="cov_table_shr_vs_ntr", outfile="cov_table_rcpol.txt")


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
