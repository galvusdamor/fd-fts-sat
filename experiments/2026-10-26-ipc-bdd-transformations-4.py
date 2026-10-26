#! /usr/bin/env python3

"""
Fourth IPC run of the transformation campaign (2026-10-26): label_union.

label_union() (task_transformation_label_union.h) replaces a group of labels that each move
exactly one, different factor, have no precondition in each other's factors and the same
transitions everywhere else by one label firing any subset of them -- more branching, no factor
merged, no parallelism lost. Locally on 395 Autoscale tasks (300 s) it helped every encoder:
hybrid with h2 preprocessing 325 -> 330, hybrid with h2 information 322 -> 324, hybrid without
h2 319 -> 322, bdd_sat 307 -> 309 (BDD.md 5.11). Each encoder in its best IPC configuration
(third run), now + label_union:

  A_1__hyb_gc2_h2pre-mtauun        hybrid, merging + tau_baking + union, h2 preprocessing (1377)
  A_1__bdd_gc2_h2info-mtauun       bdd_sat, merging + tau_baking + union, h2 information  (1337)
  A_1__fulltrans_gc2_h2pre-mtauun  full_transitions_sat, merging + tau_baking + union, h2 pre (1363)

Fetched: the 2026-09-24 runs and the first three runs. Configurations, setup and caveats:
_trans_campaign.py.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "f90f966c4ba59dc1d8a44ecb62e2a154a19f18ea"

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_4,
                        [("data/2026-09-24-ipc-bdd-A1-orders-eval", T.OLD_ALGORITHMS),
                         ("data/2026-10-20-ipc-bdd-transformations-eval",
                          {c: T.REVISION_RUN_1 for c in T.RUN_1}),
                         ("data/2026-10-21-ipc-bdd-transformations-2-eval",
                          {c: T.REVISION_RUN_2 for c in T.RUN_2}),
                         ("data/2026-10-21-ipc-bdd-transformations-3-eval",
                          {c: T.REVISION_RUN_3 for c in T.RUN_3})],
                        SCRIPT_NAME, pairs=T.PAIRS_RUN_4)
exp.run_steps()
