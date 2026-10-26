#! /usr/bin/env python3

"""
The January forall-step Algorithm C configuration (CMit_chains_slf____rcpol-shr, 1431 solved) at
revision c8f7f7b5b, IPC satisficing suite, 2026-10-28. Same setting as
2026-10-27-ipc-algorithm-c-january-2.py: -shr, no h2, 3500m, 12 runs at 3500M per task, 30 min.

That run reproduced only 1392 at f90f966c4. Two label_sat changes since January caused it (local
A/B against 8337a1810 with the same kissat-p, then git bisect):
  * c88344cda (May 2026): the forall-step chain helpers were numbered per (factor, state)
    instead of all forward, then all backward; same clauses, kissat clearly worse (hiking:
    crash at 3.4 GB instead of a plan at 2.5 GB). Fixed in f73c4c4f2 (old order is the default
    again, chains_interleaved=true selects the other).
  * 90d5534c4 (Feb 2026): the per-step label group table was kept for every step instead of
    freed; one entry per (factor, label), doubling formula memory on airport. Fixed in c8f7f7b5b.
Locally the fixed build solves the six lost hiking/airport tasks like the January build.

  CMit_chains_slf____rcpol_janfix-shr   the January configuration at c8f7f7b5b

Fetched: CMit_chains_slf____rcpol_jan-shr (f90f966c4) from 2026-10-27-ipc-algorithm-c-january-2.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "c8f7f7b5b54ab963b0504a3b9d8333d0f1ce6141"
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_C_JANFIX,
                        [("data/2026-10-27-ipc-algorithm-c-january-2-eval",
                          {"CMit_chains_slf____rcpol_jan-shr": "f90f966c4ba59dc1d8a44ecb62e2a154a19f18ea"})],
                        SCRIPT_NAME, pairs=T.PAIRS_C_JANFIX,
                        searches=T.SEARCHES_C_JANFIX,
                        memory_limit=f"{MEMORY_MB}m", parallel_runs=12, runs_per_task=36)
exp.run_steps()
