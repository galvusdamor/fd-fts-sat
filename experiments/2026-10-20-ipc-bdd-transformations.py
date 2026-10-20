#! /usr/bin/env python3

"""
bdd_sat on transformed FTSs (merging + tau_baking), 2026-10-20: IPC satisficing suite (common_setup.DEFAULT_SATISFICING_SUITE), all four configurations;
the 2026-09-24 IPC runs (-shr, 1600 MB, h2 preprocessing) are fetched for comparison.

Configurations, setup and caveats: _trans_campaign.py.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "52efe061d80dcec607a31ba8224c0c7634f2fb1d"

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_1,
                        [("data/2026-09-24-ipc-bdd-A1-orders-eval", T.OLD_ALGORITHMS)],
                        SCRIPT_NAME, pairs=T.PAIRS_RUN_1)
exp.run_steps()
