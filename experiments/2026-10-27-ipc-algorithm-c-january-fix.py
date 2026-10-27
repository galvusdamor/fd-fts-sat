#! /usr/bin/env python3

"""
Confirmation run at revision 03ba4e547: the January code plus only the two label_sat fixes (no
label reduction speedup; that is on branch transformation-speedup). IPC satisficing suite,
2026-10-27. Setting of the January forall-step run: -shr, no h2, 3500m, 12 runs at 3500M per
task, 30 min.

The forall-step rerun at f90f966c4 (2026-10-27-ipc-algorithm-c-january-2) solved 1392 instead of
the January 1431. Local A/B against 8337a1810 with the same kissat-p, then git bisect, found two
label_sat changes:
  * c88344cda (May 2026): forall-step chain helpers numbered per (factor, state) instead of all
    forward, then all backward; same clauses, kissat clearly worse. Fixed in f73c4c4f2.
  * 90d5534c4 (Feb 2026): the per-step label group table (one entry per factor and label) kept
    for every step; formula memory doubled on airport. Fixed in c8f7f7b5b.
Locally the fixed build solves the six lost hiking/airport tasks like the January build.

  CMit_chains_slf____rcpol_janfix-shr              the January forall-step configuration
                                                   (by_iteration(), 20 calls, memory_limit_mb=3500)
  C_1__fulltrans_transeff_tsort_jan_m2000-shr      TAV, tuned Algorithm C (start 1 x1.6, 4 calls,
  C_1__fulltrans_transeff_tsortstrict_jan_m2000-shr  gamma 0.5, memory_limit_mb=2000) with
  C_1__fulltrans_transeff_random_jan_m2000-shr       label_order_tsort(), ..._tsort(strict_enabling=true),
                                                   label_order_random()
TAV is not affected by either fix (no chains, no label group table).

Fetched from 2026-10-27-ipc-algorithm-c-january-2 (f90f966c4): the unfixed forall-step check and
TAV with the linear order at memory_limit_mb=2000.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "03ba4e54767aa473b6220f28d6be7221c9c3015e"
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_C_JANFIX,
                        [("data/2026-10-27-ipc-algorithm-c-january-2-eval", T.FETCH_C_JANFIX)],
                        SCRIPT_NAME, pairs=T.PAIRS_C_JANFIX,
                        searches=T.SEARCHES_C_JANFIX,
                        memory_limit=f"{MEMORY_MB}m", parallel_runs=12, runs_per_task=36)
exp.run_steps()
