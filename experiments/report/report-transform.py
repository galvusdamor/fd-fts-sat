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
from report_utils.better_solved_table import BetterSolvedTable 


TARGET_DIR = f"{os.path.dirname(os.path.abspath(__file__))}/report-transform"

exp = Experiment(TARGET_DIR)

# def change_domain(run):
#     run['domain'] = run['domain_category']
#     return run

exp.add_fetcher('/gpfs/home2/behnkeg/lab/fd-fts-sat/experiments/data/2026-01-13-new-transformer-missing-configs-eval',merge=True)
exp.add_fetcher('/gpfs/home2/behnkeg/lab/fd-fts-sat/experiments/data/2025-12-05-new-transformer-rerun-eval',merge=True)
exp.add_fetcher('/gpfs/home2/behnkeg/lab/fd-fts-sat/experiments/data/2025-12-08-classical-report-eval',merge=True)
exp.add_fetcher('/gpfs/home2/behnkeg/lab/fd-fts-sat/experiments/data/2025-12-04-remaining-configs-4-ff-lama-eval',merge=True)
exp.add_fetcher('/gpfs/home2/behnkeg/lab/fd-fts-sat/experiments/data/2025-11-12-new-transformations-eval', filter_algorithm=["CMit_slflpp_slf____rcpol-lr","CMit_chains_slf____rcpol-lr","ff-lr", "ff-fwb-lr-m100"],merge=True)
exp.add_fetcher('/gpfs/home2/behnkeg/lab/fd-fts-sat/experiments/data/2026-01-14-new-transformer-more-missing-configs-eval',merge=True)
exp.add_fetcher('/gpfs/home2/behnkeg/lab/fd-fts-sat/experiments/data/2026-01-14-new-transformer-more-more-missing-configs-eval',merge=True)
#exp.add_fetcher('/gpfs/home2/behnkeg/lab/fd-fts-sat/experiments/data/2026-01-14-new-transformer-more-missing-configs-eval',merge=True)

#exp.add_fetcher('/home/alvaro/projects/joao/fd-fts-sat/experiments/report/data-transform') # filter=[ignore_unexplained_errors2, joint_domains, invert_min_negative_dominance, unsolvable_wo_mystery,rename_time_steps])

