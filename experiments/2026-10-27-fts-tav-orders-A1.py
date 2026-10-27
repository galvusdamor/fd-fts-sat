#! /usr/bin/env python3

"""
Algorithm A (sat with one_by_one()) for TAV (full_transitions_sat, transition variables in the
effect clauses) with three label orders, FTS benchmarks (common_setup.FTS_SUITE, .sas input, no
translator), 2026-10-27. Revision 03ba4e547 (label_sat fixes only, no label reduction speedup),
the setting of the January-fix confirmation run: -shr, no h2, 3500m, 12 runs at 3500M per task,
30 min.

  A_1__fulltrans_transeff_tsort_jan-shr         label_order_tsort()
  A_1__fulltrans_transeff_tsortstrict_jan-shr   label_order_tsort(strict_enabling=true)
  A_1__fulltrans_transeff_random_jan-shr        label_order_random()

Fetched for orientation: A_1__fulltransitions_slf_transeff-shr (TAV, linear order) from the
2026-09-24 run -- note the 09-24 FTS baseline had 1600 MB.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["FTS_BENCHMARKS"]
REVISION = "03ba4e54767aa473b6220f28d6be7221c9c3015e"
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.FTS_SUITE,
                        T.RUN_A_JANFIX,
                        [("data/2026-09-24-fts-bdd-A1-orders-eval", T.FETCH_A_JANFIX)],
                        SCRIPT_NAME, pairs=T.PAIRS_A_JANFIX, time_attribute="total_time",
                        memory_limit=f"{MEMORY_MB}m", parallel_runs=12, runs_per_task=36)
exp.run_steps()
