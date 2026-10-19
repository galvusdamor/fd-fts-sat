# The BDD encoding (`bdd_sat`): state of 2026-10-17

What `bdd_sat` is, every variant and add-on implemented so far, how each one did on
the Autoscale benchmark set, and what is open. The background (the port, construction
limits on the FTS benchmarks, label orders) is in `CLAUDE.md`. The option reference is
`docs/bdd-sat/bdd-sat.tex`, and its tables are generated from the sources.

Branch `bdd`. Code: `src/search/sat/bdd_encoding.{h,cc}`. Last code commit:
`c47442c04`. The FTS transformations of §5 are on branch `fts-transformations`
(`164cb4563`, `5d15ee4a9`, `d17d456d7`, `eb715adac`). Everything below is **off by default** unless stated otherwise.

## 1. The encoding in one paragraph

For every factor i and every state pair (s, s'), B_i(s, s') is a BDD over the label
variables. It is true exactly for the label sets that, fired in the fixed **label
order**, move factor i from s to s' within one time step. With
`one_step_only=false` ("full reachability", which is what we use) a factor may make
several transitions per step. With `true` it makes at most one real transition, framed
by self-loops. Each BDD is Tseitin-translated and asserted under `prev_s ∧ next_s'`. A
pair whose BDD is false gives `¬prev_s ∨ ¬next_s'`, so no frame axioms are needed. Plan
extraction reconstructs each factor's intermediate states by DFS.

**Baseline** ("no cutting") throughout this document:

```
sat(encoder=bdd_sat(one_step_only=false,
        label_order=label_order_goal_chains(ancestor_depth=2,goal_pairs=2,leftover_layer=true,
                    state_budget=200000,max_states=20000,exact_time_limit=1)),
    length_strategy=one_by_one())
```

The baseline uses split BDDs, implicational Tseitin, `omitforcedvariables`, threshold 1,
and the `-shr` transform: weak bisimulation, exact label reduction, no merging,
`prune_transitions_from_goal`.

## 2. Evaluation setup

* **Benchmarks:** Autoscale 21.11 agile STRIPS, 42 domains x 30 tasks = 1260
  (`~/data/autoscale-benchmarks`). The main campaign ran on all of them. Since then all
  comparisons use the **303-task set**: tasks solved by at least one main variant, minus
  tasks the transformation solves alone (`auto/solved303_{0..3}.txt`).
* **Limits and machine:** 300 s, 2500 MB, kissat-p 2.0.0, one task per physical core
  (4 jobs, i7-4790K). Nothing else runs on the machine during timing, HT siblings
  included.
* **Validation:** every plan is checked with VAL. Six domains VAL cannot parse
  (data-network, floortile, openstacks, organic-synthesis-split, parcprinter, pathways)
  are checked against the translated task with `experiments/validate_sas_plan.py`
  (`VALID_SAS`), via `scripts/revalidate_autoscale.py`. **No invalid plan in any run.**
* **Measures:**
  * solved;
  * PAR-2 (mean, 600 s per unsolved task);
  * geometric means of kissat **conflicts** and **total time** relative to the baseline,
    over tasks both solve where the baseline needs >= 5 s of solve time (n ≈ 47-62);
  * clauses per time step relative to the baseline.

  Conflicts come from kissat's internal counters, which are deterministic for a given
  formula and seed. They are the noise-free measure of search effort.
* **Raw data and scripts:** `/home/gregor/data/claude-jobs/abf93c5f/`. The runner is
  `scripts/horizon_eval.py` (all configurations by name), the results are
  `auto/results_*.csv` and the logs `auto/runs_*/`.

### Noise

Identical formulas give identical conflict counts, but run times vary by a factor of
0.83-1.31 (5th-95th percentile). Two runs of the baseline differ by 3 solved tasks (290
vs 293). **Coverage differences of up to about 4 tasks, and time ratios between about
0.9 and 1.1, are not evidence.** Conflicts are.

## 3. Results on the 303 tasks

Bold marks the best per column: most solved, lowest PAR-2, conflicts and time.
"+/-" counts tasks gained and lost against the baseline. Clauses/step for the h2 rows
are for a different (preprocessed) task.

