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


TARGET_DIR = f"{os.path.dirname(os.path.abspath(__file__))}/report-aaai27"

exp = Experiment(TARGET_DIR)
TARGET_DIR+ '-eval'

def change_domain(run):
    run['domain'] = run['domain_category']
    return run

exp.add_report(AbsoluteReport(attributes=[
    "coverage",
    "cost",
    "planner_time",
#    'sat_variables', 'sat_clauses', 'timesteps_with_label'
    #"unsolvable_wo_mystery"
],  # filter_algorithm=['ff-trans','el_rnc_slfloopt_labgr_chain-shr', 'MpC-E-seq'],
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




axis_options = { 'planner_time' : {
    'extra x ticks' : '10000',
    'extra y tick style' : '{grid=major}',
    'extra y ticks' : '10000',
    'extra x tick labels' : '{uns.}',
    'extra y tick labels' : '{uns.}',
    'extra x tick style' : '{grid=major, xticklabel style={rotate=90,anchor=east}}',
    'xtickten' : '{-2,-1,0,1,2,3}',
    'ytickten' : '{-2,-1,0,1,2,3}',
    'tick label style' : '{font=\\footnotesize}',
    'label style' : '{font=\\small}',
    'ylabel style' : '{yshift=-7pt}'
},
                 'sat_clauses' : {
                     'extra x ticks' : '100000000',
                     'extra y tick style' : '{grid=major}',
                     'extra y ticks' : '100000000',
                     'extra x tick labels' : '{uns.}',
                     'extra y tick labels' : '{uns.}',
                     'extra x tick style' : '{grid=major, xticklabel style={rotate=90,anchor=east}}',
                     'xtickten' : '{0,1,2,3,4,5,6,7}',
                     'ytickten' : '{0,1,2,3,4,5,6,7}',
                     'tick label style' : '{font=\\footnotesize}',
                     'label style' : '{font=\\small}',
                     'ylabel style' : '{yshift=-7pt}'
                 },
                 'sat_variables' : {},
                 'time_steps_with_label' : {
                     'extra x ticks' : '10000',
                     'extra y tick style' : '{grid=major}',
                     'extra y ticks' : '10000',
                     'extra x tick labels' : '{uns.}',
                     'extra y tick labels' : '{uns.}',
                     'extra x tick style' : '{grid=major, xticklabel style={rotate=90,anchor=east}}',
                     'xtickten' : '{-2,-1,0,1,2,3}',
                     'ytickten' : '{-2,-1,0,1,2,3}',
                     'tick label style' : '{font=\\footnotesize}',
                     'label style' : '{font=\\small}',
                     'ylabel style' : '{yshift=-7pt}'
                 },
                }


def scatter_alg(name, alg1, alg2, atr, domain_category, algo_to_latex):
    plot_options = {
            'axis_options' : axis_options[atr],
            'size': '5cm',
        'num_extra_diagonal_lines': 2,
        'algo_to_latex': algo_to_latex,
        'extra_preamble' : [r"\makeatletter",
                            r"\def\input@path{{../}{./}}",
                            r"\makeatother",
                            # r"\input{packages}",
                            # r"\input{macros}"
                            ]
    }
    return (os.path.join(TARGET_DIR + '-plots', name),
            NiceScatterPlotReport(filter_algorithm=[alg1, alg2], attributes=[atr], format='tex',
                                  # get_category=lambda run1, run2: run1[domain_category],
                                  **plot_options))

algo_to_latex_optimizations = {
# '5a714924cb50bcc494d05e9e1b6db2d14addfbfd-A_1__fulltransitions_____transeff-shr' : 'fulltr-transeff',
# '5a714924cb50bcc494d05e9e1b6db2d14addfbfd-A_1__fulltransitions_____labeleff-shr' : 'fulltr-labeleff'
}


scatter_plots = []
#Plots to show differences of optimizations versus not optimizations
# scatter_plots += [scatter_alg(f"optimizations-{atr}-{cname}", config1, config2, atr, "domain", algo_to_latex_optimizations)
#                  for atr in ['planner_time', 'sat_clauses']
#                  for (cname, config1,config2) in [('A1-chains-shr', 'A_1__chains_slf_________-shr', 'A_1__chains_slf____rcpol-shr'),
#                                            ('A1-seq-shr',      'A_1__seq____slf_________-shr',     'A_1__seq____slf____rcpol-shr')]]


# algo_to_latex_parallelism = {
#     'A_1__chains_slf____rcpol-shr' : 'Chains',
#     'A_1__seq____slf____rcpol-shr' : 'Sequential',
# }

# #Plots to show differences of parallelism 
# scatter_plots += [scatter_alg(f"paralellism-{atr}-{cname}-A1-shr", config1, config2, atr, "domain", algo_to_latex_parallelism)
#                  for atr in ['planner_time', 'sat_clauses', 'sat_variables', 'time_steps_with_label']#, 'plan_length']
#                  for (cname,config1,config2) in [('exists-vs-forall', 'A_1__seq____slf____rcpol-shr', 'A_1__chains_slf____rcpol-shr')]]





# Plots to show differences of optimizations versus not optimizations
scatter_plots += [scatter_alg(f"optimizations-{atr}-{cname}", config1, config2, atr, "domain", algo_to_latex_optimizations)
                 for atr in ['planner_time', 'sat_clauses', 'sat_variables', 'time_steps_with_label']
                 for (cname, config1,config2) in [('A1shrfull-transeff-vs-labeleff',
                                                   '5a714924cb50bcc494d05e9e1b6db2d14addfbfd-A_1__fulltransitions_____transeff-shr',
                                                   '5a714924cb50bcc494d05e9e1b6db2d14addfbfd-A_1__fulltransitions_____labeleff-shr'),
                                                  ('A1-shr-split-transeff-vs-labeleff',
                                                   '5a714924cb50bcc494d05e9e1b6db2d14addfbfd-A_1__splittransitions_slf_labeleff-shr',
                                                   '5a714924cb50bcc494d05e9e1b6db2d14addfbfd-A_1__splittransitions_slf_transeff-shr'),
                                                  ('A1-shr-full-vs-split',
                                                   '5a714924cb50bcc494d05e9e1b6db2d14addfbfd-A_1__fulltransitions_slf_transeff-shr',
                                                   '5a714924cb50bcc494d05e9e1b6db2d14addfbfd-A_1__splittransitions_slf_transeff-shr'),
                                                  ('A1-shr-split-slf',
                                                   '5a714924cb50bcc494d05e9e1b6db2d14addfbfd-A_1__splittransitions_____transeff-shr',
                                                   '5a714924cb50bcc494d05e9e1b6db2d14addfbfd-A_1__splittransitions_slf_transeff-shr'),
                                                  ]]

# algo_to_latex_parallelism = {
#     'A_1__chains_slf____rcpol-shr' : 'Chains',
#     'A_1__seq____slf____rcpol-shr' : 'Sequential',
# }

# #Plots to show differences of parallelism 
# scatter_plots += [scatter_alg(f"paralellism-{atr}-{cname}-A1-shr", config1, config2, atr, "domain", algo_to_latex_parallelism)
#                  for atr in ['planner_time', 'sat_clauses', 'sat_variables', 'time_steps_with_label']#, 'plan_length']
#                  for (cname,config1,config2) in [('exists-vs-forall', 'A_1__seq____slf____rcpol-shr', 'A_1__chains_slf____rcpol-shr')]]


add_nice_scatter_plot_step(exp, scatter_plots)

exp.run_steps()
