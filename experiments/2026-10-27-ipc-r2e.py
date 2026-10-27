#! /usr/bin/env python3

"""
Balyo's Relaxed Relaxed Exists-Step encoding (R2E, ICTAI 2013 / thesis 2014) from downward-sat,
branch r2e (revision 08e2f95f9), with Balyo's topological action ranking (tsort()), IPC satisficing
suite, 2026-10-27. Setting of the January-fix confirmation run: no h2, 3500m, 12 runs at 3500M per
task, 30 min; plans validated with VAL (--validate).

  R2E_tsort-A        sat(r2e(ordering=tsort()), one_by_one())        algorithm A
  R2E_tsort-Cjan     schedule(...), January Algorithm C parameters   by_iteration() = 5 x1.41,
                                                                     20 calls, 1 s turns
  R2E_tsort-Ctuned   schedule(...), our tuned lengths and calls      start 1 x1.6, 4 calls, 1 s
                                                                     turns -- but gamma 1: the
                                                                     downward-sat scheduler has no
                                                                     geometric time share

Before the run (2026-10-27, local): R2E is marked "not tested yet" in that commit. Checked on the
first tasks of 35 IPC domains, 120 s: all 78 plans VAL-valid (tsort, input and disabling orders);
with KSR's own ordering (r2e(ordering=disabling())) R2E never needed more steps than KSR's
exists-step and usually far fewer (blocks 16 vs 32, freecell 3 vs 8, openstacks 6 vs 14). Its
UNSAT calls at short lengths are much harder for kissat (transport length 2: 99 s vs 0.2 s).

The downward-sat checkout on the cluster (REPO_DOWNWARD_SAT) must have fetched branch r2e.

Fetched for comparison: the fixed forall-step Algorithm C (CMit_chains_slf____rcpol_janfix-shr) from
2026-10-27-ipc-algorithm-c-january-fix.
"""

import os

from downward.experiment import FastDownwardExperiment
from score_report import AbsoluteReport

import common_setup
import filters
import sat_parser
import _bdd_common as B

SCRIPT_NAME = os.path.splitext(os.path.basename(__file__))[0]
BENCHMARKS = os.environ["DOWNWARD_BENCHMARKS"]
REPO_DOWNWARD_SAT = "/gpfs/home2/behnkeg/lab/downward-sat"
REVISION_DOWNWARD_SAT = "08e2f95f91a4d1a984f1658fc91fa523ca27733a"
BUILD_OPTS = ["-j24", "-s/gpfs/home2/behnkeg/software/kissat-p/build", "--custom-kissat"]
DRIVER_OPTS = ["--validate", "--overall-time-limit", "30m", "--overall-memory-limit", "3500m"]

R2E = "r2e(ordering=tsort())"
SEARCHES = {
    "R2E_tsort-A": f"sat(encoder={R2E},solver_quiet=true,length_strategy=one_by_one())",
    "R2E_tsort-Cjan": (f"schedule(encoder={R2E},solver_quiet=true,length_strategy=by_iteration(),"
                       "memory_limit_mb=3500,max_parallel_calls=20,scheduler_interval=1,"
                       "schedule_formula_as_one=true)"),
    "R2E_tsort-Ctuned": (f"schedule(encoder={R2E},solver_quiet=true,"
                         "length_strategy=by_iteration(start_length=1,multiplier=1.6),"
                         "memory_limit_mb=3500,max_parallel_calls=4,scheduler_interval=1,"
                         "schedule_formula_as_one=true)"),
}
FORALL = "CMit_chains_slf____rcpol_janfix-shr"
FORALL_REVISION = "03ba4e54767aa473b6220f28d6be7221c9c3015e"

exp = FastDownwardExperiment(environment=B.make_environment(email="g.behnke@uva.nl", parallel_runs=12,
                                                            runs_per_task=36))
for name, search in SEARCHES.items():
    exp.add_algorithm(name, REPO_DOWNWARD_SAT, REVISION_DOWNWARD_SAT, ["--search", search],
                      driver_options=DRIVER_OPTS, build_options=BUILD_OPTS)
exp.add_suite(BENCHMARKS, common_setup.DEFAULT_SATISFICING_SUITE)

exp.add_parser(exp.EXITCODE_PARSER)
exp.add_parser(exp.TRANSLATOR_PARSER)
exp.add_parser(exp.SINGLE_SEARCH_PARSER)
exp.add_parser(exp.PLANNER_PARSER)
exp.add_parser(sat_parser.SATParser())

exp.add_step("build", exp.build)
exp.add_step("start", exp.start_runs)
exp.add_step("parse", exp.parse)
exp.add_fetcher(name="fetch")
common_setup.add_compress_and_delete_runs_step(exp)
exp.add_fetcher("data/2026-10-27-ipc-algorithm-c-january-fix-eval",
                filter_algorithm=[f"{FORALL_REVISION}-{FORALL}"], name="fetch-forall", merge=True)

report_filters = [filters.remove_revision, filters.filter_kissat_known_unexplained_errors]
exp.add_report(AbsoluteReport(attributes=common_setup.ATTRIBUTES, filter=report_filters),
               outfile=f"{SCRIPT_NAME}-all.html")
exp.run_steps()
