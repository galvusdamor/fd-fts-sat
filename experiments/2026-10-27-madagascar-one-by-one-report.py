#! /usr/bin/env python3

"""
Report only, no runs: Madagascar (MpC) one-by-one from the existing eval directory of
2025-11-06-madagascar.py (IPC satisficing suite, 30 min, 3500 MB), next to the fixed forall-step
Algorithm C of 2026-10-27-ipc-algorithm-c-january-fix.

  MpC-no-parallel-seq   -P 0 -S 1 -A 1   sequential, one length at a time (step 1)
  MpC-A-seq             -P 1 -S 1 -A 1   forall-step, one-by-one
  MpC-E-seq             -P 2 -S 1 -A 1   exists-step, one-by-one
  MpC-A-C, MpC-E-C      -P 1 / -P 2      Madagascar's default (algorithm C), for reference

Steps: 1 fetch-mpc (data/2025-11-06-madagascar-eval), 2 fetch-forall
(data/2026-10-27-ipc-algorithm-c-january-fix-eval), 3 report. If step 1 finds no properties for
the -seq configurations, they were never run and need a new run script.
"""

import os

from lab.experiment import Experiment
from score_report import AbsoluteReport

import common_setup
import filters

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
MPC = ["MpC-no-parallel-seq", "MpC-A-seq", "MpC-E-seq", "MpC-A-C", "MpC-E-C"]
FORALL = "CMit_chains_slf____rcpol_janfix-shr"

exp = Experiment()
exp.add_fetcher("data/2025-11-06-madagascar-eval", filter_algorithm=MPC, name="fetch-mpc", merge=True)
exp.add_fetcher("data/2026-10-27-ipc-algorithm-c-january-fix-eval",
                filter=[filters.remove_revision], filter_algorithm=[FORALL], name="fetch-forall", merge=True)
exp.add_report(AbsoluteReport(attributes=common_setup.ATTRIBUTES,
                              filter=[filters.filter_madagascar_known_unexplained_errors],
                              filter_algorithm=MPC + [FORALL]),
               outfile=f"{SCRIPT_NAME}.html")
exp.run_steps()
