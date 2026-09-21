# CLAUDE.md — fd-fts-sat

Guidance for Claude Code (and humans) working in this repository.

## What this project is

A fork of **Fast Downward** that does **not** plan on the finite-domain (SAS+)
representation. Instead, `downward` reads `output.sas`, builds an atomic
**Factored Transition System (FTS)** from it, optionally runs a **task
transformation** (merge-and-shrink style: label reduction, shrinking, merging,
transition pruning), and then hands the resulting `FTSTask` to a search engine.
Plans are found on the transformed FTS and mapped back to the original task via
`g_plan_reconstruction`.

The research focus of this fork is **SAT-based planning on FTS**: encoding
bounded-length planning over a set of transition systems + labels into CNF and
solving it with kissat (Rintanen-style parallel length scheduling).

Key consequences for anyone working here:

* There is **no `SearchEngine` that works on FDR/`TaskProxy` in the usual FD
  sense**. Everything in `src/search/sat/` and the SAT engines works on
  `task_representation::FTSTask` (`fts->get_ts(i)`, `fts->get_num_labels()`,
  `TransitionSystem::get_transitions_with_label(l)`, `.src`/`.target`,
  `get_init_state()`, `get_goal_states()`, `is_relevant_label`,
  `is_selfloop_everywhere`, `isAlwaysSelfLoop`).
* A **`--transform` step is effectively mandatory** before search. Without it
  you get the atomic FTS (one factor per SAS+ variable), which is what the
  FTS benchmarks already are.
* The global `g_main_task` (`globals.h`) holds the *transformed* FTS; the
  `SATEncodingFactory` base ctor reads it directly.

## Repository layout (the parts that matter)

```
fast-downward.py, driver/         Python driver. --transform is *not* parsed here; it
                                  is passed through to the C++ binary. driver/aliases.py
                                  has FTS aliases (seq-agl-fts-ff, seq-opt-fts-sbd, ...).
build.py, build_configs.py        Build configs. DEFAULT = release64, DEBUG = debug64.
src/translate/                    Standard FD translator (PDDL -> output.sas).
src/search/planner.cc             main(): output.sas -> atomic FTS -> --transform -> search.
src/search/options/option_parser.cc  parses --transform / --search / --heuristic.
src/search/task_representation/   FTSTask, TransitionSystem, Labels, LabelEquivalenceRelation.
src/search/task_transformation/   Merge-and-shrink machinery used as a *transformation*
                                  (transform_merge_and_shrink, shrink_*, label reduction,
                                  plan_reconstruction).
src/search/sat/                   All SAT encodings + solver glue (see below).
src/search/search_engines/        sat_search, incremental_sat_search, rintanen_search
                                  (+ plugin_*.cc registering "sat", "incremental_sat",
                                  "rintanen").
src/search/task_utils/label_order_finder.*  Plugin type LabelOrderFinder
                                  (label_order_linear / _reverse / _random).
src/search/symbolic/, symbolic_pdbs/  Symbolic (BDD) search engines. Only these
                                  currently pull in CUDD.
src/search/cudd-3.0.0/            Vendored CUDD, built via ExternalProject in
                                  src/search/CMakeLists.txt.
experiments/                      Downward Lab experiment scripts (dated), parsers
                                  (sat_parser.py, fts_parser.py), cluster envs
                                  (snellius.py). Not part of the build.
Apptainer.*                       Container recipes used for IPC-style runs.
```

## Building

```bash
./build.py                 # release64 (default)
./build.py debug64         # debug; enables DEBUG(...) blocks and variable-name registry
./build.py release64 -j8
```

Things to know before building:

* **kissat is required and is NOT vendored.** `build_configs.py` passes
  `-DUSE_KISSAT=ON -DUSE_CUSTOM_KISSAT=ON -DSAT_DIR=<path> -DSAT_LIB=kissat`.
  The default `SAT_DIR` is a hard-coded developer path
  (`/home/alvaro/projects/joao/kissat-p/build`), so on any other machine you
  must override it. `build.py` already takes flags for this — do not edit
  `build_configs.py`:

  ```bash
  # the patched solver, needed for rintanen
  git clone -b p https://github.com/galvusdamor/kissat-p   # NOTE: branch 'p',
  cd kissat-p && ./configure && make                       # master has no patch
  ./build.py release64 -s/abs/path/to/kissat-p/build --custom-kissat -j8

  # stock kissat is enough for sat() and incremental_sat()
  ./build.py release64 -s/abs/path/to/kissat/build --kissat -j8
  ```

  `-s<dir>` sets `SAT_DIR`, `--custom-kissat` / `--kissat` set
  `USE_CUSTOM_KISSAT` on/off. Note the repo provides its own IPASIR shim in
  `sat/kissat.cpp` over kissat's native API, so the solver does not need to
  export `ipasir_*` itself.

  `rintanen_search.cc` calls `kissat_set_external_scheduler(...)`, which only
  exists on the `p` branch of the fork. With `--kissat`, `sat/kissat_dummy.cc`
  provides a stub that prints an explanation and exits, so `sat()` still works
  but `rintanen(...)` does not. Rintanen's threads are **cooperatively**
  scheduled (`run_mutex` in `SAT_Call_Data`): at most one runs at a time, so
  "parallel calls" means interleaved, not concurrent.
