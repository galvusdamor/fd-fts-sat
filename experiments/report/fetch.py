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


DATA_DIR = f"{os.path.dirname(os.path.abspath(__file__))}/data"
TARGET_DIR = f"{os.path.dirname(os.path.abspath(__file__))}/report"

exp = Experiment(TARGET_DIR)

exp.add_step(
    "remove-combined-properties", remove_file, Path(TARGET_DIR) / "properties")

for directory in [d for d in Path(DATA_DIR).iterdir() if d.is_dir() and d != TARGET_DIR]:
        exp.add_fetcher(str(directory), merge=True, filter=[ignore_unexplained_errors, joint_domains, invert_min_negative_dominance, unsolvable_wo_mystery])

exp.run_steps()
