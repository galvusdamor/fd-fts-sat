#! /usr/bin/env python3

"""
Second run in the setting of the January forall-step Algorithm C run (CMit_chains_slf____rcpol-shr,
1431 solved): -shr, no h2 preprocessing, 3500m driver limit, 12 runs at 3500M per task, 30 min,
IPC satisficing suite, 2026-10-27.

  CMit_chains_slf____rcpol_jan-shr        the January configuration itself (label_sat CHAINS_PARALLEL,
                                          rintanen with by_iteration(), 20 calls, memory_limit_mb=3500,
                                          scheduler_interval=1): checks the current revision and setup
                                          against the 1431
  C_1__fulltrans_{transeff,labeleff}_jan_m2000-shr
                                          TAV/TAC with the tuned parameters (start 1 x1.6, 4 calls,
                                          gamma 0.5) and rintanen's own memory_limit_mb=2000, i.e.
                                          formula generation pauses at 1000 MB. In
                                          2026-10-27-ipc-algorithm-c-january.py (memory_limit_mb=3500)
                                          466 of the TAV runs ended in memory-outs.

Fetched: the four configurations of 2026-10-27-ipc-algorithm-c-january.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "f90f966c4ba59dc1d8a44ecb62e2a154a19f18ea"   # as in the first January-setting run
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_C_JAN2,
                        [("data/2026-10-27-ipc-algorithm-c-january-eval",
                          {c: REVISION for c in T.RUN_C_JAN})],
                        SCRIPT_NAME, pairs=T.PAIRS_C_JAN2,
                        searches=T.SEARCHES_C_JAN2,
                        memory_limit=f"{MEMORY_MB}m", parallel_runs=12, runs_per_task=36)
exp.run_steps()
