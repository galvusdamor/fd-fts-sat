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

## Options under consideration (not implemented)

1. **Drop the mover variables.** The semantics only needs the switch position per factor. Keep
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
