"""
Shared configuration for the 2026-09-21 bdd_sat experiments.

Both scripts run **algorithm A only** (`sat` with `one_by_one()`), the
**full** transition relation only (`one_step_only=false`), and the **-shr**
transform only. Rintanen/CMit is deliberately excluded: with
`schedule_formula_as_one=true` and 20 parallel calls it generated horizons up
to 111 on rubiks-cube without solving any of them, so its numbers for this
encoding would say more about the scheduler than about the encoding.
"""

import math
import os

# --- the two reference configurations we compare against --------------------
# chains_slf____rcpol, from 2026-08-31-ipc-IJCAI-h2-preprocessing.py
CHAINS = ("label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,"
          "use_label_group=false,use_empty_pillars=true,use_empty_rows=true,"
          "use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,"
          "force_at_least_one_action=false)")
# fulltransitions_slf_transeff, from 2026-07-25-*-all-transitions-encodings.py
FULLTRANS = ("full_transitions_sat(use_self_loop_optimisation=true, "
             "use_labels_in_effects_constraints=false)")


def make_environment(email, parallel_runs=24, cores=24, memory_per_cpu="1750M", runs_per_task=None):
    """`parallel_runs` runs at a time in a task (SLURM job) of `cores` cores.

    The defaults are the 2026-09-21..24 setting: 24 runs on 24 cores, 1750M
    per cpu = 42000M per task. snellius.py defaults to 12 runs at 3500M each,
    which is the same 42 GB on half the cores. Subclassed here rather than
    edited there so the other scripts keep their settings. The driver limit in
    driver_opts() is set under the per-run share, so an over-budget run is
    killed by the driver with a diagnosable error instead of by SLURM taking
    down the whole task.

    With parallel_runs < cores the task still requests all `cores` cores (and
    so all their memory), but runs only `parallel_runs` at a time.

    `runs_per_task` (default: parallel_runs, i.e. one wave) runs go into a
    task. More than parallel_runs (e.g. 60 at 20 in parallel) keeps the slots
    busy: a task is billed until its slowest run ends, so with one wave a
    single timeout bills all cores for the full time limit. This relies on
    the job body handing the next run to whichever slot becomes free, and on
    DEFAULT_TIME_LIMIT_PER_TASK covering runs_per_task / parallel_runs time
    limits (02:30:00 = 3 x 30 min + margin). Per-run
    budget = cores * memory_per_cpu / parallel_runs; it must cover the driver
    limit *plus* the processes that are not limited (lab's run wrapper and the
    driver itself, a few tens of MB each), or runs can push each other over
    the task's memory.

    Imported inside the function so that this module stays importable without
    lab installed -- which is what lets the configuration strings be checked
    outside the cluster.
    """
    from snellius import SnelliusEnvironment

    class BDDSnelliusEnvironment(SnelliusEnvironment):
        PARALLEL_RUNS_PER_TASK = parallel_runs
        DEFAULT_MEMORY_PER_CPU = memory_per_cpu

        def __init__(self, **kwargs):
            super().__init__(**kwargs)
            self.cpus_per_task = cores

        def _get_num_runs_per_task(self):
            per_task = runs_per_task or self.PARALLEL_RUNS_PER_TASK
            num_runs = len(self.exp.runs)
            num_run_clusters = math.ceil(num_runs / per_task)
            return math.ceil(num_run_clusters / self.MAX_TASKS) * per_task

    return BDDSnelliusEnvironment(email=email)