| variant | solved | PAR-2 | conflicts | time | clauses/step | +/- |
|---|---:|---:|---:|---:|---:|---:|
| **baseline (no cutting)** | 290 | 55.5 | 1.00 | 1.00 | 1.00 | |
| baseline, repeat run | 293 | 52.0 | 1.00 | 0.99 | 1.00 | +3 / -0 |
| *Encodings* | | | | | | |
| combined BDDs, `combine_ite` | 285 | 64.3 | 1.04 | 1.16 | 1.12 | +5 / -10 |
| path encoding, combined `combine_ite` | 283 | 66.3 | **0.04** | 1.12 | 6.10 | +9 / -16 |
| path encoding, split, `path_sharing` | 282 | 68.6 | 0.08 | 1.20 | 4.93 | +9 / -17 |
| *Label implications* | | | | | | |
| uncapped | 289 | 54.6 | 0.50 | 0.89 | 1.26 | +5 / -6 |
| cap 10k | 295 | 48.2 | 0.85 | 0.94 | 1.09 | +6 / -1 |
| cap 30k | 293 | 51.2 | 0.85 | 0.98 | 1.12 | +7 / -4 |
| cap 100k | 292 | 51.9 | 0.80 | 0.96 | 1.16 | +5 / -3 |
| cap 300k | 294 | 49.0 | 0.70 | 0.90 | 1.20 | +6 / -2 |
| cap 1M | 294 | 48.5 | 0.63 | 0.90 | 1.25 | +6 / -2 |
| *State clauses* | | | | | | |
| source units | 294 | 47.7 | 0.82 | 0.94 | 1.08 | +7 / -3 |
| target units | 293 | 48.5 | 0.63 | 0.89 | 1.11 | +5 / -2 |
| pair units | 289 | 56.1 | 0.79 | 0.99 | 1.68 | +4 / -5 |
| cross units | 289 | 61.3 | 0.82 | 1.21 | 1.12 | +2 / -3 |
| leave clauses | 290 | 54.9 | 0.72 | 0.93 | 1.22 | +5 / -5 |
| enter clauses | 288 | 56.2 | 0.64 | 0.89 | 1.24 | +6 / -8 |
| goal-last | **296** | **45.8** | 0.92 | 0.96 | 1.00 | +8 / -2 |
| goal-last, repeat run | 294 | 49.3 | 0.92 | 0.96 | 1.00 | +6 / -2 |
| all state clauses, no budget | 294 | 50.2 | 0.39 | 0.94 | 1.81 | +8 / -4 |
| all state clauses, 10 s phased | 292 | 50.5 | 0.30 | 0.81 | 1.85 | +6 / -4 |
| all state clauses, 30 s phased | 293 | 52.3 | 0.31 | 0.91 | 1.86 | +7 / -4 |
| implications cap 100k + goal-last | 293 | 49.0 | 0.73 | 0.88 | 1.16 | +5 / -2 |
| *Cutting* | | | | | | |
| `AND` | 236 | 153.6 | 0.56 | 0.97 | 1.96 | +3 / -57 |
| `AND_LIMITED` | 282 | 65.9 | 0.90 | 0.92 | 1.00 | +2 / -10 |
| `RESTRICT` | 282 | 65.4 | 0.94 | 0.90 | 1.01 | +2 / -10 |
| `AND_CAPPED`, overlap >= 0.5 | 282 | 66.7 | 0.66 | 0.99 | 1.06 | +3 / -11 |
| `AND`, 30 s budget | 244 | 140.3 | 0.59 | 0.86 | 1.93 | +5 / -51 |
| `AND_LIMITED`, 30 s budget | 292 | 50.8 | 0.95 | 0.91 | 1.00 | +2 / -0 |
| `RESTRICT`, 30 s budget | 289 | 53.9 | 0.96 | 0.89 | 1.02 | +2 / -3 |
| `AND_CAPPED`, 30 s budget | 292 | 51.4 | 0.72 | 0.96 | 1.06 | +6 / -4 |
| *kissat `eliminate=0`* | | | | | | |
| baseline | 287 | 62.1 | 0.85 | 1.21 | 1.00 | +8 / -11 |
| goal-last | 283 | 66.6 | 0.81 | 1.17 | 1.00 | +5 / -12 |
| combined, `combine_ite` | 281 | 71.5 | 0.91 | 1.32 | 1.13 | +7 / -16 |
| implications cap 10k | 286 | 63.1 | 0.71 | 1.11 | 1.09 | +7 / -11 |
| implications cap 100k | 293 | 53.5 | 0.66 | 1.12 | 1.16 | +8 / -5 |
| all state clauses, 10 s | 289 | 57.6 | 0.33 | 0.96 | 1.85 | +5 / -6 |
| all state clauses, 30 s | 288 | 61.1 | 0.30 | 1.04 | 1.86 | +4 / -6 |
| *h2 preprocessing (`--transform-task preprocess-h2`)* | | | | | | |
| baseline | 283 | 60.6 | 0.57 | 0.71 | 0.87 | +3 / -10 |
| mutex clauses | 287 | 52.4 | 0.44 | 0.57 | 0.96 | +6 / -9 |
| mutex cutting | 283 | 62.3 | 0.56 | 0.88 | 0.87 | +2 / -9 |
| mutex clauses + all state clauses 10 s | 283 | 60.5 | 0.13 | 0.61 | 1.71 | +5 / -12 |
| mutex clauses + implications cap 300k | 290 | 46.3 | 0.28 | **0.52** | 1.16 | +7 / -7 |
| mutex clauses + all state + implications | 289 | 49.0 | 0.10 | 0.59 | 1.93 | +7 / -8 |

### What the table says

* **Nothing beats the baseline on coverage beyond noise.** Every variant within about
  4 tasks of 290-293 is a tie. goal-last's lead (296, repeat 294) is noise: its
  conflicts are 0.92 of the baseline's, and its two runs have identical conflict counts
  but solve different tasks near the limit.
* **Search effort does go down substantially.** The state clauses cut conflicts to 0.30,
  h2 with mutex clauses and all state clauses to 0.10, and the path encodings to 0.04.
  Time follows only partly, because the formulas grow.
* **Best overall trade-offs:**
  * capped implications (cap 300k: 294 / 49.0 / time 0.90) — the cheapest robust gain;
  * all state clauses with the 10 s phased budget (292 / 50.5 / conflicts 0.30 / time
    0.81);
  * h2 + mutex clauses + implications cap 300k (290 / **46.3** / conflicts 0.28 /
    **time 0.52**), the best PAR-2 and time so far. Its coverage is held back by
    nomystery (§4.7).
* **Cutting never pays off.** It only stops hurting once it is given a time budget.
* **Disabling kissat's variable elimination** hurts every configuration (time
  1.11-1.32x), although it rescues single domains (§4.8).

## 4. The variants

### 4.1 Combined BDDs and `combine_ite`