* The SAT plugin is `PLUGIN_SAT_SEARCH` (engines) and `SAT_SEARCH`
  (dependency-only library) in `src/search/DownwardFiles.cmake`. Add new
  encoding source files to the `SAT_SEARCH` `SOURCES` list; the extension `.cc`
  is implied.
* **CUDD is built whenever `PLUGIN_SYMBOLIC_ENABLED` *or*
  `PLUGIN_SAT_SEARCH_ENABLED` is set** (`src/search/CMakeLists.txt`, the
  `USE_CUDD` block). `bdd_sat` needs `cuddObj.hh`, and `SAT_SEARCH` does not
  depend on `SYMBOLIC`, so the stock `release64` config — which sets
  `-DPLUGIN_SYMBOLIC_SEARCH_ENGINE_ENABLED=FALSE` — used to leave the headers
  off the include path. The `ExternalProject` builds in-source under
  `src/search/cudd-3.0.0/` (gitignored) and needs `automake`/`autoconf`
  (`aclocal && autoheader && automake --add-missing && autoconf`). It adds
  roughly a minute to a clean build.
* Header include guards are hand-written and **collide**:
  `SEARCH_ALGORITHMS_SAT_SEARCH` is still used by both
  `sat/label_based_encoding.h` and `search_engines/sat_search.h`. Including
  both in one TU silently drops the second. (`sat/bdd_encoding.h` was moved off
  it to `SAT_BDD_ENCODING_H`.) Use unique guards in anything you touch.
* `enum encoding_type {SEQUENTIAL, SELF_LOOP_PARALLEL, CHAINS_PARALLEL}` is
  defined in both `sat/label_based_encoding.h` and
  `search_engines/rintanen_search.h`, inside `namespace sat_search`. Do not
  include both in the same TU.

## Running

Every run needs (1) a task, (2) a `--transform`, (3) a `--search`.

Canonical debug invocation used for development:

```bash
./fast-downward.py --debug \
  ../../domains/classical-domains/classical/parcprinter-08-strips/p01.pddl \
  --transform "transform_merge_and_shrink(shrink_strategy=shrink_weak_bisimulation(ignore_irrelevant_tau_groups=false),label_reduction=exact(max_time=300,atomic_fts=true,before_shrinking=true,before_merging=false),shrink_atomic_fts=true,run_main_loop=false,max_time=900,cost_type=one,prune_transitions_from_goal=true)" \
  --search "rintanen(encoder=label_sat(encoding=CHAINS_PARALLEL,use_self_loop_optimisation=true,use_label_group=false,use_empty_pillars=true,use_empty_rows=true,use_empty_cols=true,use_positive_one=true,use_ones_in_last_dimension=true,force_at_least_one_action=false),solver_quiet=true,length_strategy=by_iteration(),memory_limit_mb=3500,max_parallel_calls=20,scheduler_interval=1,schedule_formula_as_one=true)"
```

Notes:

* Domain/problem live outside the repo (`../../domains/classical-domains/...`
  relative to the repo root on the dev machine). Passing only the problem file
  works because the driver auto-detects `domain.pddl` / `domain_pXX.pddl`.
* `run_main_loop=false` means "shrink/label-reduce the atomic FTS but do not
  merge" — the typical setting for SAT encodings, which want many small
  factors, not one big product.
* The transformation above uses `cost_type=one`; SAT search is unit-cost /
  length-based anyway.
* Instances from the **FTS benchmarks** already are transformed FTS files; use
  them directly with the same `--search` and (usually) no further transform.
  They are plain `.sas` files laid out as `fts-tasks/<domain>/<instance>.sas`
  and reached through `$FTS_BENCHMARKS`; the lab scripts symlink each one into
  a run directory as `task.sas`. The set is 431 instances over six domains:
  `pancakes` and `burnt-pancakes` (100 each), `rubiks-cube` (100), `topspin`
  (100), `cavediving-adl14` (20) and `matrix-multiplication` (11); that is
  `common_setup.FTS_SUITE`.

  **They are not on Zenodo.** Record 20444380 for the IJCAI'26 paper is
  results only — logs, parsed properties and the experiment scripts — and its
  run directories contain `task.sas` merely as a dangling symlink into
  `~/benchmarks/fts-tasks/` on the cluster. Its README points at
  `aibasel/downward-benchmarks`, which is the PDDL suite and does not have
  these domains either. To run against the FTS benchmarks you need a copy of
  that `fts-tasks` directory from the cluster.

  Run them straight from the `.sas` file — no translator step, and the
  `--transform` is still yours to choose:

  ```bash
  ./fast-downward.py --plan-file /tmp/plan fts-tasks/pancakes/n5-p4.sas \
    --transform "cost(cost_type=one)" \
    --search "sat(encoder=bdd_sat(),length_strategy=one_by_one())"
  python3 experiments/validate_sas_plan.py fts-tasks/pancakes/n5-p4.sas /tmp/plan
  ```

  VAL cannot check these — there is no PDDL — hence
  `experiments/validate_sas_plan.py`.

  Without them, the nearest local substitute is the **atomic FTS**: run a PDDL
  task with `--transform "cost(cost_type=one)"` (the `-ntr` setting in the
  experiment scripts), which skips shrinking and leaves one factor per SAS+
  variable — many small factors, which is structurally what the FTS benchmarks
  look like.
