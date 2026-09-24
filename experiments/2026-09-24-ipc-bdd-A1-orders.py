#! /usr/bin/env python3

"""
bdd_sat on the IPC/PDDL suite, algorithm A, -shr, with the h2 preprocessor.

Scope is deliberately narrow (see _bdd_common.py): algorithm A only, the full
transition relation only, -shr only. Compared against two reference
configurations, chains_slf____rcpol and fulltransitions_slf_transeff.

The question is where the BDD encoding wins and where it is problematic, so
the report carries the per-time-step formula size from encoding_size_parser.py
alongside coverage -- a configuration that solves less while producing a
smaller formula per step is a different finding from one that simply produces
a huge formula.

Label orders (2026-09-24). The label order is the BDD variable order and
decides which label sequences fit into one time step, so it drives the number
of time steps (time_steps_with_label). Compared, all bdd_sat with the full
transition relation:

  bdd_full        label_order_linear()
  bdd_full_relax  label_order_relaxed()
  bdd_full_gc     label_order_goal_chains()       -- version 1, defaults
  bdd_full_gc2    label_order_goal_chains(ancestor_depth=2, goal_pairs=2,
                  leftover_layer=true, state_budget=200000, max_states=20000,
                  exact_time_limit=1)             -- version 2

Only bdd_full_gc and bdd_full_gc2 are run here. The other four were run
already and are fetched instead, to save cluster budget: chains_slf____rcpol,
fulltransitions_slf_transeff and bdd_full from 2026-09-21-*-bdd-A1 (revision
f27d8458), bdd_full_relax from 2026-09-22-*-bdd-A1 (revision 3b070a3d). Their
algorithm names therefore carry those revisions; ALGORITHM_REVISION maps each
configuration to the revision its data comes from.

The other bdd_full variants of B.ENCODINGS (comb, cut, rev, rnd, noomit,
omit2) do not vary the order and are left out; add them back from
_bdd_common if needed. lo_* attributes come from the GOALCHAINS line
(encoding_size_parser.add_label_order); lo_time is the time spent computing
the order, which is part of search time.
"""

import os

from lab.environments import LocalEnvironment
from lab.reports import Attribute, geometric_mean, arithmetic_mean

from downward.reports.absolute import AbsoluteReport
from downward.reports.scatter import ScatterPlotReport

import common_setup
from common_setup import IssueConfig, IssueExperiment

import fts_parser
import encoding_size_parser
import filters

import _bdd_common as B

DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
REVISION = "33f1372499709accb2cd41ceea8a4f65a9f5c469"
REVISIONS = [REVISION]


# Only the new configurations are run; the baselines are fetched, see below.
ENCODINGS = {
    "bdd_full_gc": B.bdd(label_order="label_order_goal_chains()"),
    "bdd_full_gc2": B.bdd(label_order="label_order_goal_chains(ancestor_depth=2,goal_pairs=2,"
                          "leftover_layer=true,state_budget=200000,max_states=20000,"
                          "exact_time_limit=1)"),
}

# revision each configuration's data comes from (run here or fetched)
REVISION_0921 = "f27d845858ae0001bfb375289e6b5b72a7b70b11"
REVISION_0922 = "3b070a3dc0a72e301419d8d894836608c1d52695"
ALGORITHM_REVISION = {
    "A_1__chains_slf____rcpol-shr": REVISION_0921,
    "A_1__fulltransitions_slf_transeff-shr": REVISION_0921,
    "A_1__bdd_full-shr": REVISION_0921,
    "A_1__bdd_full_relax-shr": REVISION_0922,
    "A_1__bdd_full_gc-shr": REVISION,
    "A_1__bdd_full_gc2-shr": REVISION,
}


def algo(config):
    return f"{ALGORITHM_REVISION[config]}-{config}"


SEARCHES = [(f"A_1__{name}",
             f"sat(encoder={enc},solver_quiet=true,length_strategy=one_by_one())")
            for name, enc in ENCODINGS.items()]

CONFIGS = []
for s_name, s_opt in SEARCHES:
    print("Search: " + s_name)
    for t_name, t_opt in B.TRANSFORM_OPTS.items():
        CONFIGS.append(IssueConfig(
            f"{s_name}{t_name}",
            t_opt + ["--search", s_opt],
            driver_options=B.driver_opts(h2=True),
            build_options=B.build_opts()))

SUITE = common_setup.DEFAULT_SATISFICING_SUITE

#ENVIRONMENT = LocalEnvironment(processes=1)
ENVIRONMENT = B.make_environment(email="g.behnke@uva.nl")