`combinebdds=true` builds one BDD per factor over (current-state vars, next-state vars,
labels) instead of one BDD per (s, s'). The original construction ORs |S|²
spelled-out minterms, each with about 2|S| negated state literals: |S|³ BDD operations.
On nomystery-sat11 p07 it did not finish in 300 s.

`combine_ite=true` builds an if-then-else chain instead, where the first true state
variable decides:

```
F   = ite(x_0, G_0, ite(x_1, G_1, ... ⊥))
G_s = ite(y_0, B(s,0), ite(y_1, B(s,1), ... ⊥))
```

It differs from the minterm version only on non-one-hot assignments, which the
exactly-one constraints exclude, so it is sound. The state variables lie above the
labels, so each `ite` is one node: |S|² constant-time steps, and 0.2 s on that task.

The result is still a worse encoding than split BDDs on every measure: conflicts 1.04,
time 1.16. Why combined mode needs more conflicts is unknown.

### 4.2 Path encoding (`path_encoding`, `path_sharing`)

This is NNFPath (Abío, Gange, Mayer-Eichberger, Stuckey, CPAIOR 2016). Plain Tseitin is
only *consistent*: a BDD that knows `l1 → ¬l2` does not propagate it. Path variables
("node/edge on the root-to-1 path") with clause P3 make unit propagation **domain
consistent**. Our extensions:

* P3 over edges that skip levels, because our BDDs are reduced and not quasi-reduced.
  Every horizon and plan agreed with Tseitin, but there is no written proof.
* Complement edges handled by node parity.
* `path_sharing`: one multi-rooted diagram per factor with root-selector variables,
  because CUDD shares nodes heavily across (s, s') BDDs. Without it the encoding is
  20-150x Tseitin's size.

Effect: 92-96 % fewer conflicts, but 5-6x the clauses per step, paid again for every
horizon. That costs coverage on long-horizon tasks (nomystery) and on tasks whose first
formula is already huge (airport, data-network, storage).

### 4.3 Label implications (`label_implications`, `_max`, `_time_limit`, `_bicliques`)

U_i = ∨ B_i(s, s') is every label set factor i permits in one step. CUDD's
`FindEssential` on U_i and on its cofactors gives every unit and binary clause implied
by U_i, with O(|support|) calls per factor. Since the empty step is always allowed,
nearly all of these are label mutexes `¬l1 ∨ ¬l2`. Modes are `UNCONDITIONAL`,
`PER_SOURCE` (guarded by `prev_s`; 6x the clauses, 86/100 solved on the medium IPC set,
dropped) and `BOTH`.

* Uncapped extraction costs too much: median 0.3 s, p90 44 s, max 283 s, up to 9M
  clauses, and out of memory on label-rich tasks (tidybot, parking, grid,
  organic-synthesis).
* The cap and time limit keep the clauses found so far; each one is sound on its own.
  Caps from 10k to 1M are all fine (292-295 solved). 300k is the default we compare
  with.
* `label_implications_bicliques` encodes the clauses 5-10x smaller but is slower:
  kissat handles binary clauses specially.

### 4.4 State clauses (`source_units`, `target_units`, `pair_units`, `cross_units`, `leave_clauses`, `enter_clauses`)

These are redundant clauses conditioned on state variables. Split BDDs only.

| option | clause |
|---|---|
| `source_units` | `prev_s → ±l` for l essential in U_i(s) = ∨_{s'} B_i(s, s') |
| `target_units` | `next_s' → ±l` for l essential in V_i(s') = ∨_s B_i(s, s') |
| `pair_units` | `prev_s ∧ next_s' → ±l` for l essential in B_i(s, s') and not already a source or target unit |
| `cross_units` | `prev_s1 ∧ prev_s2 → ±l` (likewise for next states), for literals essential in U_i(s1) ∧ U_j(s2) but in neither alone; at most `cross_pair_limit` = 10k state pairs per factor pair, factor pairs with most shared labels first |
| `leave_clauses` | frame part: `prev_s ∧ ¬next_s → ∨ leave(s)`; order part: `prev_s ∧ l → (a leaving label fires before l)` for every l without a transition from s, via one prefix chain per state |
| `enter_clauses` | the mirror image for the end state, with suffix chains |

* **Mover relevance.** A label with a self-loop everywhere can still move a factor. The
  first leave/enter version used `is_relevant_label` alone and was **unsound**. Fixed
  with the construction's test `is_relevant_label || !is_selfloop_everywhere`.
* **Phased budget.** `state_clauses_time_limit` (overall budget; the option's own
  default is 30 s) and `state_clauses_min_phase` (default 2 s). The phases run in the
  order leave/enter → source/target units → pair units → cross units. Every enabled
  phase still to come keeps its minimum, and what a phase found before stopping is
  kept.
* **Split frame clauses** (`move_frame_split`): ternary chains instead of the long frame
  clause, so the clauses stay short. `move_clause_parts=FRAME|ORDER|BOTH` is for
  experiments.

Individually, target units (0.63) and enter clauses (0.64) reduce conflicts most.
Together they reach 0.30, and the 10 s budget gives the best time of the no-h2 variants
(0.81).

### 4.5 `goal_last_label`

**Proposition.** If a task has a plan with at most m steps, it has one with at most m
steps whose last action moves some factor from a non-goal state into a goal state.
Otherwise the last action could be removed.

At the last time step, a label that can never move a factor into its goal states must
therefore be followed by a later label of that step: one suffix chain over the label
order. This is sound for every horizon and length strategy. It is cheap (clauses ×1.00)
and only mildly effective (conflicts 0.92).

### 4.6 Cutting (`cutbdds`, `cut_mode`, `cut_time_limit`, `cut_min_overlap`, `cut_limit_factor`)

What factor j cannot do from any state can never happen. Cutting conjoins (`AND`) or
restricts factor i's BDDs with factor j's union, projected onto i's labels, up to a
fixpoint. The modes are:

* `AND`;
* `AND_LIMITED`: kept only if not larger;
* `AND_CAPPED`: growth bounded by `cut_limit_factor`, kept even if larger;
* `RESTRICT`: `Cudd_bddRestrict`. This is only sound along the factor order: the first
  version let two factors restrict each other and accepted label sets neither allows;
* `GLOBAL_*`: the conjunction of all factors, which is too expensive to build.

Without a budget, every mode loses coverage to construction time (`AND` loses 57
tasks). `cut_time_limit` (`-1` = off) keeps whatever was cut when time runs out. With
30 s, `AND_LIMITED` loses nothing (292 / 50.8) but saves only 5 % of the conflicts.
`AND` still loses about 50 tasks, because its BDDs (and formulas) double. Cutting
removes few label sets that the encoding does not already rule out by itself.

### 4.7 Mutexes between factor states (`state_mutexes`, `mutex_cutting`, `mutex_source`)

* `state_mutexes` adds `¬prev_s ∨ ¬prev_t` for mutex states s, t of different factors
  at every step, and units for unreachable states.
* `mutex_cutting` cuts B_i(t, t') with what another factor permits from the states
  compatible with t and into the states compatible with t' (state-dependent cutting).
* Sources (`mutex_source`):
  * `SAS` (the option default): the finite-domain task's mutexes (translator mutex
    groups, or h2 when preprocessed), mapped through the transformation;
  * `FILE`;
  * `FTS_FIXPOINT`: our own h²-style fixpoint on the FTS. Implemented, but not used,
    since the existing h2 is the preferred source.

**Mapping SAS mutexes through the transformation.** An abstract state stands for a set
of concrete value tuples. A pair of abstract states is mutex only if **every** pair of
tuples they can stand for contains a mutex pair of facts. The sets are handed down through
**all** plan-reconstruction steps, in the order the transformations were applied.

Since `76a5b728c` every step does this itself: `PlanReconstruction::map_state_sets` is pure
virtual, so a new transformation does not compile until it decides how mutex information
passes through it, and a step that cannot map returns a reason (`STATEMUTEXES skipped:
...`) instead of producing wrong clauses. The mutex sources live in
`task_utils/state_mutexes.{h,cc}`; `bdd_encoding.cc` only turns the relation into clauses
and `mutex_cut`. The steps:

* merge-and-shrink: through the representation; a merge node takes the cross product of
  its children's tuples (merged factors), and the factors are renumbered by
  `factor_of_representation` unless a τ-path step of the same cleanup does it;
* τ-path: the **τ-closure**: every state that may arise during reconstruction via
  τ-labels must carry the mutex;
* `tau_baking`: τ-closure forward from the old state, renumbering after pruning;
* `label_closure`: identity (intermediate states lie between time points).

Earlier versions were unsound (wrong step order, representations of dropped factors,
pruned values). Checks: plans projected onto the claimed mutexes hit none, on 8 tasks with
`-shr` (with and without h2) and on 9 tasks with merging + `tau_baking` + h2; the refactored
module gives the same counts as before on `-shr`. A safeguard rejects any mapping that makes
initial states mutex. Debug environment variables: `FTS_MUTEX_DUMP`, `FTS_MAPPING_DUMP`,
`FTS_PLAN_STATES_DUMP`.

Findings:

* **Translator mutexes are rare after the transformation; h2 gives many.** With h2:
  nomystery p01 3004, storage p02 2656, pipesworld p03 5978. Without h2: 0, 120 and 0.
* **Operator counting** (an LP per state pair over label counts with flow constraints,
  prototype `scripts/opcount_mutexes.py`, HiGHS) found nothing that h2 does not find.
  Dropped.
* **h2 has a cost.** It shrinks conflicts (0.57) and time (0.71) by itself, mutex
  clauses add to that (0.44 / 0.57), and mutex cutting does not pay off. But it loses
  nomystery p03-p07 and p09, plus a few tasks to preprocessing time (transport p15) or
  transform time (childsnack).
* **The nomystery cause is confirmed** (`h2diag/`). h2 makes the *atomic* FTS slightly
  smaller (p03: 23,428 → 22,348 labels, 8,334 → 8,096 transitions). After the `-shr`
  transform, however, it is 2.5x larger: 207 → 236 labels, 8,328 → 21,241 transitions,
  all of it in the fuel factor (7,187 → 20,104). h2 removes the drive operators with
  fuel levels unreachable from a location. That breaks the regularity that let exact
  label reduction merge the drive labels across fuel levels. The BDDs grow 15-25x and
  the SAT variables per step go from ~5.5k to ~189k; horizon 2 alone then takes 110 s
  on p09.

### 4.8 kissat interaction (seeds, elimination)

* `FTS_KISSAT_SEED` and `FTS_KISSAT_OPTIONS` (e.g. `eliminate=0`) are environment
  hooks in `sat/kissat.cpp`.
* STEP lines report conflicts, decisions and propagations, plus solver vars, active,
  eliminated and fixed variables (`sat/kissat_stats.c`). Eliminated variables are
  counted via the import table.
* **Seeds:** the nomystery gain of the state clauses is real, and synergistic between
  clause kinds.
* **rovers:** the losses come from bounded variable elimination interacting with
  leave/enter clauses. `eliminate=0` rescues them, and so do short (split) frame
  clauses for leave. Losses coincide with *more* elimination, not less.
* **driverlog:** the losses are a real search effect of the enter order part and the
  target units. Split clauses reduce them but do not remove them.
* Global `eliminate=0` is worse for every configuration (table above). Tuning kissat per
  benchmark was ruled out as too hacky.

## 5. Transformations of the FTS (branch `fts-transformations`, 2026-10-16/17)

New task transformations, applied after `-shr` with `seq(transformations=[...])`. Each one
has its own plan reconstruction; `bdd_sat` itself is unchanged. Code in
`src/search/task_transformation/task_transformation_{seq,tau_baking,label_closure,dump}.*`.

### 5.1 What they do

* **`tau_baking()`.** A *tau label* of factor i is a self-loop in every state of every
  other factor. Tau moves commute with every label that is a self-loop everywhere in i,
  so they can be postponed to the next label that is not, or to the end of the plan. For
  every transition (s,l,s') of such a following label l in i, add (s'',l,s') for every s''
  from which s is tau-reachable. Every state from which a goal is tau-reachable becomes a
  goal. Then remove the tau labels and labels that are no-ops everywhere, prune states that
  became unreachable or dead, drop labels left without transitions, and repeat. It is
  epsilon-elimination: no new labels, more transitions per label. Unlike weak bisimulation
  it does not merge states, so it also removes tau labels on one-way tau paths, which
  shrinking cannot merge.
* **`label_closure()`.** For a label that moves one factor (self-loops elsewhere, possibly
  only in some states), add the transitive closure of its transitions in that factor: one
  closed transition stands for l^k. `multi_factor=true` also closes labels that move
  several factors. An l-path is *elastic* if it visits a state with an l-self-loop, so it
  can be stretched. This is sound if all moving factors are elastic (elastic closure
  everywhere), or if exactly one is not (full closure there). With two or more rigid
  factors the path lengths would have to match, which a product cannot express.
* **`dump_fts(filename, exit_after)`** writes the FTS. The dumps of the 303 tasks (atomic,
  `-shr`, `-shr` + `tau_baking`) are in `/home/gregor/data/fts-dumps/autoscale-21.11-agile-303/`.
  The reader and analyses are in `experiments/fts_dumps/`.

### 5.2 Where they apply (analysis of the dumps, 303 tasks)

* **Tau labels after `-shr`: 83 tasks** (pegsol 30, rovers 23, pathways 14, childsnack 13,
  openstacks 2, grid 1). On the atomic FTS there are 178 tasks. `-shr` removes all of them
  in gripper, elevators, transport, tpp and satellite (tau cycles, which weak bisimulation
  merges).
* **Closure applies almost never.** After `-shr` only 97 single-factor labels in 9 tasks
  have chains (driverlog, woodworking, openstacks), and only where label reduction merged
  labels. Of the labels that move several factors, about 336k are rigid in two or more
  factors and about 22.5k are rigid in exactly one, but with no chains there. One label is
  elastic. Multi-factor closure adds **no transition in any task**.
* **Locally driven factors** (moved only by labels that self-loop everywhere else):
  2,191 in 83 tasks after `-shr` (data-network 1,023, pathways 624, rovers 241,
  nomystery 110). Every one has a goal or is needed by other labels' preconditions. 1,471
  have their labels' preconditions in a single other factor; the product would have 4
  states at the median and at most 506. None of their labels is tau after `tau_baking`.

### 5.3 Results with `bdd_sat`, 303 tasks

300 s / 2500 MB, 4 jobs, all 1818 plans valid (VAL; data-network and floortile checked
separately). Horizon is summed over the tasks a configuration and `-shr` both solve;
conflicts and time are geometric means against `-shr` on those tasks where one of the two
takes at least 1 s.

| configuration | solved | PAR-2 | Σ horizon | clauses/step | conflicts | time | +/- |
|---|---:|---:|---|---:|---:|---:|---:|
| `-shr` | 292 | 53.7 | 838 | 1.00 | 1.00 | 1.00 | |
| + `tau_baking` | **297** | **45.2** | 838 -> 699 | 0.98 | 0.52 | 0.89 | +5 / -0 |
| + `label_closure` | 290 | 56.3 | 830 -> 824 | 1.00 | 0.98 | 1.01 | +0 / -2 |
| + `tau_baking` + `label_closure` | 296 | 46.3 | 831 -> 689 | 0.98 | 0.51 | **0.88** | +5 / -1 |
| + `tau_baking` + `-shr` | 293 | 52.1 | 824 -> 686 | **0.95** | 0.51 | 0.95 | +5 / -4 |
| + `tau_baking` + `-shr` + `label_closure` | 292 | 53.5 | 817 -> **676** | 0.96 | **0.50** | 0.95 | +5 / -5 |
| `-shr` **without label reduction** | 224 | 182.2 | 607 -> 615 | 6.39 | - | 3.78 | |
| ... + `tau_baking` | 231 | 168.4 | 609 -> 446 | 6.20 | - | 2.63 | |

* **`tau_baking` is the only clear win:** +5 tasks (childsnack p15/p25, pathways p18/p24,
  rovers p12), no loss, conflicts halved. Where it helps, it packs more of the plan into
  one step; the formula per step stays the same size or grows (grid: variables x1.8).
  * rovers: every task at horizon 1, Σ 157 -> 23, time 0.27x;
  * childsnack: the only domain where the formula per step shrinks (0.77x), time 0.64x;
  * grid p01: 76 s -> 34 s;
  * pathways: same horizon, but p18 and p24 newly solved (both near the limit, 238-295 s);
  * pegsol: removes the tau label, otherwise no effect.
* **`label_closure`** shortens a few horizons (driverlog 10 -> 8, woodworking 5 -> 4) but
  loses driverlog p03 and childsnack p24.
* **Re-shrinking after `tau_baking`** barely shortens the horizon further and loses grid
  p01, airport p03 and pipesworld p07/p21. The last three have no tau labels, so the
  second `-shr` alone causes it.
* **Label reduction does not cost parallelism here.** Without it the horizon is slightly
  *longer* (607 -> 615; weak bisimulation finds fewer equivalent labels to shrink with),
  the formula per step is 6.4x larger, and 68 tasks are lost (nomystery p01: 12,852
  labels instead of 143). Running it needed a fix: `transform_merge_and_shrink` crashed
  on a null `label_reduction` (`8defe2e31`).

### 5.4 Other encodings, on the 89 tasks the transformations change

*Superseded by §5.7 (all 303 tasks, both label orders for `full_transitions_sat`).*

On the other 214 tasks every configuration gets the identical task. Encoders:
`label_sat(encoding=CHAINS_PARALLEL, ...rcpol)` (`chains_slf____rcpol`) and
`full_transitions_sat(use_self_loop_optimisation=true, use_labels_in_effects_constraints=false)`
(`fulltransitions_slf_transeff`), both with `sat(one_by_one)`. Time is relative to `-shr`
with the same encoder.

| configuration | `bdd_sat` solved | PAR-2 | horizon | time | `label_sat` solved | PAR-2 | horizon | time | `full_transitions_sat` solved | PAR-2 | horizon | time |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `-shr` | 86 | 37.8 | 320 | 1.00 | 75 | 104.2 | 696 | 1.00 | **85** | 34.7 | 441 | 1.00 |
| + `tau_baking` | **89** | **19.0** | 193 | 0.63 | **78** | **84.0** | 413 | **0.54** | **85** | **34.5** | 277 | **0.72** |
| + `label_closure` | 85 | 42.3 | 308 | 1.00 | 75 | 104.6 | 689 | 1.02 | 84 | 40.0 | 430 | 1.03 |
| + both | 88 | 22.8 | **184** | **0.61** | **78** | 84.2 | **411** | 0.55 | 84 | 39.8 | **268** | 0.73 |

* `tau_baking` shortens the horizon by 37-41 % for every encoder; `label_sat` gains most
  in time (0.54x). The two re-shrink configurations, run on 67 of the 89 tasks (deadline),
  add nothing for any encoder.
* The table compares the encoders under different label orders; §5.5 does it under equal
  ones.

### 5.5 Label order versus encoding (`bdd_sat` vs `full_transitions_sat`)

`full_transitions_sat` had its label order hard-coded to the label ids, and the order was
only half implemented: the step semantics is the position of a label in
`relevantLabels[ts]`, but `labelOrder` was used in two places with mismatched keys, and plan
extraction ran a step's labels by id. Fixed in `5d15ee4a9`. The encoder now has a
`label_order` option, every factor sorts its labels by the global order, and extraction
follows it. With the default `label_order_linear()` the formula is bit-identical (6 tasks,
same clauses and conflicts).

On the same 89 tasks (85 finished in all four configurations; Σ horizon over the tasks all
four solve):

| | `-shr` solved | PAR-2 | Σ horizon | + `tau_baking` solved | PAR-2 | Σ horizon |
|---|---:|---:|---:|---:|---:|---:|
| `bdd_sat`, goal-chains v2 | **82** | 36.7 | **272** | **85** | **17.1** | **160** |
| `bdd_sat`, linear | 81 | 38.7 | 382 | 82 | 34.9 | 241 |
| `full_transitions_sat`, linear | 81 | **34.7** | 382 | 81 | 34.4 | 241 |
| `full_transitions_sat`, goal-chains v2 | 80 | 42.6 | **272** | 81 | 35.4 | **160** |

* **The horizon depends only on the order, not on the encoding.** Under the same order both
  encoders reach exactly the same Σ horizon in all four comparisons: they express the same
  step semantics.
* Under the label-id order they are level (81 / 81).
* goal-chains v2 pays off only for `bdd_sat`. `full_transitions_sat` gets the shorter
  horizon too, but becomes slower (PAR-2 34.7 -> 42.6).
* The advantage of `bdd_sat` over the transition encoding is the combination of the order
  with `tau_baking` (85 solved, PAR-2 17.1, against 81-82 and about 35 for the others).

### 5.6 Merging local factors into their guard (`merge_local_guard`)

A factor i is *locally driven* if every label that moves it moves no other factor. Its
*guards* are the factors in which those labels have preconditions. With a single guard g,
the labels of i move only the product g x i and self-loop everywhere else, i.e. they become
tau labels of the product, which `tau_baking` removes. `merge_local_guard(max_states,
max_dependents)` is an M&S merge strategy doing exactly these merges in the main loop of
`-shr` (`run_main_loop=true`). It stops when no pair fits into `max_states`.
`max_dependents` restricts it to guards on which at most that many local factors depend.
Label reduction in the main loop is switched off (`before_shrinking=false`; label reduction
on the atomic FTS only, newly allowed): in the loop it merged labels and cost parallelism
(openstacks horizon 1 -> 3) and was too slow on data-network.

**Dump analysis** (`experiments/fts_dumps/guard_merge.py`, after `-shr`): 70 tasks have
local factors with a single guard (data-network 1,023 factors, rovers 241, nomystery 110,
pathways 82, tidybot 8, openstacks 7). In all of them except nomystery every guard has one
dependent and the product has at most 12 states. In nomystery many packages hang on the
same truck, so the products of a guard and all its dependents reach 10^27 states.

**303 tasks** (`bdd_sat`, same setup as §5.3, limit 1,000 states, all 606 plans valid).
Merges happen in exactly the 70 tasks the dump analysis predicts; on the other 233 the
task is the one of `-shr` + `tau_baking`. Σ horizon and time over the 288 tasks all four
solve, time relative to `-shr`:

| configuration | solved | PAR-2 | Σ horizon | time |
|---|---:|---:|---:|---:|
| `-shr` | 292 | 53.7 | 777 | 1.00 |
| `-shr` + `tau_baking` | 297 | 45.2 | 638 | 0.88 |
| + merging, unrestricted | 295 | 47.6 | **629** | 0.87 |
| + merging, `max_dependents=1` | **299** | **41.9** | 634 | **0.86** |

On the 70 tasks where merging happens (time relative to `-shr` + `tau_baking`, 62 solved by
all):

| domain | tasks | `-shr` + tau: solved / PAR-2 | + merging, `max_dependents=1`: solved / PAR-2 | time |
|---|---:|---|---|---:|
| data-network | 8 | 7 / 157.0 | **8 / 105.2** | 0.88 |
| tidybot | 2 | 2 / 154.3 | 2 / **124.9** | 0.81 |
| pathways | 26 | 26 / 36.9 | 26 / 36.6 | 0.92 |
| rovers | 24 | 24 / 5.2 | 24 / 4.9 | 0.93 |
| openstacks | 2 | 2 / 0.0 | 2 / 0.0 | - |
| nomystery | 8 | 8 / 138.7 | 8 / 140.1 | 1.03 |
| all 70 | 70 | 69 / 53.7 | **70 / 46.9** | **0.92** |

* `max_dependents=1` loses no task, gains data-network p09 (childsnack p26, also gained,
  is noise: no merge happens there), and is 8 % faster where it merges. In nomystery it
  merges nothing, as intended.
* Unrestricted merging loses nomystery p04/p06/p07/p09: the stacked products of a truck and
  its packages make every step's formula larger and kissat stalls at horizon 12.
* The first runs of this experiment used a buggy strategy: the per-label list of moved
  factors was capped at two, so a label moving three or more factors was missing from the
  later factors' lists and they looked local. In satellite that produced merges the dump
  analysis does not predict, and with 89,164 atomic labels (every merge step copies the
  FTS for plan reconstruction) p07/p08 ran out of memory. Fixed in `eb715adac`. Merging as
  a separate `transform_merge_and_shrink` step after the finished `-shr` gives identical
  results.

**All combinations, 303 tasks** (same setup; merging = `merge_local_guard(max_states=1000,
max_dependents=1)`; Σ horizon, conflicts and time over the 287 tasks all eight solve,
relative to `-shr`; +/- against `-shr`):

| configuration | solved | PAR-2 | Σ horizon | conflicts | time | +/- |
|---|---:|---:|---:|---:|---:|---:|
| `-shr` | 292 | 53.7 | 788 | 1.00 | 1.00 | |
| + `tau_baking` | 297 | 45.2 | 673 | 0.61 | 0.92 | +5 / -0 |
| + `label_closure` | 290 | 56.3 | 782 | 0.98 | 1.01 | +0 / -2 |
| + `tau_baking` + `label_closure` | 296 | 46.3 | 670 | 0.60 | 0.92 | +5 / -1 |
| merging | 291 | 54.5 | 783 | 0.97 | 0.96 | +1 / -2 |
| merging + `tau_baking` | **299** | **41.9** | 669 | 0.60 | 0.90 | +7 / -0 |
| merging + `label_closure` | 290 | 55.7 | 777 | 0.95 | 0.96 | +1 / -3 |
| merging + `tau_baking` + `label_closure` | 297 | 43.6 | **666** | **0.59** | **0.89** | +7 / -2 |

* **Merging pays off only together with `tau_baking`.** Alone it is level with `-shr`
  (291 vs 292): the labels of a merged local factor are tau labels of the product, but
  only `tau_baking` removes them. Without it, merging loses the tasks `tau_baking` gains
  (childsnack p15/p25, pathways p18/p24, rovers p11/p23, data-network p09).
* **`label_closure` adds nothing on top.** With merging and `tau_baking` it shortens the
  horizon a little more (669 -> 666) but loses driverlog p03 and nomystery p07.
* Best configuration: `-shr` + merging (`max_dependents=1`) + `tau_baking`.

### 5.7 All encoders and transformations, 303 tasks (2026-10-19)

Same 303 tasks and setup as §5.3 (300 s / 2500 MB, 4 jobs, all plans valid; data-network
and floortile checked separately). `full_transitions_sat` with its default label-id order
(linear) and with goal-chains v2, the order `bdd_sat` uses. `label_sat` has no label order:
its CHAINS_PARALLEL semantics is forall-step (chains built in both directions,
`compute_guarded_forall_chains`), so the order only affects the clause construction.
Merging = `merge_local_guard(max_states=1000, max_dependents=1)`.

| configuration | `bdd_sat` v2 solved | PAR-2 | `full_transitions_sat` v2 solved | PAR-2 | `full_transitions_sat` linear solved | PAR-2 | `label_sat` solved | PAR-2 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `-shr` | 292 | 53.7 | 292 | 45.8 | **259** | 108.1 | 218 | 189.0 |
| + `tau_baking` | 297 | 45.2 | **294** | 42.1 | **259** | 107.8 | **221** | **182.3** |
| + `label_closure` | 290 | 56.3 | 291 | 47.8 | 258 | 109.8 | 217 | 190.6 |
| + both | 296 | 46.3 | 293 | 43.7 | 258 | 109.3 | 220 | 184.1 |
| merging + `tau_baking` | **299** | **41.9** | **294** | **41.5** | **259** | **106.7** | 220 | 184.1 |
| merging + `tau_baking` + `label_closure` | 297 | 43.6 | 293 | 43.0 | 258 | 108.2 | 220 | 184.2 |

Σ horizon over the 206 tasks all four solve in every configuration (rows in table order),
and total time relative to each encoder's own `-shr` (geometric mean, tasks both solve,
one of the two >= 1 s):

| encoder | Σ horizon | time |
|---|---|---|
| `bdd_sat` v2 | 574 / 459 / 567 / 456 / 449 / 446 | 1.00 / 0.89 / 1.01 / 0.88 / 0.86 / 0.85 |
| `full_transitions_sat` v2 | 574 / 459 / 567 / 456 / 449 / 446 | 1.00 / 0.94 / 1.02 / 0.94 / 0.93 / 0.92 |
| `full_transitions_sat` linear | 989 / 841 / 983 / 839 / 823 / 821 | 1.00 / 0.91 / 1.01 / 0.92 / 0.89 / 0.90 |
| `label_sat` | 2226 / 1940 / 2215 / 1937 / 1885 / 1882 | 1.00 / 0.87 / 1.01 / 0.88 / 0.88 / 0.89 |

* **The label order, not the encoding, made the difference.** Under the same order
  `full_transitions_sat` solves as many tasks as `bdd_sat` without transformation (292 /
  292) with a better PAR-2 (45.8 vs 53.7), and both reach exactly the same horizon in every
  configuration: they express the same step semantics. With its default label-id order
  `full_transitions_sat` solves 33 fewer.
* `bdd_sat` profits more from the transformations: +7 tasks and PAR-2 53.7 -> 41.9 with
  merging + `tau_baking`, against +2 and 45.8 -> 41.5 for `full_transitions_sat` v2. In
  the best configuration the two are level on PAR-2, `bdd_sat` solves 5 more.
* The encoders fail on different tasks (best configuration): only `bdd_sat` solves
  nomystery p04/p05/p06/p07/p09 and pathways p09/p14/p18; only `full_transitions_sat` v2
  solves blocksworld p02 and childsnack p16/p17/p27 (§ on childsnack p16: same horizon 1,
  but `bdd_sat` needs 100-850x the conflicts).
* `tau_baking` shortens the horizon of every encoder by 13-20 % and the time by 6-13 %;
  `label_closure` never helps.

Raw data: `/home/gregor/data/claude-jobs/abf93c5f/trans/` (`camp_all.csv`, `nolr_all.csv`,
`enc_{label,trans}_*.csv`, `lin.csv`, `tv2_*.csv`, `g303_all.csv`, `g303b_all.csv`, `enc303_{label,trans,tv2}_all.csv`, `local_*.csv`,
`closure_*.csv`, `guard_shr.csv`); runner `scripts/trans_test.py`, report
`scripts/trans_report.py`.

## 6. Open issues

* No variant improves **coverage** beyond noise. The gains are in conflicts and time on
  the tasks that are solved anyway.
* **Formula size versus propagation strength.** Domain consistency (path encoding)
  works, but its size is paid in every SAT call.
* P3 over skipping edges is unproven; it is empirically sound.
* Why combined BDDs need more conflicts than split ones is unknown.
* Label-implication extraction can be expensive, and caps are needed.
* **Eliminating redundant clauses.** kissat may remove state clauses during elimination
  and still pays for them; clause shape (short vs long) matters.
* **h2 changes the transformed task** (§4.7). Mutexes from h2 come with a less
  compressed FTS on some domains.
* Timed-out SAT calls report no conflict counts.
* M&S plan reconstruction copies the whole FTS at every merge step, with label tables sized
  by the atomic label count (satellite p07: 89,164). Merging in the main loop is only
  affordable because `merge_local_guard` merges few factors.

## 7. Open ideas (parked unless noted)

* **h2 only as a mutex source** (parked 2026-10-16): run the transform and search on
  the unpreprocessed task and import h2's mutexes, mapped back to the original facts by
  name. This would keep the mutex gain without nomystery's larger FTS. Mainly a
  coverage fix.
* Combinations not tried yet: implications + state clauses without h2;
  `AND_LIMITED` 30 s on top of the best add-ons.
* A smaller domain-consistent encoding: edge variables only on skipping edges, or
  quasi-reduction compared against P3 over skips.
* Restrict / Constrain the factor BDDs by the implication or mutex care set before
  Tseitin, which shrinks the CNF instead of adding clauses.
* A native BDD propagator (IPASIR-UP in CaDiCaL) instead of a CNF encoding: domain
  consistency without the formula size. kissat has no such interface.
* Length strategies that do not probe every horizon (`by_iteration`, a tuned rintanen),
  which would help the large encodings most.
* Per-instance or per-factor encoding selection: deliberately **not** pursued.
* **Transformations** (§5): eliminating locally driven factors by merging them with the
  single factor their labels depend on, so their labels become tau and can be baked
  (judged risky, analysis only so far); splitting two single-factor labels with different
  preconditions into a label for the intersection, which label reduction can then merge;
  label reduction restricted to merges whose result moves one factor, followed by closure.
