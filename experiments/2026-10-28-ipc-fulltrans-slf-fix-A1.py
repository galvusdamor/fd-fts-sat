#! /usr/bin/env python3

"""
full_transitions_sat (TAV and TAC) with the self-loop fix of 4fd06976f, algorithm A
(sat with one_by_one()), IPC satisficing suite, 2026-10-28. Setting of the TAV order checks
(2026-10-27-ipc-tav-orders-A1.py): -shr, no h2, 3500m, 12 runs at 3500M per task, 30 min,
label_order_linear().

The fix: with use_self_loop_optimisation, the r2 chains ("the factor is no longer in this state")
lacked their initial condition, so a mixed label could claim an unencoded self-loop in a state
the factor was not in. development-and-testing c41eca3eb tried to fix the same with a clause on
the first label being executed (over-constraining) and an out-of-bounds access for factors with
a single relevant label; this run uses the corrected version (also available as a patch
relative to c41eca3eb).

  A_1__fulltrans_transeff_slf_fix-shr     TAV, self-loop optimisation
  A_1__fulltrans_transeff_noslf_fix-shr   TAV, no self-loop optimisation (fix not involved)
  A_1__fulltrans_labeleff_slf_fix-shr     TAC, self-loop optimisation
  A_1__fulltrans_labeleff_noslf_fix-shr   TAC, no self-loop optimisation (fix not involved)

Local check (first tasks of 30 IPC domains, 120 s): no invalid plan before or after the fix,
identical horizons on all 27 tasks both versions solve; grid p02 gets slower at the same horizon.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "4fd06976f404c9538b8cae28d0991421db549226"
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_A_FULLTRANS_FIX, [],
                        SCRIPT_NAME, pairs=T.PAIRS_A_FULLTRANS_FIX,
                        memory_limit=f"{MEMORY_MB}m", parallel_runs=12, runs_per_task=36)
exp.run_steps()