* `--debug` builds and runs `debug64`, which prints registered SAT variable
  names (`sat_capsule::registerVariable`) — very useful when validating an
  encoding.
* Quick sanity check of the SAT stack without Rintanen's threads:
  `--search "sat(encoder=label_sat(...),length_strategy=one_by_one())"`.
* The same run with the BDD encoding — `bdd_sat()` defaults are usable as-is:

  ```bash
  --search "sat(encoder=bdd_sat(),length_strategy=one_by_one(),solver_quiet=true)"
  --search "sat(encoder=bdd_sat(one_step_only=false),length_strategy=one_by_one(),solver_quiet=true)"
  ```

* **Validate plans with VAL**, not just by reading "Solution found." The
  encodings each contain a manual FTS plan check in `extractSolution`, but
  that only checks the plan on the *transformed* task:

  ```bash
  ./fast-downward.py --plan-file /tmp/plan --sas-file /tmp/out.sas <problem> ...
  Validate <domain.pddl> <problem.pddl> /tmp/plan     # must print "Plan valid"
  ```

**Watch out for tasks the transformation solves on its own.** With the usual
shrink transformation the FTS can come out empty — `Main task: FTSTask with 0
variables, 0 labels` followed by `Task solved without search`. The planner then
reports a plan and "Solution found." without the search engine, let alone the
encoding, ever running. Of 32 first-instances tried, five behave this way:
`elevators-00-strips/s1-0`, `logistics00/adl-98-prob01`, `miconic/s1-0`,
`movie/prob01` and `zenotravel/pfile1`. Such a task tells you nothing about an
encoding, and because every encoder "agrees" on it, it is easy to mistake for
passing coverage. Grep the log for `Task solved without search` and discount
those runs.

A known rough edge, **pre-existing and not specific to any encoding**
(`label_sat` reproduces it identically), so don't chase it when a new encoding
seems to misbehave:

* `debug64` aborts on some tasks in the `FTSTask` constructor —
  `assert(distances.get_goal_distance(s) < numeric_limits<int>::max())`
  (`task_representation/fts_task.cc:87`), e.g. on parcprinter p01. Assertion
  coverage has to come from tasks that get past it.

When scripting validation, find the domain file the way the driver does
(`driver/util.py:find_domain_filename`): `domain.pddl`, else
**`basename[:3] + "-domain.pddl"`**, else `domain_<basename>`, else
`domain-<basename>`. Guessing `<problem>-domain.pddl` silently picks the wrong
file in e.g. `airport/` (`p01-airport1-p1.pddl` pairs with `p01-domain.pddl`),
and VAL then calls every plan invalid.

### Search engines and plugin names

| Plugin (`--search`) | File | What it does |
|---|---|---|
| `sat(...)` | `search_engines/sat_search.cc` | One SAT call per length, lengths from `length_strategy`. |
| `incremental_sat(...)` | `search_engines/incremental_sat_search.cc` | Reuses one solver; uses the `retractable` init/goal/`encodeStateEquals` API. |
| `rintanen(...)` | `search_engines/rintanen_search.cc` | Rintanen's Algorithm C: several lengths in parallel threads, kissat interrupted by an external scheduler. |

Both `sat` and `rintanen` take `encoder=<SATEncodingFactory>` and
`length_strategy=<LengthStrategy>` (`one_by_one()`, `by_iteration()`,
`constant(...)`).

| Encoder plugin | File | Notes |
|---|---|---|
| `label_sat(...)` | `sat/label_based_encoding.cc` | Main encoding (labels + states, matrix-based "row/col/pillar" optimisations via `FTSMatrix`). |
| `full_transitions_sat(...)` | `sat/full_transitions_encoding.cc` | One SAT var per (non-self-loop) transition. |
| `split_transitions_sat(...)` | `sat/split_transitions_encoding.cc` | Split transition variables. |
| `bdd_sat(...)` | `sat/bdd_encoding.cc` | BDDs per factor over the label variables, Tseitin-translated. See below. |

## Architecture of the SAT stack (post-refactor, Oct 2025)

