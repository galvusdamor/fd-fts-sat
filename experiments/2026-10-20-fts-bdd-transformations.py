#! /usr/bin/env python3

"""
bdd_sat on transformed FTSs (merging + tau_baking), 2026-10-20: FTS benchmarks (common_setup.FTS_SUITE, .sas input). Without the h2 configuration:
h2 needs the translator's output. The 2026-09-24 FTS runs (-shr, 1600 MB, no h2) are fetched.

Configurations, setup and caveats: _trans_campaign.py.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["FTS_BENCHMARKS"]
REVISION = "52efe061d80dcec607a31ba8224c0c7634f2fb1d"

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.FTS_SUITE,
                        [c for c in T.RUN_1 if T.CONFIGS[c][2] is None],
                        [("data/2026-09-24-fts-bdd-A1-orders-eval", T.OLD_ALGORITHMS)],
                        SCRIPT_NAME, pairs=T.PAIRS_RUN_1)
exp.run_steps()
