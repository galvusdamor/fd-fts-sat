#! /usr/bin/env python3

"""
Third IPC run of the transformation campaign (2026-10-21): the hybrid encoding.

bdd_sat(transition_factors=AUTO, transition_factor_ratio=5, transition_factor_min_share=0.7):
every factor gets full_transitions_sat's clauses except one whose transition encoding
explodes (estimated > 5x its BDDs and >= 70% of the whole), which keeps its BDDs. Second run:
bdd_sat needed ~9x the conflicts of full_transitions_sat at equal horizons and won only where
one factor exploded (openstacks, pathways). Locally on 86 IPC tasks (300 s) the hybrid solved
81 against 75, PAR-2 50.1 against 93.1, and equalled full_transitions_sat elsewhere -- with the
threshold tuned on those tasks.

  A_1__hyb_gc2_h2info-mtau   hybrid, merging + tau_baking, h2 information
  A_1__hyb_gc2_h2pre-mtau    hybrid, merging + tau_baking, h2 preprocessing

Fetched: the 2026-09-24 runs and the first two runs. Configurations, setup and caveats:
_trans_campaign.py.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "1011b8df74098730c0881a69bd3a24beb423ecd6"

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_3,
                        [("data/2026-09-24-ipc-bdd-A1-orders-eval", T.OLD_ALGORITHMS),
                         ("data/2026-10-20-ipc-bdd-transformations-eval",
                          {c: T.REVISION_RUN_1 for c in T.RUN_1}),
                         ("data/2026-10-21-ipc-bdd-transformations-2-eval",
                          {c: T.REVISION_RUN_2 for c in T.RUN_2})],
                        SCRIPT_NAME, pairs=T.PAIRS_RUN_3)
exp.run_steps()
