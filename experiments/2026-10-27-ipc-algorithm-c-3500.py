#! /usr/bin/env python3

"""
Algorithm C (tuned rintanen()) with full_transitions_sat TAV and TAC at 3500 MB, IPC satisficing
suite, 2026-10-27.

2026-10-26-ipc-algorithm-c.py ran them at 1669 MiB (driver limit and rintanen's
memory_limit_mb). The January forall-step Algorithm C run it is compared against
(CMit_chains_slf____rcpol-shr, 1431 solved) had 3500m for both, 12 runs at 3500M per task.
On the tasks that run solves and TAV/TAC do not, about 60% of the TAV/TAC failures are
memory-outs. This rerun changes only the memory: same revision, same parameters, h2
preprocessing as in the 1669 MiB run (the January run had none).

  C_1__fulltrans_transeff_3500-shr   TAV (transition variables in the effect clauses)
  C_1__fulltrans_labeleff_3500-shr   TAC (label variables in the effect clauses)

12 runs at a time on the 24 cores of a task (42000M / 12 = 3500M per run, as in January), 36 runs
per task (3 waves, inside the 02:30:00 task limit). Fetched: TAV/TAC at 1669 MiB from
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
                        T.RUN_C_3500,
                        [("data/2026-10-26-ipc-algorithm-c-eval",
                          {c: REVISION for c in ["C_1__fulltrans_transeff-shr",
                                                 "C_1__fulltrans_labeleff-shr"]})],
                        SCRIPT_NAME, pairs=T.PAIRS_C_3500,
                        search=T.SEARCH_C.replace("MEMORY_MB", str(MEMORY_MB)),
                        memory_limit=f"{MEMORY_MB}m", parallel_runs=12, runs_per_task=36)
exp.run_steps()
