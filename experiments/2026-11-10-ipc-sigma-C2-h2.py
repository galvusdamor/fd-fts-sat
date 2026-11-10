#! /usr/bin/env python3

"""
sigma_sat with algorithm C, reference run, IPC satisficing suite, 2026-11-10. January setting
(3500m, 12 runs at a time, 36 per SLURM task, 30 min), revision as 2026-11-10-ipc-sigma-A1-C2
(branch unified-encoding). Algorithm C: by_iteration(start_length=1, multiplier=1.6), 2 parallel
lengths, gamma 0.5, rintanen memory_limit_mb=3500.

  C_2__sigma_gc2d_blocks_shift_h2pre-shr     best sigma configuration (per-factor blocks + shift,
                                             deterministic goal-chains v2) with h2 preprocessing
  C_2__sigma_gc2d_blocks_shift_h2pre-mtauun  the same on merging + tau_baking + label_union
  C_2__sigma_lin-shr                         plain sigma with the input order (label_order_linear)

Fetched: from 2026-11-10-ipc-sigma-A1-C2 the same encodings without h2 (sigma, blocks + shift)
and, merged there, forall-step (January schedule) and TAV; from 2026-10-26-ipc-algorithm-c the
hybrid BDD encoding with h2 + merging + tau_baking (+ label_union) and forall-step with h2. The
2026-10-26 runs had 1669 MiB per run (24 runs per node): a different setting, for orientation.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "d7575373ff6ef6bdacfd012f533214b53f289a08"
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_SIGMA_C_REF, T.FETCH_SIGMA_C_REF, SCRIPT_NAME, pairs=T.PAIRS_SIGMA_C_REF,
                        searches=T.SEARCHES_SIGMA_C_REF, memory_limit=f"{MEMORY_MB}m",
                        parallel_runs=12, runs_per_task=36)
exp.run_steps()