#exp.add_fetcher('/home/alvaro/projects/joao/fd-fts-sat/experiments/report/report-eval') # filter=[ignore_unexplained_errors2, joint_domains, invert_min_negative_dominance, unsolvable_wo_mystery,rename_time_steps])

        
exp.add_report(AbsoluteReport(attributes=[
    "coverage",
    "cost",
    "planner_time",
    #'sat_variables', 'sat_clauses'
    #"unsolvable_wo_mystery"
], filter= [ignore_unexplained_errors], filter_algorithm=['ff-pref-ntr', 'ff-pref-shr', 'ff-pref-lr', 'ff-pref-fwb-lr-m100', "CMit_chains_slf____rcpol-fwb-lr-m100","CMit_chains_slf____rcpol-lr","CMit_chains_slf____rcpol-ntr","CMit_chains_slf____rcpol-shr",'MpC-A-C', 'MpC-E-C'],
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
    
exp.add_report(MyTable(['ntr','lr','shr','fwb-lr-m100'],['CMit_seq___', 'CMit_slflpp', 'CMit_chains', 'ff','ff-pref', 'MpC-A-C', 'MpC-E-C'], name_config), name="cov_table_shr", outfile="cov_table_shr.txt")



def name_config2 (alg):
    return alg

exp.add_report(BetterSolvedTable(['CMit_seq____slf____rcpol-shr', 'CMit_slflpp_slf____rcpol-shr', 'CMit_chains_slf____rcpol-shr', 'ff-shr','ff-pref-shr', 'MpC-A-C', 'MpC-E-C'], name_config2), name="better_table_shr", outfile="better_table_shr.txt")



# exp.add_report(TotalCoverageTable(), name="cov_table", outfile="cov_table.txt")


# exp.add_report(MyTable(['____________', '___________l', '_______rcpol', '____lg______', 'slf_________', 'slf________l', 'slf____rcpo_', 'slf____rcpol', 'slf_lg______', 'slf_lg_____l', 'slf_lg___p_l', 'slf_lg___pol', 'slf_lg__c__l', 'slf_lg__c_ol', 'slf_lg_r___l', 'slf_lg_r__ol', 'slf_lg_rcp__', 'slf_lg_rcp_l', 'slf_lg_rcpo_', 'slf_lg_rcpol'],['A_1__chains', 'A_1__seq___', 'A_1__slflpp', 'CMit_chains', 'CMit_seq___', 'CMit_slflpp'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column}_{row}-ntr"), name="cov_table_ntr", outfile="cov_table_ntr.txt")

# exp.add_report(MyTable(['____________', '___________l', '_______rcpol', '____lg______', 'slf_________', 'slf________l', 'slf____rcpo_', 'slf____rcpol', 'slf_lg______', 'slf_lg_____l', 'slf_lg___p_l', 'slf_lg___pol', 'slf_lg__c__l', 'slf_lg__c_ol', 'slf_lg_r___l', 'slf_lg_r__ol', 'slf_lg_rcp__', 'slf_lg_rcp_l', 'slf_lg_rcpo_', 'slf_lg_rcpol'],['A_1__chains', 'A_1__seq___', 'A_1__slflpp', 'CMit_chains', 'CMit_seq___', 'CMit_slflpp'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column}_{row}-shr"), name="cov_table_shr", outfile="cov_table_shr.txt")


# ## Explore subsets of RCPOL, but only for chains
# exp.add_report(MyTable(['slf________l', 'slf____rcpol', 'slf_lg_____l', 'slf_lg___p_l', 'slf_lg___pol', 'slf_lg__c__l', 'slf_lg__c_ol', 'slf_lg_r___l', 'slf_lg_r__ol', 'slf_lg_rcp_l', 'slf_lg_rcpol'],['A_1__chains_ntr', 'CMit_chains_ntr','A_1__chains_shr', 'CMit_chains_shr'], lambda row,column : f"{a938b5d2d5697b3c17c045512fe56824101e2b6c-column[:-4]}_{row}-{column[-3:]}"), name="cov_table_rcpol", outfile="cov_table_rcpol.txt")

# ## explore versions of parallelism (with and without shrinking)
# exp.add_report(MyTable(['slf_lg_rcpol_shr','slf____rcpol_shr','slf________l_shr','slf__________shr','slf_lg_rcpol_ntr','slf____rcpol_ntr','slf________l_ntr','slf__________ntr'],['A_1__chains', 'CMit_chains','A_1__slflpp', 'CMit_slflpp','A_1__seq___', 'CMit_seq___'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column}_{row[:-4]}-{row[-3:]}"), name="cov_table_shr_vs_ntr", outfile="cov_table_shr_vs_ntr.txt")

# ## explore RCPOL, but for the case without label group optimisation
# exp.add_report(MyTable(['slf________l', 'slf______p_l', 'slf______pol', 'slf_____c__l', 'slf_____c_ol', 'slf____r___l', 'slf____r__ol', 'slf____rcp_l', 'slf____rcpol'],['A_1__chains_ntr', 'CMit_chains_ntr','A_1__chains_shr', 'CMit_chains_shr'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column[:-4]}_{row}-{column[-3:]}"), name="cov_table_rcpol-no-lg", outfile="cov_table_rcpol-no-lg.txt")

# ### explore impact of self-loop optimisation in seq (only applicable there)
# exp.add_report(MyTable(['____________', '___________l', '_______rcpol', '____lg______', 'slf_________', 'slf________l', 'slf____rcpo_', 'slf____rcpol', 'slf_lg______', 'slf_lg_____l', 'slf_lg_rcpo_', 'slf_lg_rcpol'],['A_1__seq____shr', 'CMit_seq____shr', 'A_1__seq____ntr', 'CMit_seq____ntr'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column[:-4]}_{row}-{column[-3:]}"), name="cov_table_slf", outfile="cov_table_slf.txt")


# #####################################################################
# ### rcpo table
# exp.add_report(MyTable(['slf________l', 'slf______p_l', 'slf______pol', 'slf_____c__l', 'slf_____c_ol', 'slf____r___l', 'slf____r__ol', 'slf____rcp_l', 'slf____rcpol', 'slf_lg_____l', 'slf_lg___p_l', 'slf_lg___pol', 'slf_lg__c__l', 'slf_lg__c_ol', 'slf_lg_r___l', 'slf_lg_r__ol', 'slf_lg_rcp_l', 'slf_lg_rcpol'],['A_1__chains_ntr','A_1__chains_shr', 'CMit_chains_ntr', 'CMit_chains_shr'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column[:-4]}_{row}-{column[-3:]}"), name="g-cov_table_rcpol", outfile="g-cov_table_rcpol.txt")

# exp.add_report(MyTable(['slf________l', 'slf______p_l', 'slf______pol', 'slf_____c__l', 'slf_____c_ol', 'slf____r___l', 'slf____r__ol', 'slf____rcp_l', 'slf____rcpol'],['A_1__chains_ntr','A_1__chains_shr', 'CMit_chains_ntr', 'CMit_chains_shr'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column[:-4]}_{row}-{column[-3:]}"), name="cov_table_rcpol-no-lg", outfile="g-cov_table_rcpol-no-lg.txt")

# exp.add_report(MyTable(['slf_lg_rcpol_shr','slf____rcpol_shr','slf________l_shr','slf__________shr','slf_lg_rcpol_ntr','slf____rcpol_ntr','slf________l_ntr','slf__________ntr'],['A_1__chains','A_1__slflpp','A_1__seq___', 'CMit_chains', 'CMit_slflpp', 'CMit_seq___'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column}_{row[:-4]}-{row[-3:]}"), name="g-cov_table_shr_vs_ntr", outfile="g-cov_table_parallel_shr_vs_ntr.txt")


# ###
# exp.add_report(MyTable(['____________', '___________l', '_______rcpol', '_______rcpo_', '____lg______', '____lg_____l', '____lg_rcpol', '____lg_rcpo_', 'slf_________', 'slf________l', 'slf____rcpo_', 'slf____rcpol', 'slf_lg______', 'slf_lg_____l', 'slf_lg_rcpo_', 'slf_lg_rcpol'],['A_1__seq____shr', 'A_1__seq____ntr', 'CMit_seq____shr', 'CMit_seq____ntr'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column[:-4]}_{row}-{column[-3:]}"), name="g-cov_table_slf", outfile="g-cov_table_slf.txt")

# ### base table
# exp.add_report(MyTable(['____________', '___________l', '_______rcpol', '____lg______', 'slf_________', 'slf________l', 'slf____rcpo_', 'slf____rcpol', 'slf_lg______', 'slf_lg_____l', 'slf_lg_rcpo_', 'slf_lg_rcpol'],['A_1__chains', 'A_1__seq___', 'A_1__slflpp', 'CMit_chains', 'CMit_seq___', 'CMit_slflpp'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column}_{row}-ntr"), name="g-cov_table_ntr", outfile="g-cov_table_ntr.txt")
# exp.add_report(MyTable(['____________', '___________l', '_______rcpol', '____lg______', 'slf_________', 'slf________l', 'slf____rcpo_', 'slf____rcpol', 'slf_lg______', 'slf_lg_____l', 'slf_lg_rcpo_', 'slf_lg_rcpol'],['A_1__chains', 'A_1__seq___', 'A_1__slflpp', 'CMit_chains', 'CMit_seq___', 'CMit_slflpp'], lambda row,column : f"a938b5d2d5697b3c17c045512fe56824101e2b6c-{column}_{row}-shr"), name="g-cov_table_shr", outfile="g-cov_table_shr.txt")




def scatter_alg(name, alg1, alg2, atr, domain_category, algo_to_latex):
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
                                  # get_category=lambda run1, run2: run1[domain_category],
                                  **plot_options))



