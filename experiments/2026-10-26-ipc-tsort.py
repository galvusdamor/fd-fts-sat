#! /usr/bin/env python3

"""
Balyo's topological ranking (label_order_tsort) for full_transitions_sat, algorithm A
(sat with one_by_one), IPC satisficing suite, 2026-10-26.

label_order_tsort(): depth-first post-order over the enabling graph -- l before l' if l has a
non-self-loop transition in a factor where l' has a precondition, into a state where l' is
applicable. strict_enabling=true: only transitions from a state where l' is not applicable into
one where it is. The post-order respects the order between strongly connected components;
inside one it follows the DFS (label_order_finder.h).

  A_1__fulltrans_transeff_tsort-shr        transitions as variables (TAV), tsort
  A_1__fulltrans_transeff_tsortstrict-shr  TAV, tsort with strict enabling
  A_1__fulltrans_labeleff_tsort-shr        transitions as clauses (TAC: label variables in the
                                           effect clauses), tsort
  A_1__fulltrans_labeleff_tsortstrict-shr  TAC, tsort with strict enabling
  A_1__fulltrans_labeleff-shr              TAC, linear label order (baseline)

All -shr with h2 preprocessing, 30 min / 2050 MB, as the fetched 2026-09-24 baseline
A_1__fulltransitions_slf_transeff-shr (TAV, linear, 1600 MB). Configurations, setup and
caveats: _trans_campaign.py.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "54d23ef04bd6f52a39d5f2fa0110cf1c2b1a305c"

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_TSORT,
                        [("data/2026-09-24-ipc-bdd-A1-orders-eval", T.OLD_ALGORITHMS)],
                        SCRIPT_NAME, pairs=T.PAIRS_TSORT)
exp.run_steps()
