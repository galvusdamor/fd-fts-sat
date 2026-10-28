#! /usr/bin/env python3

"""
Balyo's R2E (ICTAI 2013) as in the paper, downward-sat branch r2e at 579382908, IPC satisficing
suite, 2026-10-28. Setting as in the earlier R2E and January-fix runs: no h2, 3500m, 12 runs at
3500M per task, 30 min, --validate.

Differences to 2026-10-27-ipc-r2e-gamma.py (which used the implementation's defaults):
  * prune_opponents=false. The default (true) is an optimisation that is not in the paper: an
    operator with precondition x=u and an effect on x opposes only x=u. Sound (all plans valid),
    but it makes the formulas far harder for kissat: miconic s6-0 with tsort, length 2 takes 60 s
    instead of 0.02 s; on 40 tasks R2E lost on the cluster (miconic, transport, satellite,
    freecell, elevators) tsort solved 0 with pruning and 25 without.
  * force_at_least_one_action=false. The option was misnamed before 579382908
    (forceAtLeastOneAction; the parser lower-cases the search string), so every earlier run
    forced at least one action per step. The paper has no such constraint; with Algorithm C it
    makes lengths beyond the plan's number of actions unsatisfiable.

  R2Ep_tsort-A          tsort() (the default order), sat(..., one_by_one())
  R2Ep_tsort-Ctuned05   tsort(), schedule(...): start 1 x1.6, 4 calls, 1 s turns, gamma 0.5
  R2Ep_input-A          input() (SAS operator order), one_by_one()
  R2Ep_input-Ctuned05   input(), tuned Algorithm C as above

Note: R2E may set superfluous actions (plans of 400 actions where 19 suffice on miconic s6-0, 10326
on satellite p13). VAL cannot read the largest of these plan files ("Bad plan description");
experiments/validate_sas_plan.py confirmed them against the translated task. Treat validation
failures of very long plans with care.

The downward-sat checkout on the cluster (REPO_DOWNWARD_SAT) needs the r2e branch of
github.com/galvusdamor/downward-sat (remote "galvusdamor": git fetch galvusdamor r2e).

Fetched: the fixed forall-step Algorithm C (2026-10-27-ipc-algorithm-c-january-fix) and the
default-option R2E runs of 2026-10-27-ipc-r2e-gamma.
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
REVISION_DOWNWARD_SAT = "579382908897c3451e7907bb0b6de36b424832c6"
BUILD_OPTS = ["-j24", "-s/gpfs/home2/behnkeg/software/kissat-p/build", "--custom-kissat"]
DRIVER_OPTS = ["--validate", "--overall-time-limit", "30m", "--overall-memory-limit", "3500m"]

def r2e(order):
    return f"r2e(ordering={order},prune_opponents=false,force_at_least_one_action=false)"
def search_a(enc):
    return f"sat(encoder={enc},solver_quiet=true,length_strategy=one_by_one())"
def search_c(enc):
    return (f"schedule(encoder={enc},solver_quiet=true,"
            "length_strategy=by_iteration(start_length=1,multiplier=1.6),"
            "memory_limit_mb=3500,max_parallel_calls=4,gamma=0.5,scheduler_interval=1,"
            "schedule_formula_as_one=true)")
SEARCHES = {}
for _name, _order in [("tsort", "tsort()"), ("input", "input()")]:
    SEARCHES[f"R2Ep_{_name}-A"] = search_a(r2e(_order))
    SEARCHES[f"R2Ep_{_name}-Ctuned05"] = search_c(r2e(_order))
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
exp.add_fetcher("data/2026-10-27-ipc-r2e-gamma-eval",
                filter_algorithm=["R2E_tsort-A", "R2E_tsort-Ctuned05"], name="fetch-r2e-default", merge=True)

report_filters = [filters.remove_revision, filters.filter_kissat_known_unexplained_errors]
exp.add_report(AbsoluteReport(attributes=common_setup.ATTRIBUTES, filter=report_filters),
               outfile=f"{SCRIPT_NAME}-all.html")
exp.run_steps()
