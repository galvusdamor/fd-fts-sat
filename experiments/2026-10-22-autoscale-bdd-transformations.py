#! /usr/bin/env python3

"""
Autoscale 21.11 agile STRIPS (42 domains x 30 tasks), 2026-10-22: the baselines and the best
configurations of the IPC runs. Not run on the cluster before, so nothing is fetched.

  A_1__chains_h2pre-shr          label_sat (CHAINS_PARALLEL), -shr, h2 preprocessing
  A_1__bdd_gc2_h2pre-shr         bdd_sat, -shr, h2 preprocessing (the 2026-09 configuration)
  A_1__bdd_gc2_h2info-mtau       bdd_sat, merging + tau_baking, h2 information
  A_1__fulltrans_gc2_h2pre-mtau  full_transitions_sat, merging + tau_baking, h2 preprocessing
  A_1__hyb_gc2_h2pre-mtau        hybrid, merging + tau_baking, h2 preprocessing (best on IPC)
  A_1__hyb_gc2_h2info-mtau       hybrid, merging + tau_baking, h2 information

All goal-chains v2 (except label_sat, which has no label order), 30 min / 2050 MB.
AUTOSCALE_BENCHMARKS points at the 21.11-agile-strips directory of
github.com/AI-Planning/autoscale-benchmarks (domain files are named domain.pddl or
domain-<problem>.pddl; lab and the driver find both). VAL cannot parse six of these domains
(data-network, floortile, openstacks, organic-synthesis-split, parcprinter, pathways); lab
does not validate here anyway. Configurations, setup and caveats: _trans_campaign.py.
"""

import os

import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["AUTOSCALE_BENCHMARKS"]
REVISION = "1011b8df74098730c0881a69bd3a24beb423ecd6"
SUITE = sorted(d for d in os.listdir(BENCHMARKS) if os.path.isdir(os.path.join(BENCHMARKS, d)))

exp = T.make_experiment(REVISION, BENCHMARKS, SUITE, T.RUN_AUTOSCALE, [], SCRIPT_NAME,
                        pairs=T.PAIRS_AUTOSCALE)
exp.run_steps()
