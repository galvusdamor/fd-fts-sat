#! /usr/bin/env python3

"""
forall-step (label_sat CHAINS_PARALLEL) with the January Algorithm C schedule (by_iteration(),
20 parallel calls, memory_limit_mb=3500), IPC satisficing suite, 2026-11-11. Setting of the
January runs: -shr, no h2, 3500m, 12 runs at a time, 36 per SLURM task, 30 min.

  CMit_chains_slf____rcpol_janspeed-shr   the configuration of 2026-10-27-ipc-algorithm-c-january-fix
                                          at revision d7575373f (branch unified-encoding): both
                                          label_sat fixes plus the exact label reduction speedup
                                          (b58524600, reapplied in 384c37284); label_sat's formulas
                                          are bit-identical to the January fix (StepEncoding)

The only difference to the January fix run is the faster -shr transformation; it matters where the
transformation took a large part of the 30 min. Fetched: the January fix (03ba4e547, without the
speedup) and the sigma runs of 2026-11-10-ipc-sigma-A1-C2 (same revision).
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "d7575373ff6ef6bdacfd012f533214b53f289a08"
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_C_JANSPEED, T.FETCH_C_JANSPEED, SCRIPT_NAME, pairs=T.PAIRS_C_JANSPEED,
                        searches=T.SEARCHES_C_JANSPEED, memory_limit=f"{MEMORY_MB}m",
                        parallel_runs=12, runs_per_task=36)
exp.run_steps()