```
SATEncodingFactory  (sat/sat_encoding.h)      one per --search, ctor reads g_main_task
  ├─ initialize()                              expensive, task-level precomputation (once)
  └─ createEncodingInstance(shared_ptr<sat_capsule>) -> unique_ptr<SATEncoding>
                                               one per SAT call / plan length
SATEncoding
  ├─ encode(fromTime, toTime)                  transition relation for steps fromTime..toTime
  ├─ encodeInit(fromTime, retractable)
  ├─ encodeGoal(toTime, retractable)
  ├─ encodeStateEquals(fromTime, toTime, retractable)   (incremental engine only)
  └─ extractSolution(initTime, time_step_order)
         -> tuple<PlanState, vector<PlanState>, vector<int> labels, set<int> timesteps>

Concrete hierarchy:
  SATEncoding
   └─ StateEncoding          (sat/state_encoding.*)     state vars per (time, ts, state),
                                                        exactly-one, init/goal encoding
       └─ LabelEncoding      (sat/label_encoding.*)     label vars per time, relevantLabels
           └─ CommonEncoding (sat/common_encoding.*)    template method encode():
                                                        generateAdditionalVariables ->
                                                        encode_transition ->
                                                        encode_frame_axioms;
                                                        shared extractSolution using
                                                        extractIntermediateStates()
               ├─ LabelBasedEncoding        (label_sat)
               └─ TransitionsEncoding
                    ├─ FullTransitionsEncoding
                    └─ SplitTransitionsEncoding
```

* **All clause emission goes through the `sat_capsule`**
  (`sat/sat_encoder.h`): `sat->new_variable()`, `sat->implies(a,b)`,
  `sat->atMostOne(v)`, `sat->atLeastOne(v)`, `sat->notAll(v)`,
  `sat->andImplies(...)`, `sat->impliesOr(...)`, chain encoders, etc. The
  capsule owns the `void* solver` (ipasir handle). **There are no longer free
  functions `atMostOne(solver, capsule, ...)` / `assertYes(solver, ...)` and no
  `reset_number_of_clauses()`; `sat_capsule` has no default ctor.**
* The **engine** owns the solver lifecycle: it calls `ipasir_init()`, wraps it
  in a `sat_capsule`, asks the factory for an encoding, calls
  `encodeInit(1,false)`, `encode(...)` for each step, `encodeGoal(len+1,false)`,
  then `ipasir_solve`, then `extractSolution`. Encodings **must not** create
  or solve their own solver.
* Timesteps are 1-based in the engine (`encodeInit(1, ...)`).
* `StateEncoding::allTimesStateVars[time][ts][state]` and
  `LabelEncoding`'s per-time label vars are the shared vocabulary; new
  encodings should reuse `getPreviousStateSATVar/getNextStateSATVar` rather
  than inventing parallel bookkeeping.
* Plugin registration pattern (options framework is the *old* FD
  `options::OptionParser`, not `plugins::Feature`):

  ```cpp
  static shared_ptr<SATEncodingFactory> _parse_xxx(options::OptionParser &parser) {
      parser.add_option<bool>("name", "doc", "default");
      options::Options opts = parser.parse();
      if (parser.dry_run()) return nullptr;
      return make_shared<XxxFactory>(opts);
  }
  static options::PluginShared<SATEncodingFactory> _plugin_xxx("xxx_sat", _parse_xxx);
  ```

  (`extern void add_options_to_feature(plugins::Feature&)` declarations in some
  headers are leftovers and unused.)

## The BDD encoding (`src/search/sat/bdd_encoding.{h,cc}`)

### Idea

For every factor (transition system) build BDDs over
`[factor-state vars | factor-next-state vars | one var per label]` that
describe which label sets, applied in a fixed **label order**, move the factor
from state `s` to state `ss` within one plan step (possibly several labels per
step — this is the parallelism). The BDDs are then Tseitin-translated into CNF
(`bdd_to_cnf`), shared across all time steps, and conditioned on the SAT state
variables of that step. Because several labels can fire per step and their
intermediate factor states are not encoded, `extractSolution` must
**reconstruct** intermediate states per factor by DFS over the transition
system (`bdd_state_reconstruction_dfs`).

See **Options** below for the modes this exposes.

### Status

Re-integrated and building (commits `270a9fc94`, `ac461fec3`). Registered as
the encoder plugin **`bdd_sat(...)`**. `label_sat` is untouched.

The file had been excluded from the build in `6f20300de` right after the
`SATEncodingFactory` refactor. It was not simply stale: it had never been
finished. Its `encode()` built a formula, called `ipasir_val()` on a solver
that had never been passed to `ipasir_solve()`, and discarded the extracted
plan (`// likely check_goal_and_set_plan with four arguments`). The port had
to supply the solution path, not just translate the old one.

Structure now mirrors `label_sat`:

```
BDDSATEncodingFactory  options, LabelOrderFinder, the Cudd manager, all BDD
                       construction and the node in-degrees. Once per --search.
BDDEncodingData        the result of that, shared read-only (shared_ptr<const>)
                       with every encoding instance.
BDDSATEncoding         one SAT call: label vars, Tseitin translation of the
                       factor BDDs, plan extraction with DFS state
                       reconstruction. Derives from LabelEncoding, so state
                       vars, exactly-one, encodeInit/encodeGoal come for free.
```

