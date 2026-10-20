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

# name -> (encoder, transform key, h2): h2 is None, "info" (h2 on a copy, --h2-information,
# mutexes + dead labels) or "pre" (h2 preprocessing, --transform-task preprocess-h2, as in the
# 2026-09-24 runs: the task is changed, no mutex clauses)
FULLTRANS_GC2_H2INFO = (B.FULLTRANS_GC2[:-1] + ","
                        + ",".join(f"{k}={v}" for k, v in B.H2_ENCODER_OPTS.items()) + ")")
CONFIGS = {
    # first IPC run (2026-10-20)
    "A_1__bdd_gc2-mtau": (B.bdd(label_order=B.GC2), "-mtau", None),
    "A_1__bdd_gc2_h2info-mtau": (B.bdd(label_order=B.GC2, **B.H2_ENCODER_OPTS), "-mtau", "info"),
    "A_1__fulltrans_gc2-mtau": (B.FULLTRANS_GC2, "-mtau", None),
    "A_1__bdd_gc2-mtaucl": (B.bdd(label_order=B.GC2), "-mtaucl", None),
    # second IPC run (2026-10-21): h2 information for full_transitions_sat
    # (sat/static_constraints), the h2 modes against each other, and -shr at 2050 MB
    "A_1__fulltrans_gc2_h2info-mtau": (FULLTRANS_GC2_H2INFO, "-mtau", "info"),
    "A_1__fulltrans_gc2_h2pre-mtau": (B.FULLTRANS_GC2, "-mtau", "pre"),
    "A_1__bdd_gc2_h2info-shr": (B.bdd(label_order=B.GC2, **B.H2_ENCODER_OPTS), "-shr", "info"),
    "A_1__bdd_gc2_h2pre-shr": (B.bdd(label_order=B.GC2), "-shr", "pre"),
    "A_1__fulltrans_gc2_h2pre-shr": (B.FULLTRANS_GC2, "-shr", "pre"),
}
RUN_1 = ["A_1__bdd_gc2-mtau", "A_1__bdd_gc2_h2info-mtau", "A_1__fulltrans_gc2-mtau", "A_1__bdd_gc2-mtaucl"]
RUN_2 = ["A_1__fulltrans_gc2_h2info-mtau", "A_1__fulltrans_gc2_h2pre-mtau", "A_1__bdd_gc2_h2info-shr",
         "A_1__bdd_gc2_h2pre-shr", "A_1__fulltrans_gc2_h2pre-shr"]


def driver_options(h2):
    if h2 == "info":
        return B.driver_opts_h2_information(MEMORY_LIMIT)
    return B.driver_opts(h2=(h2 == "pre"), memory_limit=MEMORY_LIMIT)

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


# revision of the first IPC run (RUN_1)
REVISION_RUN_1 = "52efe061d80dcec607a31ba8224c0c7634f2fb1d"


def _compress_runs_dir(runs_dir):
    """runs_dir -> runs_dir.tar.bz2, read back completely before anything is deleted."""
    import subprocess
    runs_dir = runs_dir.rstrip("/")
    archive = runs_dir + ".tar.bz2"
    parent, name = os.path.split(runs_dir)
    subprocess.run(["tar", "-cjf", archive, "-C", parent, name], check=True)
    subprocess.run(["tar", "-tjf", archive], check=True, stdout=subprocess.DEVNULL)
    print(f"compressed {runs_dir} -> {archive}")


def _delete_runs_dir(runs_dir):
    """Deletes the run directory, but only next to a readable archive of it."""
    import shutil
    import subprocess
    runs_dir = runs_dir.rstrip("/")
    archive = runs_dir + ".tar.bz2"
    if not os.path.isdir(runs_dir):
        print(f"{runs_dir} does not exist, nothing to delete")
        return
    if not os.path.exists(archive):
        raise SystemExit(f"no archive {archive}: run compress-runs-dir first")
    subprocess.run(["tar", "-tjf", archive], check=True, stdout=subprocess.DEVNULL)
    shutil.rmtree(runs_dir)
    print(f"deleted {runs_dir} (archive: {archive})")


def add_compress_and_delete_runs_steps(exp):
    """After parse + fetch the run directories (logs) are only needed for debugging:
    compress-runs-dir writes <exp>.tar.bz2, delete-runs-dir removes the directory only
    if that archive exists and reads back. Unpack with tar -xjf to look at a run."""
    exp.add_step("compress-runs-dir", _compress_runs_dir, exp.path)
    exp.add_step("delete-runs-dir", _delete_runs_dir, exp.path)


