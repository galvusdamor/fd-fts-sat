"""
Shared part of the 2026-10-20 transformation experiments (IPC, Autoscale, FTS).

bdd_sat with the goal-chains v2 label order on transformed FTSs, algorithm A
(`sat` with `one_by_one()`), 30 min / 2050 MB, 20 runs at a time on 24 cores,
60 runs per SLURM task. Local results behind the selection (303 Autoscale tasks,
300 s / 2500 MB): BDD.md sections 5.7 and 5.8.

  A_1__bdd_gc2-mtau          merging + tau_baking, no h2              local 299 / PAR-2 41.9
  A_1__bdd_gc2_h2info-mtau   the same + h2 mutexes + h2 dead labels   local 293 / 45.3
                             (h2 on a copy of the task, --h2-information; not for FTS)
  A_1__fulltrans_gc2-mtau    full_transitions_sat, same order         local 294 / 41.5
  A_1__bdd_gc2-mtaucl        merging + tau_baking + label_closure     local 297 / 43.6

merging = merge_local_guard(max_states=1000, max_dependents=1) in the M&S main
loop, label reduction only on the atomic FTS (see _bdd_common.MERGE).

Compared against the 2026-09-24 runs (fetched, not rerun), which differ in
setup -- keep that in mind when reading the merged report:
  * -shr instead of merging + tau_baking;
  * 1600 MB instead of 2050 MB (24 runs per task);
  * IPC: h2 *preprocessing* (driver --transform-task preprocess-h2), which
    changes the task; FTS: no h2.
A failed h2 preprocessing there is silent: the driver only warns and the search
runs on the unpreprocessed task. --h2-information here writes its file only
if h2 finishes and the encoding then prints "STATEMUTEXES skipped" /
"DEADLABELS skipped" (attributes h2_available, mutexes_skipped,
dead_labels_skipped).
"""

import os

import _bdd_common as B

EMAIL = "g.behnke@uva.nl"
MEMORY_LIMIT = "2050m"
PARALLEL_RUNS = 20
RUNS_PER_TASK = 60
CORES = 24

SEARCH = "sat(encoder={},solver_quiet=true,length_strategy=one_by_one())"

# name -> (encoder, transform key, uses h2 information)
CONFIGS = {
    "A_1__bdd_gc2-mtau": (B.bdd(label_order=B.GC2), "-mtau", False),
    "A_1__bdd_gc2_h2info-mtau": (B.bdd(label_order=B.GC2, **B.H2_ENCODER_OPTS), "-mtau", True),
    "A_1__fulltrans_gc2-mtau": (B.FULLTRANS_GC2, "-mtau", False),
    "A_1__bdd_gc2-mtaucl": (B.bdd(label_order=B.GC2), "-mtaucl", False),
}

# the 2026-09-24 experiments and the revisions their algorithms carry
REVISION_0921 = "f27d845858ae0001bfb375289e6b5b72a7b70b11"
REVISION_0922 = "3b070a3dc0a72e301419d8d894836608c1d52695"
REVISION_0924 = "4e3fef24f4fd5dc45bad3b2402278e1ad0f43f48"  # "Configs actually run" (fda171049)
OLD_ALGORITHMS = {
    "A_1__chains_slf____rcpol-shr": REVISION_0921,
    "A_1__fulltransitions_slf_transeff-shr": REVISION_0921,
    "A_1__bdd_full-shr": REVISION_0921,
    "A_1__bdd_full_relax-shr": REVISION_0922,
    "A_1__bdd_full_gc-shr": REVISION_0924,
    "A_1__bdd_full_gc2-shr": REVISION_0924,
}