exp = IssueExperiment(
    revisions=REVISIONS,
    configs=CONFIGS,
    environment=ENVIRONMENT,
)

exp.add_suite(BENCHMARKS_DIR, SUITE)

exp.add_parser(exp.EXITCODE_PARSER)
exp.add_parser(exp.TRANSLATOR_PARSER)
exp.add_parser(exp.SINGLE_SEARCH_PARSER)
exp.add_parser(exp.PLANNER_PARSER)

exp.add_step("build", exp.build)
exp.add_step("start", exp.start_runs)
exp.add_step("parse", exp.parse)
exp.add_parser(fts_parser.FTSParser())
exp.add_parser(encoding_size_parser.EncodingSizeParser())

exp.add_fetcher(name="fetch")

# already-run baselines, merged into this experiment's eval directory
tofetch = [
    ("2026-09-21-ipc-bdd-A1", [algo("A_1__chains_slf____rcpol-shr"),
                                 algo("A_1__bdd_full-shr"),
                                 algo("A_1__fulltransitions_slf_transeff-shr")]),
    ("2026-09-22-ipc-bdd-A1", [algo("A_1__bdd_full_relax-shr")]),
]

for (idd, (expname, algos)) in enumerate(tofetch):
    exp.add_fetcher(
        f"data/{expname}-eval",
        filter=[],
        filter_algorithm=algos,
        name=f"fetch-{expname}-{idd}",
        merge=True,
    )

LABEL_ORDER_ATTRIBUTES = ["lo_time", "lo_chains", "lo_pair_chains", "lo_leftover",
                          "lo_violated", "lo_explored_states", "lo_skipped_budget",
                          "lo_deep_fallbacks"]
ATTRIBUTES = common_setup.ATTRIBUTES + LABEL_ORDER_ATTRIBUTES + ["time_steps_with_label"] + [
    Attribute(a, min_wins=True, function=arithmetic_mean, absolute=False)
    for a in B.ENCODING_ATTRIBUTES if a != "bdd_failure_reason"
] + ["bdd_failure_reason"]

exp.add_report(
    AbsoluteReport(attributes=ATTRIBUTES,
                   filter=[filters.remove_revision,
                           filters.filter_bdd_known_unexplained_errors,
                           filters.filter_kissat_known_unexplained_errors]),
    outfile=f"{SCRIPT_NAME}-all.html")

for c1, c2 in [("A_1__bdd_full_gc-shr", "A_1__bdd_full_gc2-shr"),
               ("A_1__bdd_full_relax-shr", "A_1__bdd_full_gc2-shr"),
               ("A_1__bdd_full-shr", "A_1__bdd_full_gc2-shr"),
               ("A_1__chains_slf____rcpol-shr", "A_1__bdd_full_gc2-shr"),
               ("A_1__fulltransitions_slf_transeff-shr", "A_1__bdd_full_gc2-shr")]:
    exp.add_report(
        ScatterPlotReport(
            attributes=["planner_time"],
            filter_algorithm=[algo(c1), algo(c2)],
            get_category=lambda x, y: x["domain"],
            format="png",
            show_missing=True,
        ),
        name=f"ipc-scatterplot-planner-time-{c1}-vs-{c2}",
    )
    exp.add_report(
        ScatterPlotReport(
            attributes=["time_steps_with_label"],
            filter_algorithm=[algo(c1), algo(c2)],
            get_category=lambda x, y: x["domain"],
            format="png",
            show_missing=True,
        ),
        name=f"ipc-scatterplot-time-steps-{c1}-vs-{c2}",
    )
    exp.add_report(
        ScatterPlotReport(
            attributes=["enc_step_clauses_last"],
            filter_algorithm=[algo(c1), algo(c2)],
            get_category=lambda x, y: x["domain"],
            format="png",
            show_missing=False,
        ),
        name=f"ipc-scatterplot-step-clauses-{c1}-vs-{c2}",
    )
    exp.add_report(
        ScatterPlotReport(
            attributes=["solved_sat_clauses"],
            filter_algorithm=[algo(c1), algo(c2)],
            get_category=lambda x, y: x["domain"],
            format="png",
            show_missing=False,
        ),
        name=f"ipc-scatterplot-total-clauses-{c1}-vs-{c2}",
    )

    exp.add_report(
        ScatterPlotReport(
            attributes=["solved_sat_variables"],
            filter_algorithm=[algo(c1), algo(c2)],
            get_category=lambda x, y: x["domain"],
            format="png",
            show_missing=False,
        ),
        name=f"ipc-scatterplot-total-variables-{c1}-vs-{c2}",
    )

exp.run_steps()
