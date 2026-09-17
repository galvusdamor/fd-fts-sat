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
  (`/home/alvaro/projects/joao/kissat-p/build`). Point it at a local build of
  the *patched* kissat: `rintanen_search.cc` calls
  `kissat_set_external_scheduler(...)`, which only exists in that fork. Without
  `USE_CUSTOM_KISSAT`, `sat/kissat_dummy.cc` provides no-op stubs so the code
  links, but Rintanen's scheduler will not actually interrupt solvers.
* The SAT plugin is `PLUGIN_SAT_SEARCH` (engines) and `SAT_SEARCH`
  (dependency-only library) in `src/search/DownwardFiles.cmake`. Add new
  encoding source files to the `SAT_SEARCH` `SOURCES` list; the extension `.cc`
  is implied.
* **CUDD is only compiled when `PLUGIN_SYMBOLIC_ENABLED` is true**
  (`src/search/CMakeLists.txt`, `if(PLUGIN_SYMBOLIC_ENABLED)` block). The
  default config sets `-DPLUGIN_SYMBOLIC_SEARCH_ENGINE_ENABLED=FALSE`, so in a
  stock `release64` build the CUDD headers are not on the include path and
  `libcudd.a` is not linked. Anything in `sat/` that includes `cuddObj.hh`
  therefore needs the CUDD block made available to `SAT_SEARCH` too (see
  "BDD encoding" below).
* Header include guards are hand-written and **collide**:
  `SEARCH_ALGORITHMS_SAT_SEARCH` is used by `sat/label_based_encoding.h`,
  `sat/bdd_encoding.h` and `search_engines/sat_search.h`. Including two of them
  in one TU silently drops the second. Use unique guards (or `#pragma once`) in
  anything you touch.
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
* `--debug` builds and runs `debug64`, which prints registered SAT variable
  names (`sat_capsule::registerVariable`) — very useful when validating an
  encoding.
* Quick sanity check of the SAT stack without Rintanen's threads:
  `--search "sat(encoder=label_sat(...),length_strategy=one_by_one())"`.

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
| *(none yet)* | `sat/bdd_encoding.cc` | **Disabled**, see below. |

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

Modes and options (from the pre-refactor `plugin_sat.cc`, commit `7199ca871`):

| option | default | meaning |
|---|---|---|
| `combinebdds` | false | one BDD per factor over (state, next-state, labels) vs. one BDD per (factor, s, ss) pair conditioned on state vars |
| `considerOnlyOneStepTransitions` | `true` (hard-coded) | only "one real transition + self-loops before/after" per step, vs. full all-pairs reachability DP over the label order |
| `bdd_size_limit` | -1 | per-factor node budget; oversize (s,ss) BDDs fall back to the one-step BDD |
| `impltseitsin` | true | implicational (one-directional) Tseitin instead of bi-implicational |
| `omitforcedvariables` / `forcedvariablesthreshold` | true / 100 | skip Tseitin vars for BDD nodes with in-degree ≤ threshold |
| `cutbdds` | false | fixpoint "cutting" of BDDs across factors for stronger constraints |
| `coverbdds` | false | search for always-true/false `state -> ±label` implications to shrink BDDs |
| `label_order` | `label_order_linear()` | `LabelOrderFinder` plugin; the BDD variable order == label order |

CUDD manager parameters (`cudd_init_nodes`, cache size, memory) are constants
in the header marked `// TODO: read from command line arguments`.

### Why it does not build today

It was excluded from `DownwardFiles.cmake` (`#sat/bdd_encoding`) in commit
`6f20300de "Disabled BDD"` immediately after the `SATEncodingFactory`
refactor (`a29285cd0`). Concretely it is stale in these ways:

1. **Wrong base class / interface.** It derives from a non-existent
   `SAT_encoding` and implements `initialize()` + `encode(int currentLength,
   int stepTimeLimit)`. The current interface is
   `SATEncodingFactory` (with `initialize`, `createEncodingInstance`) +
   `SATEncoding` (with `encode(from,to)`, `encodeInit`, `encodeGoal`,
   `encodeStateEquals`, `extractSolution`).
2. **Owns its own solver.** `encode()` calls `ipasir_init()`, builds
   `sat_capsule capsule;` (default ctor gone), emits init and goal itself, and
   reads the model with `ipasir_val`. All of that now belongs to the engine.
3. **Free clause helpers.** Uses `atMostOne(solver, capsule, v)`,
   `atLeastOne(solver, capsule, v)`, `assertYes(solver, v)`,
   `notAll(solver, v)`, `andImplies(solver, ...)`, `reset_number_of_clauses()`,
   `get_number_of_clauses()`. These are now methods on `sat_capsule`.
4. **Options plumbing.** Ctor takes `(const Options&, shared_ptr<FTSTask>)`
   and expects the option names above to exist on the *search engine*; they
   were removed from `plugin_sat.cc`. The `label_order` option and
   `labelOrder = label_order_finder->find_order(*fts)` call are commented out,
   so `labelOrder` is empty and every `labelOrder[l]` access is UB.
5. **Build wiring.** CUDD include dirs / `libcudd.a` are only added under
   `if(PLUGIN_SYMBOLIC_ENABLED)`; `SAT_SEARCH` does not depend on `SYMBOLIC`.
6. **Header guard collision** with `label_based_encoding.h` / `sat_search.h`.
7. Global state: `map<DdNode*,int> tseitsinVars, node_indegree` are
   file-scope globals (marked `TODO Get rid of global variables!`). With
   `rintanen` running several encodings concurrently in threads this is a
   data race; they must become members of the per-call encoding object.
   Similarly `exceptionError`/`exitOutOfMemory` handlers and the CUDD manager
   are per-factory state that multiple threads will share — CUDD managers are
   not thread-safe.

