#! /usr/bin/env python3

"""
Rintanen's Algorithm C (rintanen()) with the parameters of the offline tuning, IPC satisficing
suite, 2026-10-26.

Parameters (rintanen campaign, potential_final.txt of 2026-10-03; simulated on algorithm-A
profiles of bdd_sat goal-chains v2 + -shr + h2 preprocessing, 1,466 tasks, 300 s):
length_strategy by_iteration(start_length=1, multiplier=1.6, strictly_increasing=true),
max_parallel_calls=4, gamma=0.5 (Algorithm C's geometric time share); coverage 1035 against 994
for algorithm A. The interval made no difference (0.05-5 s), 1 s here.

Encodings, all with -shr and h2 preprocessing as in the tuning:
  C_1__fulltrans_transeff-shr    full_transitions_sat, linear label order, transition variables
                                 in the effect clauses (use_labels_in_effects_constraints=false)
  C_1__fulltrans_labeleff-shr    the same with label variables in the effect clauses
  C_1__chains_slf____rcpol-shr   label_sat CHAINS_PARALLEL (forall-step, best label encoding)
  C_1__seq____slf____rcpol-shr   label_sat SEQUENTIAL

Memory: 1.75 GB = 1.75e9 bytes = 1669 MiB per run (driver limit and rintanen's own
memory_limit_mb), 24 runs at a time on the 24 cores of a task (1750M per core leaves ~80 MiB
per run for lab and the driver), 72 runs per task. Note that rintanen runs up to 4 SAT calls
in threads; glibc reserves address space per thread, which counts against the driver's
address-space limit.

Fetched for comparison: the 2026-09-24 IPC runs (algorithm A, -shr, h2 preprocessing, 1600 MB),
among them A_1__chains_slf____rcpol-shr and A_1__fulltransitions_slf_transeff-shr.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "f90f966c4ba59dc1d8a44ecb62e2a154a19f18ea"
MEMORY_MB = 1669

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_C,
                        [("data/2026-09-24-ipc-bdd-A1-orders-eval", T.OLD_ALGORITHMS)],
                        SCRIPT_NAME, pairs=T.PAIRS_C,
                        search=T.SEARCH_C.replace("MEMORY_MB", str(MEMORY_MB)),
                        memory_limit=f"{MEMORY_MB}m", parallel_runs=24, runs_per_task=72)
exp.run_steps()