('a938b5d2d5697b3c17c045512fe56824101e2b6c-A_1__chains_slf____rcpo_-shr', 'a938b5d2d5697b3c17c045512fe56824101e2b6c-A_1__chains_slf_________-shr')


algo_to_latex = {
    'MpC-A-C' : 'MpC-A-C',
    'ff-pref-shr' : 'ff-pref-shr',
    'CMit_chains_slf____rcpol-shr' : 'CMit-chains-slf-rcpol-shr',
}


#Plots to show differences of between general approaches (our SAT, MpC and FF-pref)
scatter_plots = [scatter_alg(f"{atr}-{config1}-{config2}", config1, config2, atr, "domain_category", algo_to_latex)
                 for atr in ['planner_time']
                 for (config1,config2) in [('CMit_chains_slf____rcpol-shr',
                                            'ff-pref-shr')]]

scatter_plots += [scatter_alg(f"{atr}-{config1}-{config2}", config1, config2, atr, "domain_category", algo_to_latex)
                 for atr in ['planner_time']
                 for (config1,config2) in [('CMit_chains_slf____rcpol-shr',
                                            'MpC-A-C')]]

scatter_plots += [scatter_alg(f"{atr}-{config1}-{config2}", config1, config2, atr, "domain_category", algo_to_latex)
                 for atr in ['planner_time']
                 for (config1,config2) in [('CMit_chains_slf____rcpol-shr',
                                            'MpC-E-C')]]

scatter_plots += [scatter_alg(f"{atr}-{config1}-{config2}", config1, config2, atr, "domain_category", algo_to_latex)
                 for atr in ['planner_time']
                 for (config1,config2) in [('ff-pref-shr',
                                            'MpC-E-C')]]

scatter_plots += [scatter_alg(f"{atr}-{config1}-{config2}", config1, config2, atr, "domain_category", algo_to_latex)
                 for atr in ['planner_time']
                 for (config1,config2) in [('ff-pref-shr',
                                            'MpC-A-C')]]
 
add_nice_scatter_plot_step(exp, scatter_plots)


exp.run_steps()