### Re-integration plan (branch `bdd`)

Do this incrementally; keep `label_sat` behaviour untouched and verify it
still produces identical plans before and after each step.

1. **Build wiring.** In `src/search/CMakeLists.txt`, make the CUDD
   `ExternalProject` / include / link block fire when
   `PLUGIN_SYMBOLIC_ENABLED OR PLUGIN_SAT_SEARCH_ENABLED` (or introduce a
   `USE_CUDD` option set by both). Re-add `sat/bdd_encoding` to the
   `SAT_SEARCH` sources in `DownwardFiles.cmake`. Note the CUDD configure
   step requires `automake`/`autoconf` (`aclocal && autoheader && automake
   --add-missing && autoconf`).
2. **Fix the header.** Unique include guard; forward-declare what you can;
   include `cuddObj.hh` only in the header that needs it.
3. **Split into factory + per-call encoding**, mirroring `label_sat`:
   * `BDDSATEncodingFactory : SATEncodingFactory` — holds options, the
     `LabelOrderFinder`, `labelOrder`, the `Cudd` manager and the
     precomputed `transition_BDDs_*` (everything now in
     `BDDSATEncoding::initialize()`), plus the one-off in-degree computation.
     Register as plugin `bdd_sat(...)` with the options in the table above
     (`bdd_size_limit`, `impltseitsin`, `omitforcedvariables`,
     `forcedvariablesthreshold`, `combinebdds`, `cutbdds`, `coverbdds`,
     `label_order`, `force_at_least_one_action`, and expose
     `one_step_only` for `considerOnlyOneStepTransitions`).
   * `BDDSATEncoding : StateEncoding` (or `LabelEncoding`) — per SAT call.
     Reuse `StateEncoding` for state variables, exactly-one constraints and
     `encodeInit`/`encodeGoal`; keep only label vars, `bdd_to_cnf`, the
     Tseitin maps (as members) and `extractSolution` here.
     `encode(fromTime,toTime)` = for each step: generate label vars, next
     state vars, then `bdd_to_cnf` per factor with the current step's SAT
     vars. Replace all `xxx(solver, capsule, ...)` calls with `sat->xxx(...)`.
   * `extractSolution` reads the model via `ipasir_val(sat->solver, var)` and
     returns the `tuple<PlanState, vector<PlanState>, vector<int>, set<int>>`
     expected by the engines; port the DFS state reconstruction and the
     label re-ordering by `labelOrder` (labels within a step must be listed in
     BDD-order for the plan to be valid).
   * `encodeStateEquals` can `assert(false)`/throw "not supported by BDD
     encoding" initially (needed only by `incremental_sat`).
4. **Thread safety for `rintanen`.** CUDD objects live in the factory and are
   read-only after `initialize()`. `bdd_to_cnf` only reads `DdNode*`
   structure via `Cudd_T/Cudd_E/Cudd_IsConstant`, which is safe if no other
   thread mutates the manager — so: never create BDDs after `initialize()`
   returns, and make `tseitsinVars`/`node_indegree` per-encoding (or
   precompute the in-degree map once in the factory and copy it).
   Alternatively start by only supporting `sat(...)` and add `rintanen`
   support once the single-threaded path is validated.
5. **Validate.** Compare against `label_sat` on small instances
   (parcprinter p01, a few FTS benchmark instances): same plan length per
   `length_strategy` step, plans validated with VAL, no assertion failures in
   `debug64`. Check clause/variable counts print as
   `Formula has N clauses and M variables.` — `experiments/sat_parser.py`
   greps for that exact string.
6. **Experiments.** Add a dated script in `experiments/` following the
   existing pattern (`common_setup.IssueConfig`, `fts_parser`, `sat_parser`,
   `REVISION = <sha>`), pinned to a commit on `bdd`.

### After re-integration: investigating the encoding

Once `bdd_sat` builds and validates, the goal is to *understand* the encoding,
not just run it. Useful directions and where the hooks already are:

* **Size and structure.** `initialize()` already prints per-factor BDD node
  counts, `possibleSingleTrans` vs `allTrans`, and sizes before/after
  `bdd_size_limit`. Turn these into structured output the parsers can pick up
  (clauses/vars per factor, Tseitin vars omitted, in-degree histogram).
* **Label order sensitivity.** BDD size is dominated by the label order
  (= BDD variable order). Compare `label_order_linear`, `_reverse`, `_random`
  (see `experiments/2025-07-09-bdd-other-orders.py`) and consider a
  causal-graph / `FTSMatrix`-informed order. CUDD dynamic reordering
  (`Cudd_AutodynEnable`) is not used — try it and record the resulting order.
* **One-step vs. full reachability.** `considerOnlyOneStepTransitions=true`
  gives ∃-step-like parallelism restricted to one "real" move per factor per
  step; `false` allows arbitrary label sequences within a factor per step
  (much stronger, much bigger). Measure plan-length (number of steps) vs.
  formula size vs. solving time. `bdd_size_limit` interpolates between them.
* **Tseitin variants.** `impltseitsin` + `omitforcedvariables` +
  threshold: quantify clause/var savings and impact on kissat.
* **`cutbdds` / `coverbdds`.** Both are experimental and were never
  systematically evaluated; establish whether they produce correct, smaller
  encodings before drawing conclusions.
* **Comparison baseline.** `label_sat(encoding=CHAINS_PARALLEL,...)` with the
  "good configurations" from `experiments/2025-11-0x-good-configurations*.py`
  and the `rintanen` settings above.
* `bdd_to_dot` exists (writes `.dot` of a BDD with named variables) — handy for
  eyeballing small factors; calls are commented out in `initialize()`.

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
