#! /usr/bin/env python3

"""
sigma_sat (intermediate-state / micro-step encoding, Symbolic Pattern Planning's sigma over an FTS)
on the IPC satisficing suite, 2026-11-10. January setting: -shr, no h2, 3500m, 12 runs at a time,
36 runs per SLURM task, 30 min. Revision on branch unified-encoding (StepEncoding: label_sat,
sigma_sat, micro_steps_sat in one class; label_sat's formulas are bit-identical to before).

Six configurations, all with the deterministic goal-chains v2 order (_trans_campaign.GC2D):

  A_1__sigma_gc2d-shr               algorithm A, plain sigma
  A_1__sigma_gc2d_shift-shr         + left/right shift of the order along the feeds graph
                                      (commute_order=3: horizons never grow)
  A_1__sigma_gc2d_blocks-shr        + exact per-factor forall blocks (identical horizons)
  A_1__sigma_gc2d_blocks_shift-shr  + both
  C_2__sigma_gc2d-shr               algorithm C (start 1, x1.6, 2 parallel lengths, gamma 0.5)
  C_2__sigma_gc2d_blocks_shift-shr  algorithm C, both improvements

Local results (386 Autoscale tasks, 300 s / 2500 MB): A 327 / 331 (sigma / blocks+shift),
C 337 / 343, forall-step (label_sat chains, January schedule) 297.

Fetched for comparison, same setting: forall-step algorithm A (2026-10-29-ipc-forall-A1) and C
with the January schedule (2026-10-27-ipc-algorithm-c-january-fix), TAV algorithm A
(2026-10-28-ipc-fulltrans-slf-fix-A1) and C (tuned, rintanen 2000 MB, via the january-fix eval).
Horizons differ by design (sigma packs several labels per step): compare coverage and time.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "d7575373ff6ef6bdacfd012f533214b53f289a08"
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_SIGMA, T.FETCH_SIGMA, SCRIPT_NAME, pairs=T.PAIRS_SIGMA,
                        searches=T.SEARCHES_SIGMA, memory_limit=f"{MEMORY_MB}m",
                        parallel_runs=12, runs_per_task=36)
exp.run_steps()