No frame axioms are emitted, and none are needed: constraining *every*
`(source,target)` pair — including impossible ones, whose BDD is `bddZero`
and which therefore emit `¬prev_s ∨ ¬next_ss` — already is the frame
constraint. `encodeStateEquals` is inherited from `StateEncoding` and is
correct, so `incremental_sat` is not specifically blocked (it cannot run
against kissat regardless, which has no `ipasir_assume`).

`tseitsinVars` is per encoding instance and is cleared per factor per time
step — it **must** be, since the Tseitin variable of a BDD node stands for
that node evaluated against *this* time step's label variables.

### Validation status

1846 runs, every plan checked — PDDL tasks with VAL, FTS tasks with
`experiments/validate_sas_plan.py` — with **no invalid plan and no assertion
failure anywhere**:

| batch | transform | runs | VALID | TIMEOUT |
|---|---|---:|---:|---:|
| 61 PDDL instances x 12 configs | `-shr` | 732 | 657 | 75 |
| 31 PDDL instances x 6 configs | `-ntr` (atomic FTS) | 186 | 173 | 13 |
| 58 FTS-benchmark instances x 8 configs x 2 | `-ntr` and `-shr` | 928 | 543 | 385 |

36 domains in total (30 PDDL + the six FTS-benchmark domains), `sat()` with
`one_by_one()` and `rintanen()` with `by_iteration()`, covering
`one_step_only`, `combinebdds`, `impltseitsin`, `omitforcedvariables`,
`bdd_size_limit`, `cutbdds` and the label orders. Timeouts are a 40s limit;
on the PDDL side the four worst (`hanoi/pfile11`, `grid/prob03`,
`freecell/pfile11`, `depot/pfile11`) time out for all twelve configurations,
`label_sat` included, and on the FTS side they concentrate in `rubiks-cube`
(124/160) and `cavediving-adl14` (94/160), which is task difficulty at 40s.

**On the FTS benchmarks `bdd_sat` beats `label_sat`**, which is the first
result that argues for the encoding rather than merely clearing it. Comparing
`bdd_sat(one_step_only=false)` against the `label_sat` chains baseline on the
same instance and transform:

| transform | both | only `bdd_full` | only `label_sat` | neither |
|---|---:|---:|---:|---:|
| `-ntr` | 33 | **5** | 2 | 18 |
| `-shr` | 35 | **4** | 0 | 19 |

With `-shr` the `bdd_full` coverage is a strict superset. `combinebdds=true` is
level with it (37/39 vs 38/39 solved). This is the opposite of the PDDL
picture, where `label_sat` is ahead — worth understanding before drawing
conclusions either way.

The DFS intermediate-state reconstruction — the part of `extractSolution` with
no counterpart in the other encodings — is exercised properly by
`one_step_only=false`, which packs several labels into one time step:
2.5 labels/step on gripper and driverlog, 4.5 on openstacks, 7.5 on
woodworking, 8.5 on freecell and 10 on ferry (a single time step holding the
whole plan). All VAL-valid.

Two instances where `bdd_sat` is clearly weaker than `label_sat` and worth
investigating: `mprime/prob03` (label 15 steps, `bdd_full` 88) and
`grid/prob01` (label 40, `rint_full` 58, most bdd configs time out).
Generally `bdd_full` finds *longer* plans than `label_sat` at the same number
of time steps — 82 longer vs 25 shorter vs 110 equal over the matrix — which
is the expected cost of packing more labels per step.

### Options (`bdd_sat(...)`)

| option | default | meaning |
|---|---|---|
| `one_step_only` | `true` | one real transition per factor per step (plus self loops) vs. the full all-pairs reachability DP over the label order |
| `combinebdds` | `false` | one BDD per factor over (state, next state, labels) vs. one BDD per (factor, s, ss) conditioned on the state vars |
| `bdd_size_limit` | `-1` | per-factor node budget; oversize `(s,ss)` BDDs fall back to the one-step BDD. Rejected with `one_step_only=true`, where it means nothing |
| `impltseitsin` | `true` | implicational instead of bi-implicational Tseitin |
| `omitforcedvariables` / `forcedvariablesthreshold` | `true` / `100` | skip Tseitin vars for nodes with in-degree ≤ threshold |
| `cutbdds` | `false` | fixpoint "cutting" of BDDs across factors |
| `coverbdds` | `false` | **diagnostic only**: reports always-true/false `state -> ±label` implications and does not change the formula |
| `label_order` | `label_order_linear()` | `LabelOrderFinder`; this *is* the BDD variable order |
| `force_at_least_one_action` | `false` | as elsewhere |

CUDD manager parameters (`cudd_init_nodes`, cache size, memory) are still
constants in the header marked `// TODO: read from command line arguments`.

### Bugs fixed during the port

Worth knowing about, because several of them would have silently produced
wrong measurements rather than crashes:

1. **`one_step_only` could not express an empty time step.** Every disjunct
   of the one-step BDDs forces its own label true — including the self-loop
   disjuncts — so a factor could never stay put unless some label fired. The
   formula was therefore UNSAT for *every horizon longer than the shortest
   one*. That silently breaks `by_iteration()`, `constant()` and rintanen's
   length scheduling, all of which probe non-consecutive lengths. A "no real
   transition" disjunct was added. Regression check: with
   `length_strategy=constant(plan_length=N)` on parcprinter p01, both
   `one_step_only` settings must be SAT for every `N >= 8`.
2. **The BDD variable order was not the label order.** The DP ran in
   `labelOrder` but indexed CUDD variables by label *id*
   (`bddVar(label + num_factor_vars)`), while `bdd_to_dot` labelled variable
   `i` as `labelOrder[i]`. Comparing label orders was thus measuring nothing
   about the variable order. There is now an explicit `labelToBDDVar`, and
   label order == BDD variable order as documented.
3. **`one_step_only` + `combinebdds=false` read the wrong structure.**
   `initialize()` filled only the one-step BDDs, `encode()` read the
   full-reachability ones, which were empty. Both now feed one structure.
4. **`tseitsinVars` / `node_indegree` were file-scope globals.** Tseitin ids
   are per capsule, so under `rintanen` — which interleaves encodings — they
   leaked between SAT calls. Note the threads are *cooperative*: only one
   runs at a time, so this was cross-contamination, not a data race, and it
   would have produced wrong formulas rather than a crash. `node_indegree`
   also accumulated across calls, drifting the `omitforcedvariables`
   decisions.
5. **`cutbdds` ran inside the per-factor loop**, recomputing its fixpoint
   once per factor and, on the first pass, over factors not yet built.
6. **`coverbdds` ended in `exit(0)`** and its result was never read.
7. **`bdd_size_limit`** is now rejected with `one_step_only`.


### Label dependencies extractable from the BDDs

`bdd_sat(report_label_implications=true)` (diagnostic, default off) mines each
factor's label BDD for dependencies that hold in *every* legal label set, so
they are sound clauses over any time step's label variables.

The union over all `(source,target)` pairs of a factor's transition BDDs is a
BDD over label variables describing every label set that factor permits in one
step. `Cudd_FindEssential` returns the cube of variables forced in every
satisfying assignment, so applying it to that function gives the unit
dependencies and applying it to the cofactor by a literal gives **every**
binary dependency with that literal as premise in one call. Extraction is
therefore O(|support|) cofactor+essential calls per factor, not O(L²), and the
support is only the labels relevant to the factor.

What is actually there, with every claimed mutex re-checked by a direct
`any AND l AND l' == 0` test (287288 checked, **0 refuted**):

| instance | labels | mode | unconditional distinct `l -> -l'` |
|---|---:|---|---:|
| `cavediving/testing18A_easy` | 804 | full | **114022** |
| `cavediving/testing18A_easy` | 804 | one-step | **153538** |
| `matrix-mult/mm2x2X2x1` | 135 | one-step | 4220 |
| `matrix-mult/mm2x2X2x1` | 135 | full | 0 |
| `rubiks-cube`, `pancakes`, `burnt-pancakes`, `topspin` | 2-18 | both | 0 |

On cavediving that is ~35% of all label pairs, extracted in 11s against 0.03s
of construction. The zeros are a property of the domains, not of the method:
in the permutation puzzles every label is applicable in every state, so the
union is *identically true* and the BDDs carry no unconditional label
information at all — everything they know is about *which* state pair they
connect. Conditioning on the source state does not help there either. Check
`BDDSTAT implications_shape`'s `unconditional_informative` count first; it is
milliseconds and tells you whether extraction is worth running.

Caveat: these are consequences of constraints the encoding already emits, so
adding them as clauses buys propagation speed, not a smaller search space. The
untested and more promising use is to `Restrict`/`Constrain` the factor BDDs by
the implication care-set *before* the Tseitin translation, since `nodes_sum`
drives CNF size directly.

### Where BDD construction breaks

Measured over all 431 FTS-benchmark instances with the `BDDSTAT` output and
`bdd_init_time_limit` (30s screen, then 10 min on everything that failed).
The result is the opposite of what the naming suggests.

**The full transition relation is always constructible.** 430 of 431 build
(the 431st, `pancakes/n5-p2`, has goal = initial state, so nothing is built),
and 427 of them in under 2.7s. Only three need more than 30s, and they finish
comfortably inside 10 minutes:

| instance | labels | factors | time | nodes_sum |
|---|---:|---:|---:|---:|
| `matrix-multiplication/mm2x2X2x3` | 59535 | 144 | 65s | 4.72M |
| `matrix-multiplication/mm2x3X3x2` | 59535 | 144 | 66s | 4.72M |
| `matrix-multiplication/mm3x2X2x2` | 59535 | 144 | 66s | 4.72M |

Note these are *not* the largest files, and the smaller `mm2x2X2x2` builds in
full mode without trouble. At a 30s budget they die in `full_reachability_dp`
around factor 39-47 of 144 with 250k-330k live nodes.

