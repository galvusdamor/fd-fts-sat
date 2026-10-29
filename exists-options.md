# exists_sat: encoding options and what we know about them (2026-10-28)

Branch `exists-speedup` (= `fts-transformations` + the exact-label-reduction speedup).
Code: `src/search/sat/exists_encoding.{h,cc}`. Plugin: `exists_sat(...)`.

## Semantics

Per factor and step at most one real move s -> s'. The firing labels of a step run in the
fixed label order; every non-mover runs as a self-loop.

| `semantics=` | non-mover before the move | non-mover after the move | extra |
|---|---|---|---|
| `STRICT` | self-loop at s | self-loop at s' | every firing label applicable at s (Rintanen's exists-step) |
| `LAX` | self-loop at s | self-loop at s' | none (a later label's precondition may be produced in the step) |
| `FORALL` | self-loop at s **and** s' | same | none; comparison variant (forall-step on the same machinery) |

Hierarchy: forall <= STRICT <= LAX <= R2E/TAV (several moves per factor per step).
LAX has exactly the shortest horizons of `bdd_sat(one_step_only=true)` with the same order
(checked on 30 IPC first tasks).

## Options (current implementation)

| option | default | meaning |
|---|---|---|
| `label_order` | `label_order_tsort()` | base label order (tie-break for `conflict_order`) |
| `conflict_order` | `true` | order labels by the SCCs of the kind-1/kind-2 conflict graph (generalised Rintanen disabling graph; kind 1: label with self-loop at s but not s' must precede movers s -> s'; kind 2: label with self-loop at s' but not s must follow them). Measured: `experiments/fts_dumps/exists_order.py` |
| `exploit_order` | `true` | no before/after guard for labels whose conflicts the order resolves (single clause "self-loop at s or at s'") |
| `implied_clauses` | `false` | add `label & start=s -> end in R(l,s)` (implied; propagates the end state from the label variable as label_sat does) |
| `break_mover_symmetry` | `false` | among labels with identical transitions in a factor, only the first firing one may be the mover. Exact only for `FORALL`; rejected otherwise |
| `force_at_least_one_action` | `false` | |

Variables per step and factor: a mover variable `mv` per (factor, label with a real move) and a
prefix variable `M_j` per mover ("one of the first j movers fired").

## Findings so far

* 395 Autoscale tasks, `-shr`, no h2, algorithm A: exists needs fewer steps (lax about 16% fewer
  than forall) but no more tasks and 1.1-1.4x the time of `label_sat` forall.
* childsnack (p17, p24-p27): same horizon (3) and formula size for all variants, but the
  satisfiable call takes 9-263 s for exists vs 11-45 s for `label_sat`.
* Ablation `semantics=FORALL`: as slow as exists. So the cost comes from the encoding machinery,
  not from the exists semantics.
* childsnack p17: 72,441 mover variables per step fall into only 162 groups of labels with
  identical transitions per factor (~450 interchangeable movers per group). Suspected causes:
  (1) symmetric mover choice, (2) a firing label alone does not constrain the end state.
  `break_mover_symmetry` and `implied_clauses` measure (1) and (2) (runs in
  `/home/gregor/data/claude-jobs/abf93c5f/unify/runs_abl2`).

## Ablation on childsnack (2026-10-28)

Satisfiable call at length 3 (time / conflicts; "–" = not finished within 300 s), build
`release64-exists-abl`, two parallel jobs (single times noisy):

| task | forall `label_sat` | FORALL | + sym | + impl | + both | STRICT conf | + impl | LAX tsort | + impl |
|---|---|---|---|---|---|---|---|---|---|
| p17 | 14 s / 157k | 15 s / 38k | – | 51 s / 325k | 10 s / 10k | 9 s / 11k | 6 s / 10k | 107 s / 734k | 51 s / 234k |
| p24 | 11 s / 80k | 58 s / 481k | 68 s / 210k | 103 s / 594k | – | 222 s / 1634k | 54 s / 250k | 150 s / 863k | 166 s / 903k |
| p25 | 14 s / 78k | 250 s / 1823k | 243 s / 858k | – | – | 263 s / 1745k | – | 142 s / 753k | – |
| p26 | 45 s / 427k | 155 s / 720k | – | 111 s / 538k | – | 226 s / 1270k | – | – | – |
| p27 | 17 s / 87k | 220 s / 1160k | – | – | – | – | – | 116 s / 549k | – |

* `break_mover_symmetry` does not help: the interchangeable movers are not the main cause.
* `implied_clauses` helps in some cases (STRICT p24 222 -> 54 s, LAX p17 107 -> 51 s) but not
  consistently, and makes FORALL p17 slower.
* Neither closes the gap to `label_sat`. Next candidate: option 1 below (no mover variables).

## Without mover variables (`mover_variables=false`, b232d5665)

Same horizons as with mover variables on the 30 IPC first tasks. childsnack, satisfiable call at
length 3 (time / conflicts):

| task | forall `label_sat` | FORALL mv | FORALL no mv | STRICT conf mv | STRICT conf no mv | LAX tsort mv | LAX tsort no mv |
|---|---|---|---|---|---|---|---|
| p17 | 14 s / 157k | 15 s / 38k | 17 s / 50k | 9 s / 11k | 5 s / 8k | 107 s / 734k | 93 s / 532k |
| p24 | 11 s / 80k | 58 s / 481k | 204 s / 1098k | 222 s / 1634k | 80 s / 387k | 150 s / 863k | 152 s / 820k |
| p25 | 14 s / 78k | 250 s / 1823k | 190 s / 848k | 263 s / 1745k | 243 s / 1486k | 142 s / 753k | – |
| p26 | 45 s / 427k | 155 s / 720k | 6 s / 11k | 226 s / 1270k | – | – | 237 s / 926k |
| p27 | 17 s / 87k | 220 s / 1160k | 7 s / 16k | – | – | 116 s / 549k | 228 s / 773k |

Mixed: large gains on some tasks, losses on others, still behind `label_sat` overall. The mover
variables are not the main cause either. Remaining structural difference to `label_sat`: its
matrix clauses over label groups (label -> source/target sets, source/target pillars).

## exists-step on the forall machinery: `label_sat(..., exists_step=true)` (2026-10-28)

Commits 3a5f6ce85, d150957df. Options of `label_sat` (CHAINS_PARALLEL, `use_label_group=false`):
`exists_step` (strict), `exists_lax`, `label_order` (default `label_order_linear()`).

Why the forall encoding cannot just lose clauses as in FDR: Rintanen's per-fact chains rely on a
firing action that deletes a fact having certainly moved. In the FTS a label can self-loop at a
subset of states, so whether it runs before or after the mover depends on where the factor goes,
not only where it comes from. What makes a per-state version work anyway: a label almost never has
a self-loop at the *source* of one of its own moves (Autoscale dumps, one task per domain: 0 on
-ntr, 0-25% of the moves on -shr, e.g. childsnack 16%, gripper 25%); self-loops at the *target*
are common (up to 50%) but harmless.

Encoding, per step and factor:
* one prefix chain per target state t over the labels with a real transition into t ("adders" of
  t), in label order: I_t[k] = "the factor moved into t by adder k or earlier";
  - event: l & prev_u & next_t -> I_t[k(l)] for every real u -> t of l without a self-loop at u
    (a label with a self-loop at u may run before the mover and does not force the chain);
  - upper bound: I_t[k] -> I_t[k-1] | l_k, and I_t[k] -> I_t[k-1] | prev in sources of l_k into t;
  - I_t[last] -> next_t & -prev_t; next_t & -prev_t -> I_t[last];
  - l -> -I_t[before l] for every t where l lacks a self-loop and moves into t (or l only
    self-loops): no such label after a move into t (binary clauses);
* the forall chains are dropped;
* the forall clauses "l & prev_s -> -next_t" ((s, t) not a transition of l) are weakened for
  labels with self-loops: self-loop at s only -> "-I_t[before l]" (l before the mover), at t only
  -> "I_t[before l]" (after), at both -> no clause; the label -> target clauses (empty cols, the
  self-loop optimisation's effect clause) are dropped for labels with self-loops;
* LAX: "l -> prev in sources(l)" (rows) becomes "l -> prev in sources(l) | I_t[before l] for a
  self-loop state t of l" (after a move into a state where l self-loops).
Plans are extracted in label order with the mover read from I_next.

It is "forall minus clauses plus one chain per state": on childsnack p17 at the satisfying length
1.64M clauses / 281k variables against forall's 1.96M / 569k (exists_sat: similar to forall).

First results (algorithm A, -shr, 300 s, two of four cores, the other two running the queue):
childsnack, total time (forall / old exists_sat strict full / new strict, tsort):

| task | forall | exists_sat strict (full / conflict order) | new strict |
|---|---:|---:|---:|
| p17 | 35 s | 77 / 31 s | 44 s |
| p24 | 35 s | 116 / 240 s | **28 s** |
| p25 | 39 s | 194 / 276 s | 55 s |
| p26 | 81 s | 219 / 251 s | **60 s** |
| p27 | 61 s | – / – | 79 s |

83 Autoscale tasks (the first non-trivial ones of the exists_fast run: airport, childsnack,
data-network, elevators, gripper, mprime, pathways, pipesworld-notankage, rovers, satellite,
storage, tetris, tidybot, tpp, transport, woodworking), algorithm A, -shr, 300 s / 2500 MB, label
order tsort for all exists variants. The new runs used two cores while the queue used the other
two (4 jobs, as the reference runs). Every plan valid; **every horizon equal** to exists_sat with
the same semantics and order (strict vs exS_full, lax vs exL_full), which checks the semantics.

| config | solved | Σ horizon (48 solved by all) | Σ time (same 48) | geomean time / forall |
|---|---:|---:|---:|---:|
| forall `label_sat` | 52 | 361 | 892 s | 1.00 |
| exists_sat strict | 50 | 357 | 1633 s | 1.36 |
| **new strict** | **55** | 357 | 921 s | 0.97 |
| exists_sat lax | 49 | **323** | 1784 s | 1.53 |
| **new lax** | 54 | **323** | **862 s** | **0.94** |

The new encodings solve airport p02, p03, p16 that forall does not (p01: 76 s vs 255 s at the same
horizon 53); lax loses pathways p17. Elsewhere coverage is identical. So the exists semantics now
costs nothing over forall and lax still saves 11% of the steps.

Raw data: `/home/gregor/data/claude-jobs/abf93c5f/unify/runs_es80`, `runs_esl80` (build
`release64-exists-step2` = d150957df), comparison script `unify/cmp_es.py`. Algorithm C for both
(tuned: start 1 x1.6, 4 calls, gamma 0.5) on the same 83 tasks, and algorithm A on the remaining 73
tasks of the exists_fast set, are queued in the local `algc80` batch.

## Options under consideration (not implemented)

1. **Drop the mover variables.** (implemented: `mover_variables=false`) The semantics only needs the switch position per factor. Keep
   only the prefix chain; "label i is the mover" is `M_i & -M_{i-1}`, which then carries the
   transition constraint of label i.
2. **Compress the chain.** Consecutive labels in the order with identical transitions in the
   factor behave identically: one chain position for the run. Removes most of the symmetric
   choices. Gain depends on how often identical labels are adjacent in the order (measure for
   tsort and the conflict order first).
3. **Implied clauses by default** if they pay off (`implied_clauses`).
4. Group-level movers that stay exact for STRICT/LAX (the swap argument that makes
   `break_mover_symmetry` exact for FORALL fails there: moving the mover changes which labels
   run before and after it).