def make_experiment(revision, benchmarks_dir, suite, config_names, fetches, name_prefix, pairs=None):
    """fetches: list of (eval directory, {configuration name: revision}) merged into this
    experiment's eval directory (e.g. ("data/2026-09-24-ipc-bdd-A1-orders-eval",
    OLD_ALGORITHMS)). pairs: (configuration, configuration) scatter plots, by bare name;
    names not in this experiment or a fetch are skipped."""
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
        configs.append(IssueConfig(
            name, B.TRANSFORM_OPTS_2026_10[transform] + ["--search", SEARCH.format(enc)],
            driver_options=driver_options(h2), build_options=B.build_opts()))

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
    add_compress_and_delete_runs_steps(exp)

    # bare configuration name -> full algorithm name (all bare names are unique)
    full = {c: f"{revision}-{c}" for c in config_names}
    for i, (eval_dir, algorithms) in enumerate(fetches):
        names = [f"{rev}-{c}" for c, rev in algorithms.items()]
        full.update({c: f"{rev}-{c}" for c, rev in algorithms.items()})
        exp.add_fetcher(eval_dir, filter_algorithm=names, name=f"fetch-{i}-{os.path.basename(eval_dir)}",
                        merge=True)

    attributes = (common_setup.ATTRIBUTES
                  + encoding_size_parser.TRANSFORMATION_ATTRIBUTES
                  + [Attribute(a, min_wins=True, function=arithmetic_mean, absolute=False)
                     for a in B.ENCODING_ATTRIBUTES if a != "bdd_failure_reason"]
                  + ["bdd_failure_reason"])
    report_filters = [filters.remove_revision,
                      filters.filter_bdd_known_unexplained_errors,
                      filters.filter_kissat_known_unexplained_errors]
    # filters.remove_revision drops the "<revision>-" prefix before filter_algorithm is
    # applied, so the tables select by the bare configuration names
    exp.add_report(AbsoluteReport(attributes=attributes, filter=report_filters,
                                  filter_algorithm=list(full)),
                   outfile=f"{name_prefix}-all.html")
    exp.add_report(AbsoluteReport(attributes=attributes, filter=report_filters,
                                  filter_algorithm=list(config_names)),
                   outfile=f"{name_prefix}-new.html")

    # scatter plots select by full names (no remove_revision filter there)
    for c1, c2 in pairs or []:
        if c1 not in full or c2 not in full:
            continue
        for attribute in ["planner_time", "time_steps_with_label", "sat_conflicts"]:
            exp.add_report(
                ScatterPlotReport(attributes=[attribute], filter_algorithm=[full[c1], full[c2]],
                                  get_category=lambda x, y: x["domain"], format="png",
                                  show_missing=attribute == "planner_time"),
                name=f"{name_prefix}-scatter-{attribute}-{c1}-vs-{c2}")
    return exp


# scatter pairs of the first run (as in its original script)
PAIRS_RUN_1 = [(c, "A_1__bdd_gc2-mtau") for c in RUN_1 if c != "A_1__bdd_gc2-mtau"] + [
    ("A_1__bdd_full_gc2-shr", "A_1__bdd_gc2-mtau"),
    ("A_1__fulltransitions_slf_transeff-shr", "A_1__fulltrans_gc2-mtau")]

# the second run: every pair differs in exactly one respect
PAIRS_RUN_2 = [
    # merging + tau_baking against -shr
    ("A_1__bdd_gc2_h2info-shr", "A_1__bdd_gc2_h2info-mtau"),
    ("A_1__fulltrans_gc2_h2pre-shr", "A_1__fulltrans_gc2_h2pre-mtau"),
    # h2 preprocessing against h2 information
    ("A_1__bdd_gc2_h2pre-shr", "A_1__bdd_gc2_h2info-shr"),
    ("A_1__fulltrans_gc2_h2pre-mtau", "A_1__fulltrans_gc2_h2info-mtau"),
    # no h2 against h2 information
    ("A_1__fulltrans_gc2-mtau", "A_1__fulltrans_gc2_h2info-mtau"),
    # 1600 MB against 2050 MB
    ("A_1__bdd_full_gc2-shr", "A_1__bdd_gc2_h2pre-shr"),
    # full_transitions_sat against bdd_sat
    ("A_1__fulltrans_gc2_h2info-mtau", "A_1__bdd_gc2_h2info-mtau"),
    ("A_1__fulltrans_gc2_h2pre-shr", "A_1__bdd_gc2_h2pre-shr"),
]
