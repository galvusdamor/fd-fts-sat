#! /usr/bin/env python3

"""
forall-step (label_sat CHAINS_PARALLEL, chains_slf____rcpol) with algorithm A (sat with
one_by_one()), IPC satisficing suite, 2026-10-29. Exactly the setting and revision of
2026-10-28-ipc-fulltrans-slf-fix-A1.py (-shr, no h2, 3500m, 12 runs at 3500M per task, 30 min,
revision 4fd06976f, which contains the label_sat fixes f73c4c4f2 and c8f7f7b5b), so that the
size plots (2026-10-29-ipc-size-plots-4.py) compare forall and TAV on the same footing.

The forall run used so far (2025-11-07) predates the ENCSTAT line, so it has no per-step sizes;
the recent forall reruns were Algorithm C, which does not print ENCSTAT.

  A_1__chains_slf____rcpol_fix-shr   forall-step
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "4fd06976f404c9538b8cae28d0991421db549226"
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_A_FORALL_FIX, [],
                        SCRIPT_NAME, pairs=[],
                        memory_limit=f"{MEMORY_MB}m", parallel_runs=12, runs_per_task=36)
exp.run_steps()