**`one_step_only=true` was the fragile mode, and mostly for a fixable reason.**
As originally written it could not build 17 instances in 10 minutes. After the
rewrite in `8d5b39a2d` (prefix/suffix cubes, see below) **14 of those 17 build**,
most in under a minute; `testing10/11/12_easy` went from not reaching factor 12
of 453 to completing in ~547s, and `testing09_easy` in 403s.

The original cost was *not* BDD blow-up — live node counts at abort were 8k-48k.
It was the number of BDD conjunctions: one per label that has to be forced
false, for every transition, and the transition count is itself around |L|·|S|
because a label that is irrelevant for a factor still has |S| explicit
self-loops there. The nested rescanning of transition lists looked like the
culprit and is not: removing it alone made things *slower*. What fixed it was
emitting two BDD operations per transition instead of |forced-false| of them.

What that costs and buys, sequential 3-repeat medians against the
pre-rewrite build (`one_step_only=true`):

| instance | labels | before | after | |
|---|---:|---:|---:|---|
| `topspin/n12-k6-p1` | 2 | 0.27ms | 0.66ms | +0.4ms |
| `pancakes/n8-p0` | 7 | 0.37ms | 0.66ms | +0.3ms |
| `rubiks-cube/s3-t9-p3` | 18 | 10.4ms | 11.6ms | +1.2ms |
| `cavediving/testing08_easy` | 496 | 6880ms | 207ms | **-6.7s** |

So it is a fixed overhead of a few hundred microseconds per factor that pays
for itself above roughly 50 labels. The overhead is building the `FTSMatrix`
(six sparse index structures and their complements), which below that replaces
a scan that was nearly free. Both are linear in the label count.

Do **not** trust ratios from parallel sweeps over the whole suite here: 172 of
the 410 instances construct in under 2ms, and two sweeps of *identical* code at
`-P 6` gave medians of x2.03 and x0.64. The commit message of `8d5b39a2d`
quotes "median x2.0", which came from such a sweep and does not hold — the
table above, measured sequentially with repeats, is the real picture. Equivalence
(`nodes_sum_all_factors`, `nodes_max_pair`) was identical in all four sweeps, so
only the timings were affected.

The three remaining instances — `matrix-multiplication/mm2x2X2x3`,
`mm2x3X3x2`, `mm3x2X2x2` — are a genuine BDD blow-up and not worth chasing with
more budget. On `mm2x2X2x3`, one-step dies at factor **2 of 144** after 300s
with **33.6M live nodes** (53M peak, 2.5GB), while *full reachability builds the
whole task in 1.67s* with 916k live nodes. For these tasks the one-step
semantics is simply the wrong representation.

For the record: the empty-step disjunct added during the port is a separate
phase (`one_step_stay_disjunct`) and none of the failures occur in it — they all
report `where one_step_construction`.

**What full mode actually costs is formula size, not constructibility.**
`nodes_sum_all_factors` is the summed `nodeCount()` over the per-(source,
target) BDDs, i.e. exactly what gets Tseitin-translated, so it predicts CNF
size. Full is a median **4.5x** larger than one-step, up to 10x. Per-domain
medians for full: `burnt-pancakes` 148k, `cavediving-adl14` 399k, `pancakes`
85k, `rubiks-cube` 204k, `matrix-multiplication` 2k. The extremes are
`cavediving-adl14/testing10_easy` (6.47M nodes, largest single pair 24150,
1.05M Tseitin nodes, built in 2.05s) and the three matrix-multiplication
instances above. Note `burnt-pancakes/n22-*` reaches 1.90M summed nodes with
only 506 Tseitin nodes — heavy structural sharing, so summed nodes overstates
its CNF size; compare the two columns rather than trusting either alone.

### Investigating the encoding

`bdd_sat` builds and validates, so the goal now is to *understand* the
encoding, not just run it. Useful directions and where the hooks already are:

* **Size and structure.** `initialize()` already prints per-factor BDD node
  counts, `possibleSingleTrans` vs `allTrans`, and sizes before/after
  `bdd_size_limit`. Turn these into structured output the parsers can pick up
  (clauses/vars per factor, Tseitin vars omitted, in-degree histogram).
* **Label order sensitivity.** BDD size is dominated by the label order
  (= BDD variable order). Compare `label_order_linear`, `_reverse`, `_random`
  (see `experiments/2025-07-09-bdd-other-orders.py`) and consider a
  causal-graph / `FTSMatrix`-informed order. CUDD dynamic reordering
  (`Cudd_AutodynEnable`) is not used — try it and record the resulting order.
* **One-step vs. full reachability.** `one_step_only=true` gives ∃-step-like
  parallelism restricted to one "real" move per factor per step; `false`
  allows arbitrary label sequences within a factor per step (much stronger,
  much bigger). Measure plan-length (number of steps) vs. formula size vs.
  solving time. `bdd_size_limit` interpolates between them. First data point,
  parcprinter p01 at the length where it first becomes SAT: one-step 11827
  clauses / 1844 vars, compression 1.0; full reachability 6180 / 427,
  compression 1.6 — i.e. the *bigger* semantics gave the *smaller* formula
  here, because the one-step BDDs enumerate one disjunct per transition.
