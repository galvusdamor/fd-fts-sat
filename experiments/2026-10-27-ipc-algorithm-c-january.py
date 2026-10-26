#! /usr/bin/env python3

"""
Tuned Algorithm C with full_transitions_sat TAV and TAC in the setting of the January
forall-step Algorithm C run (CMit_chains_slf____rcpol-shr, 1431 solved), IPC satisficing
suite, 2026-10-27.

January setting: -shr, no h2 preprocessing, 3500m driver limit and rintanen memory_limit_mb=3500,
12 runs at 3500M per task, 30 min. 2026-10-26-ipc-algorithm-c.py differed in two ways: 1669 MiB,
and h2 preprocessing. About 60% of the TAV/TAC failures on the tasks only the January run solves
were memory-outs.

Two tuned Algorithm C versions (scheduler_interval=1 in both):
  C_1__fulltrans_{transeff,labeleff}_jan-shr     the tuning result: by_iteration(start_length=1,
                                                 multiplier=1.6, strictly_increasing=true),
                                                 max_parallel_calls=4, gamma=0.5
  C_g1p6i4_fulltrans_{transeff,labeleff}_jan-shr round-robin: by_increment(start_length=4,
                                                 increment=4), max_parallel_calls=6, gamma=1
                                                 (increment: see SEARCH_C_G1 in _trans_campaign)
transeff = TAV (transition variables in the effect clauses), labeleff = TAC; label_order_linear().

12 runs at a time on the 24 cores of a task (42000M / 12 = 3500M per run, as in January), 36 runs
per task (3 waves, inside the 02:30:00 task limit). Fetched: TAV/TAC at 1669 MiB with h2 from
2026-10-26-ipc-algorithm-c.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "f90f966c4ba59dc1d8a44ecb62e2a154a19f18ea"   # as in the 1669 MiB run
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_C_JAN,
                        [("data/2026-10-26-ipc-algorithm-c-eval",
                          {c: REVISION for c in ["C_1__fulltrans_transeff-shr",
                                                 "C_1__fulltrans_labeleff-shr"]})],
                        SCRIPT_NAME, pairs=T.PAIRS_C_JAN,
                        search=T.SEARCH_C.replace("MEMORY_MB", str(MEMORY_MB)),
                        searches={c: s.replace("MEMORY_MB", str(MEMORY_MB))
                                  for c, s in T.SEARCHES_C_JAN.items()},
                        memory_limit=f"{MEMORY_MB}m", parallel_runs=12, runs_per_task=36)
exp.run_steps()