def bdd(**kw):
    """bdd_sat with the full transition relation and explicit construction budgets.

    The budgets are not an optimisation: without them an instance whose BDDs
    cannot be built runs until the driver kills it and is reported as an
    unexplained error, instead of as
    `BDDSTAT construction_failed reason time_limit ...`, which the parser picks
    up as bdd_construction_failed.
    """
    d = dict(one_step_only="false", combinebdds="false", impltseitsin="true",
             omitforcedvariables="true", forcedvariablesthreshold=1,
             bdd_size_limit=-1, cutbdds="false",
             label_order="label_order_linear()",
             bdd_init_time_limit=600, bdd_node_limit=60000000,
             # Cudd sizes itself from the machine's RAM when left alone, which
             # cost 440-770MB per process before a single BDD existed -- a
             # quarter to a half of the 1750M each run gets here. These two have
             # to come down together: with maxMemory unset Cudd clamps both
             # requests to its own budget, so lowering either alone changes
             # nothing. Measured over cavediving, burnt-pancakes, topspin,
             # pancakes and rubiks: 442-770MB -> 19-20MB, construction time
             # unchanged, nodes_sum identical. init_nodes=10000 is too far and
             # makes cavediving construction 9x slower through table resizing.
             cudd_init_nodes=100000, cudd_cache_size=1000000)
    d.update(kw)
    return "bdd_sat(" + ",".join(f"{k}={v}" for k, v in d.items()) + ")"


ENCODINGS = {
    # --- reference points ---------------------------------------------------
    "chains_slf____rcpol":          CHAINS,
    "fulltransitions_slf_transeff": FULLTRANS,

    # --- the BDD encoding, full transition relation -------------------------
    "bdd_full":            bdd(),
    "bdd_full_comb":       bdd(combinebdds="true"),
    "bdd_full_cut":        bdd(cutbdds="true"),
    # the label order *is* the BDD variable order, so it drives BDD size
    "bdd_full_rev":        bdd(label_order="label_order_reverse()"),
    "bdd_full_rnd":        bdd(label_order="label_order_random()"),
    # Tseitin variants
    #"bdd_full_biimpl":     bdd(impltseitsin="false"),
    "bdd_full_noomit":     bdd(omitforcedvariables="false"),
    "bdd_full_omit2":      bdd(forcedvariablesthreshold=2),
    # interpolate towards the one-step relation for oversized state pairs
    #"bdd_full_lim1000":    bdd(bdd_size_limit=1000),
    #"bdd_full_lim100000":  bdd(bdd_size_limit=100000),
}

# algorithm A: one SAT call per length, shortest first
SEARCHES = [(f"A_1__{name}",
             f"sat(encoder={enc},solver_quiet=true,length_strategy=one_by_one())")
            for name, enc in ENCODINGS.items()]

TRANSFORM_OPTS = {
    "-shr": ["--transform",
             "transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation("
             "ignore_irrelevant_tau_groups=false),label_reduction=exact(max_time=300,"
             "atomic_fts=true,before_shrinking=true,before_merging=false),"
             "shrink_atomic_fts=true,run_main_loop=false,max_time=900,cost_type=one,"
             "prune_transitions_from_goal=true)"],
}


def driver_opts(h2, memory_limit="1600m"):
    """Driver options. `h2` activates the h2 preprocessor.

    Pass the *bare name* `preprocess-h2`, not a path: when it is not on the
    PATH the driver resolves it via get_executable(args.build, ...), i.e. the
    binary of the build lab just made. A hard-coded path (as in
    2026-08-31-ipc-IJCAI-h2-preprocessing.py) points at one person's home and
    breaks anywhere else.

    Two things to know about how it behaves, both verified:

    * h2 writes `output.sas` into the *current working directory*; it does not
      rewrite its input. It therefore only takes effect when the driver's sas
      file is `./output.sas`, which is the default and is what lab uses (every
      run gets its own directory). Passing --sas-file elsewhere makes it a
      silent no-op.
    * It only runs for tasks that go through the translator. driver/main.py
      calls transform_task() inside the `component == "translate"` branch, and
      a .sas input sets components = ["search"], so --transform-task is never
      reached. h2 therefore CANNOT be applied to the FTS benchmarks this way --
      see 2026-09-21-fts-bdd-A1.py.

    Effect on pipesworld-notankage/p01-net1-b6-g2: 128 -> 104 labels,
    282 -> 258 transitions, 4810 -> 3920 clauses per time step.
    """
    opts = []
    if h2:
        opts += ["--transform-task", "preprocess-h2"]
    # under the per-run share, see make_environment (1600m: 1750M per run)
    return opts + ["--overall-time-limit", "30m",
                   "--overall-memory-limit", memory_limit]


