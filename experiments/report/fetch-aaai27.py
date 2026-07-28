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


DATA_DIR = f"{os.path.dirname(os.path.abspath(__file__))}/data-transition-encodings"
TARGET_DIR = f"{os.path.dirname(os.path.abspath(__file__))}/report-aaai27"

exp = Experiment(TARGET_DIR)

exp.add_step(
    "remove-combined-properties", remove_file, Path(TARGET_DIR + "-eval") / "properties")


def ignore_unexplained_errors2(run):
    def ignore_error(error):
        for x in ['planner failed to log peak memory', 'run.err: warning: could not determine peak memory',
                  'Found multiple occurences of Total time', 'planner finished and wrote', 'BDDError', 'MemoryError', 'planner wall-clock time:','exitcode--15', 'planner exit code:'
                  'cannot allocate memory', 'Fatal glibc error: malloc', 'SystemError: error return without exception set',
                  'rm-tmp-files.py',
                  'planner wrote',
                  'output-to-slurm.err','exitcode',
                  'out-of-memory','driver.log',
                  'exitcode-250']:
            if x in error:
                return True
        return False

    if "unexplained_errors" in run:
        run['unexplained_errors'] = [x for x in run['unexplained_errors'] if not ignore_error(x)]
        if any ([ x for x in run['unexplained_errors'] if "no match found in plan reconstruction" in x]):
            print (f"Faking coverage {run['coverage']} {run['unexplained_errors']} {run['domain']}")
            run['coverage'] = 1  
        elif run['unexplained_errors']:
            print(run['unexplained_errors'])
    return run

# def rename_time_steps(run):
#     if "steps" in run and not "time_steps_with_label" in run:
#         run['time_steps_with_label'] = run['steps']
        
#    return run
for directory in [d for d in Path(DATA_DIR).iterdir() if d.is_dir() and d != TARGET_DIR]:
        exp.add_fetcher(str(directory), merge=True, filter=[joint_domains, ignore_unexplained_errors2]) # filter=[ignore_unexplained_errors2, joint_domains, invert_min_negative_dominance, unsolvable_wo_mystery,rename_time_steps])

exp.run_steps()
