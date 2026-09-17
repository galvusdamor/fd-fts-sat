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

Known rough edges, both **pre-existing and not specific to any encoding**
(`label_sat` reproduces them identically), so don't chase them when a new
encoding seems to misbehave:

* `debug64` aborts on some tasks in the `FTSTask` constructor —
  `assert(distances.get_goal_distance(s) < numeric_limits<int>::max())`
  (`task_representation/fts_task.cc:87`), e.g. on parcprinter p01. Assertion
  coverage has to come from tasks that get past it.
* On some tasks (e.g. `miconic/s1-0`) the plan is printed and "Solution found."
  is reported but no plan file is written, so VAL has nothing to check.

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