def build_opts():
    """kissat location. Overridable because the default is a cluster path.
    --custom-kissat is kept for consistency with the other scripts; algorithm A
    never calls kissat_set_external_scheduler, so stock kissat would do."""
    sat_dir = os.environ.get("KISSAT_BUILD",
                             "/gpfs/home2/behnkeg/software/kissat-p/build")
    return ["-j24", f"-s{sat_dir}", "--custom-kissat"]


# attributes produced by encoding_size_parser.py
ENCODING_ATTRIBUTES = [
    "enc_step_clauses_mean", "enc_step_variables_mean",
    "enc_step_clauses_last", "enc_step_variables_last",
    "enc_total_clauses", "enc_total_variables", "enc_last_length",
    "bdd_construction_failed", "bdd_failure_reason",
    "bdd_construction_time", "bdd_nodes_sum_all_factors",
    "bdd_nodes_sum_encoded", "bdd_nodes_max_pair", "bdd_tseitin_nodes",
    "bdd_peak_nodes",
]


# --- 2026-10 transformations campaign ---------------------------------------
# goal-chains v2, the label order of the 2026-09-24 runs (bdd_full_gc2)
GC2 = ("label_order_goal_chains(ancestor_depth=2,goal_pairs=2,leftover_layer=true,"
       "state_budget=200000,max_states=20000,exact_time_limit=1)")
FULLTRANS_GC2 = ("full_transitions_sat(use_self_loop_optimisation=true,"
                 "use_labels_in_effects_constraints=false,label_order=" + GC2 + ")")

# merging = merge_local_guard in the M&S main loop: a factor that only one other factor
# depends on is merged into its guard (product <= 1000 states); label reduction only on
# the atomic FTS (before_shrinking=false). Then tau_baking, optionally label_closure.
MERGE = ("transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation("
         "ignore_irrelevant_tau_groups=false),label_reduction=exact(max_time=300,"
         "atomic_fts=true,before_shrinking=false,before_merging=false),"
         "shrink_atomic_fts=true,run_main_loop=true,"
         "merge_strategy=merge_local_guard(max_states=1000,max_dependents=1),"
         "max_time=900,cost_type=one,prune_transitions_from_goal=true)")
TRANSFORM_OPTS_2026_10 = {
    "-shr": TRANSFORM_OPTS["-shr"],
    "-mtau": ["--transform", f"seq(transformations=[{MERGE},tau_baking()])"],
    "-mtaucl": ["--transform", f"seq(transformations=[{MERGE},tau_baking(),label_closure()])"],
    # label_union(): labels that each move one, different factor and share their
    # preconditions elsewhere become one label firing any subset of them
    "-mtauun": ["--transform", f"seq(transformations=[{MERGE},tau_baking(),label_union()])"],
}

# h2 without preprocessing: the driver runs h2 on a copy (--h2-information) and the
# encoding reads its mutexes and dead labels. The path is relative to the run directory
# and lower-case (the option parser lower-cases --search).
H2_FILE = "h2-information.sas"


def driver_opts_h2_information(memory_limit):
    """h2 at its default limit (300 s of h2 computation); the task is not changed."""
    return ["--h2-information", H2_FILE] + driver_opts(h2=False, memory_limit=memory_limit)


H2_ENCODER_OPTS = dict(state_mutexes="true", mutex_source="SAS_FILE", mutex_file=H2_FILE,
                       dead_labels="SAS_FILE", dead_labels_file=H2_FILE)
