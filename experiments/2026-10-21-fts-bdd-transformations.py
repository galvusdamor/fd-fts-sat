#! /usr/bin/env python3

"""
FTS benchmarks (common_setup.FTS_SUITE, .sas input), 2026-10-21: every configuration of the
transformation campaign without h2 -- h2 needs the translator's output and cannot run here.

  A_1__bdd_gc2-shr           bdd_sat, -shr (the 2026-09-24 configuration at 2050 MB)
  A_1__fulltrans_gc2-shr     full_transitions_sat, -shr
  A_1__bdd_gc2-mtau          bdd_sat, merging + tau_baking
  A_1__bdd_gc2-mtaucl        bdd_sat, merging + tau_baking + label_closure
  A_1__fulltrans_gc2-mtau    full_transitions_sat, merging + tau_baking
  A_1__hyb_gc2-mtau          hybrid (bdd_sat transition_factors=AUTO), merging + tau_baking

All goal-chains v2, 30 min / 2050 MB. The 2026-09-24 FTS runs (-shr, 1600 MB) are fetched.
Configurations, setup and caveats: _trans_campaign.py.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["FTS_BENCHMARKS"]
REVISION = "1011b8df74098730c0881a69bd3a24beb423ecd6"

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.FTS_SUITE,
                        T.RUN_FTS,
                        [("data/2026-09-24-fts-bdd-A1-orders-eval", T.OLD_ALGORITHMS)],
                        SCRIPT_NAME, pairs=T.PAIRS_FTS)
exp.run_steps()
