#! /usr/bin/env python3

"""
Second IPC run of the transformation campaign (2026-10-21), IPC satisficing suite.

After the first run (2026-10-20-ipc-bdd-transformations.py): h2 information helped bdd_sat
(+66/-2), full_transitions_sat had not been run with it, and every fetched -shr run had
1600 MB and h2 preprocessing. Five configurations, all goal-chains v2, 2050 MB, 30 min:

  A_1__fulltrans_gc2_h2info-mtau   full_transitions_sat, merging + tau_baking, h2 information
  A_1__fulltrans_gc2_h2pre-mtau    full_transitions_sat, merging + tau_baking, h2 preprocessing
  A_1__bdd_gc2_h2info-shr          bdd_sat, -shr, h2 information
  A_1__bdd_gc2_h2pre-shr           bdd_sat, -shr, h2 preprocessing (the 09-24 config at 2050 MB)
  A_1__fulltrans_gc2_h2pre-shr     full_transitions_sat, -shr, h2 preprocessing

Fetched: the first run (4 configurations) and the 2026-09-24 runs. The scatter pairs
(T.PAIRS_RUN_2) each differ in exactly one respect: transformation, h2 mode, memory or
encoder. Configurations, setup and caveats: _trans_campaign.py.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "7614c44e4410534034b560bd98b7765401b5d917"

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_2,
                        [("data/2026-09-24-ipc-bdd-A1-orders-eval", T.OLD_ALGORITHMS),
                         ("data/2026-10-20-ipc-bdd-transformations-eval",
                          {c: T.REVISION_RUN_1 for c in T.RUN_1})],
                        SCRIPT_NAME, pairs=T.PAIRS_RUN_2)
exp.run_steps()