def make_experiment(revision, benchmarks_dir, suite, config_names, fetch_from, name_prefix):
    """fetch_from: eval directory of the 2026-09-24 experiment of this suite, or None."""
    from lab.reports import Attribute, arithmetic_mean
    from downward.reports.scatter import ScatterPlotReport
    from score_report import AbsoluteReport
    import common_setup
    from common_setup import IssueConfig, IssueExperiment
    import encoding_size_parser
    import filters
    import fts_parser

    configs = []
    for name in config_names:
        enc, transform, h2 = CONFIGS[name]
        driver = (B.driver_opts_h2_information(MEMORY_LIMIT) if h2
                  else B.driver_opts(h2=False, memory_limit=MEMORY_LIMIT))
        configs.append(IssueConfig(
            name, B.TRANSFORM_OPTS_2026_10[transform] + ["--search", SEARCH.format(enc)],
            driver_options=driver, build_options=B.build_opts()))

    environment = B.make_environment(email=EMAIL, parallel_runs=PARALLEL_RUNS, cores=CORES,
                                     runs_per_task=RUNS_PER_TASK)
    exp = IssueExperiment(revisions=[revision], configs=configs, environment=environment)
    exp.add_suite(benchmarks_dir, suite)

    exp.add_parser(exp.EXITCODE_PARSER)
    exp.add_parser(exp.TRANSLATOR_PARSER)
    exp.add_parser(exp.SINGLE_SEARCH_PARSER)
    exp.add_parser(exp.PLANNER_PARSER)
    exp.add_parser(fts_parser.FTSParser())
    exp.add_parser(encoding_size_parser.EncodingSizeParser())

    exp.add_step("build", exp.build)
    exp.add_step("start", exp.start_runs)
    exp.add_step("parse", exp.parse)
    exp.add_fetcher(name="fetch")

    new = [f"{revision}-{c}" for c in config_names]
    old = []
    if fetch_from:
        old = [f"{rev}-{c}" for c, rev in OLD_ALGORITHMS.items()]
        exp.add_fetcher(fetch_from, filter_algorithm=old, name="fetch-2026-09-24", merge=True)

    attributes = (common_setup.ATTRIBUTES
                  + encoding_size_parser.TRANSFORMATION_ATTRIBUTES
                  + [Attribute(a, min_wins=True, function=arithmetic_mean, absolute=False)
                     for a in B.ENCODING_ATTRIBUTES if a != "bdd_failure_reason"]
                  + ["bdd_failure_reason"])
    report_filters = [filters.remove_revision,
                      filters.filter_bdd_known_unexplained_errors,
                      filters.filter_kissat_known_unexplained_errors]
    # filters.remove_revision drops the "<revision>-" prefix before filter_algorithm is
    # applied, so the tables select by the bare configuration names (all unique)
    exp.add_report(AbsoluteReport(attributes=attributes, filter=report_filters,
                                  filter_algorithm=list(config_names) + list(OLD_ALGORITHMS) if fetch_from
                                  else list(config_names)),
                   outfile=f"{name_prefix}-all.html")
    exp.add_report(AbsoluteReport(attributes=attributes, filter=report_filters,
                                  filter_algorithm=list(config_names)),
                   outfile=f"{name_prefix}-new.html")

    # each new configuration against its closest old one, and the new ones against #1
    main = f"{revision}-A_1__bdd_gc2-mtau"
    pairs = [(f"{revision}-{c}", main) for c in config_names if f"{revision}-{c}" != main]
    if fetch_from:
        pairs += [(f"{REVISION_0924}-A_1__bdd_full_gc2-shr", main),
                  (f"{REVISION_0921}-A_1__fulltransitions_slf_transeff-shr",
                   f"{revision}-A_1__fulltrans_gc2-mtau")]
    for c1, c2 in pairs:
        if c1 not in new + old or c2 not in new + old:
            continue
        for attribute in ["planner_time", "time_steps_with_label", "sat_conflicts"]:
            exp.add_report(
                ScatterPlotReport(attributes=[attribute], filter_algorithm=[c1, c2],
                                  get_category=lambda x, y: x["domain"], format="png",
                                  show_missing=attribute == "planner_time"),
                name=f"{name_prefix}-scatter-{attribute}-{c1.split('-', 1)[1]}-vs-{c2.split('-', 1)[1]}")
    return exp