* **Tseitin variants.** `impltseitsin` + `omitforcedvariables` +
  threshold: quantify clause/var savings and impact on kissat.
* **`cutbdds` / `coverbdds`.** Still never systematically evaluated. Both now
  run and `cutbdds` produces VAL-valid plans, but on the small instances tried
  so far it barely moves formula size (parcprinter p01: 6180 -> 6300 clauses,
  i.e. slightly *worse*). `coverbdds` is diagnostic-only and changes nothing.
* **Comparison baseline.** `label_sat(encoding=CHAINS_PARALLEL,...)` with the
  "good configurations" from `experiments/2025-11-0x-good-configurations*.py`
  and the `rintanen` settings above.
* `bdd_to_dot` exists on the factory (writes `.dot` of a BDD with named
  variables) — handy for eyeballing small factors. Nothing calls it; add a call
  in `initialize()` while debugging.

## Instances that take 10-30 seconds

Most of the IPC suite is useless for comparing encodings: it is either trivial
or hopeless, with very little in between. Screening 251 instances across 25
domains gave 64 under one second, 21 between one and ten, **6** between ten and
thirty, 5 between thirty and seventy, and 53 over seventy. Finding the middle
means sampling around the instances that already land near it, not sampling the
suite uniformly.

These are the ones in the band, at most two per domain, horizons from 2 to 17:

| instance | seconds | horizon |
|---|---:|---:|
| `openstacks-opt08-strips/p10` | 11.4 | 7 |
| `airport/p16-airport3-p4` | 11.5 | 17 |
| `tpp/p18` | 11.7 | 3 |
| `rovers/p29` | 13.0 | 2 |
| `nomystery-opt11-strips/p08` | 14.0 | 11 |
| `trucks-strips/p05` | 14.4 | 10 |
| `rovers/p31` | 14.5 | 3 |
| `scanalyzer-08-strips/p05` | 15.2 | 3 |
| `openstacks-opt08-strips/p11` | 18.2 | 7 |
| `pathways-noneg/p29` | 22.7 | 9 |
| `storage/p14` | 24.4 | 5 |
| `trucks-strips/p07` | 24.4 | 9 |
| `satellite/p24-HC-pfile4` | 27.0 | 2 |
| `airport/p17-airport3-p5` | 27.6 | 17 |
| `storage/p15` | 28.5 | 4 |
| `tpp/p22` | 28.6 | 3 |
| `pathways-noneg/p26` | 29.7 | 9 |

**Measured with `sat(encoder=bdd_sat(one_step_only=false), length_strategy=one_by_one())`,
the `-shr` transform, and no h2 preprocessing.** That matters: the band belongs
to that configuration, not to the instances. `label_sat`, the reverse label
order and the h2-preprocessed versions all sit somewhere else -- h2 alone took
pipesworld from 4810 to 3920 clauses per time step. Re-measure before reusing
these for a different encoding.

"horizon" is the number of time steps at which the formula first becomes
satisfiable, i.e. `ENCSTAT length` for the successful SAT call. It is not the
plan length: this encoding packs several labels into one step by design, so it
returns longer plans at shorter horizons, and plan length is the wrong thing to
compare between encodings or label orders.

## Conventions and gotchas

* C++ code style is loose FD style with tabs in the `sat/` directory; match
  the surrounding file. Do not run a formatter over whole files.
* Use `DEBUG(sat->registerVariable(v, "name@t"))` for every new SAT variable
  so `debug64` output stays readable.
* Do not `cout` from per-call encodings in tight loops; `rintanen` runs up to
  `max_parallel_calls` encodings concurrently and interleaved output breaks
  the Lab parsers. Statistics go through the factory (`statisticsPrinted`
  pattern in `LabelBasedEncodingFactory`).
* Anything the experiment parsers depend on (`Formula has ... clauses and ...
  variables.`, `SAT init time:`, `Transform time:`, kissat's summary lines) is
  load-bearing; keep the wording.
* `experiments/` scripts reference cluster paths and `DOWNWARD_BENCHMARKS`;
  they are not runnable locally without Lab and the benchmark checkout.
* Generated files ignored by git: `output.sas`, `sas_plan*`, `builds/`.
* Never commit a `SAT_DIR` that points at a personal home directory as the
  "fix" — make it overridable (`build.py` passes extra `-D` flags through).

## Git workflow for this work

* Base branch: `development-and-testing`. Do not push to it directly.
* Work on branch **`bdd`**; commit in small, buildable steps
  (wiring → header → factory/encoding split → plugin → validation).
* Merge into `main` only after `label_sat` results are unchanged and
  `bdd_sat` validates on the debug instance and a handful of FTS benchmarks.
