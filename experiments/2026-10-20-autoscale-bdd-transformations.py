#! /usr/bin/env python3

"""
bdd_sat on transformed FTSs (merging + tau_baking), 2026-10-20: Autoscale 21.11 agile STRIPS (42 domains x 30 tasks), all four configurations.
Nothing to fetch: Autoscale has not been run on the cluster before. AUTOSCALE_BENCHMARKS
points at the 21.11-agile-strips directory of github.com/AI-Planning/autoscale-benchmarks.

Configurations, setup and caveats: _trans_campaign.py.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["AUTOSCALE_BENCHMARKS"]
REVISION = "52efe061d80dcec607a31ba8224c0c7634f2fb1d"

exp = T.make_experiment(REVISION, BENCHMARKS, sorted(d for d in os.listdir(BENCHMARKS) if os.path.isdir(os.path.join(BENCHMARKS, d))),
                        list(T.CONFIGS), None, SCRIPT_NAME)
exp.run_steps()
