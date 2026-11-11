#! /usr/bin/env python3

"""
forall-step (label_sat CHAINS_PARALLEL) with the January Algorithm C schedule (by_iteration(),
20 parallel calls, memory_limit_mb=3500) and h2 preprocessing, IPC satisficing suite, 2026-11-11.
3500m, 12 runs at a time, 36 per SLURM task, 30 min. Revision d7575373f (branch unified-encoding):
both label_sat fixes and the exact label reduction speedup; label_sat's formulas bit-identical to
the January fix.

  CMit_chains_slf____rcpol_janspeed_h2pre-shr      h2 preprocessing + -shr
  CMit_chains_slf____rcpol_janspeed_h2pre-mtauun   h2 preprocessing + merging + tau_baking +
                                                   label_union

Fetched: sigma (per-factor blocks + shift, algorithm C with 2 calls) with the same two
transformations from 2026-11-10-ipc-sigma-C2-h2; forall-step with h2 from 2026-10-26-ipc-algorithm-c
(f90f966c4: no speedup, no label_sat fixes, tuned schedule, 1669 MiB -- orientation only); and
forall-step without h2 at this revision from 2026-11-11-ipc-forall-C-jan-speedup (its fetch step
fails until that experiment has run; run the report steps after it).
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "d7575373ff6ef6bdacfd012f533214b53f289a08"
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_C_JANSPEED_H2, T.FETCH_C_JANSPEED_H2, SCRIPT_NAME, pairs=T.PAIRS_C_JANSPEED_H2,
                        searches=T.SEARCHES_C_JANSPEED_H2, memory_limit=f"{MEMORY_MB}m",
                        parallel_runs=12, runs_per_task=36)
exp.run_steps()
