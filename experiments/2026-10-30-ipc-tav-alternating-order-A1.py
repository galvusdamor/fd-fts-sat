#! /usr/bin/env python3

"""
TAV (full_transitions_sat with the self-loop optimisation) with alternating label orders, algorithm
A (sat with one_by_one()), IPC satisficing suite, 2026-10-30. January setting: -shr, no h2, 3500m,
12 runs at 3500M per task, 30 min. One configuration (replaces the six of
2026-10-29-ipc-tav-alternating-orders-A1.py, which need not be run):

  A_1__fulltrans_transeff_slf_alt-shr   odd time steps the input order (label_order_linear()),
                                        even ones its reverse (alternate_label_orders=true)

Compared against TAV with the input order in the same setting, fetched from the self-loop fix run
2026-10-28-ipc-fulltrans-slf-fix-A1 (A_1__fulltrans_transeff_slf_fix-shr, revision 4fd06976f; the
revision here only adds alternate_label_orders, default off).

Note on horizons: alternating with horizon 2m is Symbolic Pattern Planning's pattern
(order; order^-1) concatenated m times (Cardellini, Giunchiglia, Maratea, AIJ 2026), so compare
coverage and time, not the number of steps.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "688d5ba68b30a8b9b787f7889b3b0ee3fcc022fb"
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_A_ALT_TAV,
                        [("data/2026-10-28-ipc-fulltrans-slf-fix-A1-eval", T.FETCH_A_ALT_TAV)],
                        SCRIPT_NAME, pairs=T.PAIRS_A_ALT_TAV,
                        memory_limit=f"{MEMORY_MB}m", parallel_runs=12, runs_per_task=36)
exp.run_steps()
