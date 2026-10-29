#! /usr/bin/env python3

"""
TAV and TAC (full_transitions_sat with the self-loop optimisation) with three label orders,
algorithm A (sat with one_by_one()), IPC satisficing suite, 2026-10-29. January setting: -shr,
no h2, 3500m, 12 runs at 3500M per task, 30 min.

  A_1__fulltrans_{transeff,labeleff}_slf_lin-shr   input order (label_order_linear())
  A_1__fulltrans_{transeff,labeleff}_slf_rev-shr   reverse input order (label_order_reverse())
  A_1__fulltrans_{transeff,labeleff}_slf_alt-shr   alternating: odd time steps the input order,
                                                   even ones its reverse
                                                   (alternate_label_orders=true)
transeff = TAV (transition variables in the effect clauses), labeleff = TAC (label variables).

alternate_label_orders (full_transitions_sat, REVISION below): the step semantics is the order of
the factor's relevant labels; the encoding keeps the order and its reverse and switches between
them per time step, and plan extraction sequences each step in its own order. Local check: the
first tasks of 30 IPC domains, 60 s, VAL (see the commit message).

Linear is rerun here (it is also in 2026-10-28-ipc-fulltrans-slf-fix-A1) so that all three orders
come from the same revision.
"""

import os

import common_setup
import _trans_campaign as T

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "688d5ba68b30a8b9b787f7889b3b0ee3fcc022fb"
MEMORY_MB = 3500

exp = T.make_experiment(REVISION, BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE,
                        T.RUN_A_ALT_ORDERS, [],
                        SCRIPT_NAME, pairs=T.PAIRS_A_ALT_ORDERS,
                        memory_limit=f"{MEMORY_MB}m", parallel_runs=12, runs_per_task=36)
exp.run_steps()
