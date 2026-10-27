# Transformations: state and open work (2026-10-27)

Context: SAT planning on transformed FTSs (`--transform`), mainly merging + tau_baking (+ label
union) with h2 preprocessing, encoders bdd_sat / hybrid / full_transitions_sat. Local evaluation:
395 Autoscale tasks, 300 s / 2500 MB, 4 jobs (`/home/gregor/data/claude-jobs/abf93c5f/unify/`).

## Done

* **Exact label reduction speedup** (branch `transformation-speedup`, commit `53dfcf9ca`, not yet
  merged into `fts-transformations`). `exact(skip_unsuccessful=true)` (default) skips the
  label-reduction iterations a hash test proves to reduce nothing; the iterations that reduce
  run the original code, so the label mapping and the new labels' order are unchanged.
  `EquivalenceRelation` keeps element positions in a vector instead of an `unordered_map`.
  * Identity: the `-shr` FTS (`dump_fts`) is byte-identical to the previous build on all 372 of
    395 tasks both builds finish; woodworking p06 differs because the old build hit
    `max_time=300` (2316 vs 2260 labels).
  * Label reduction time on the 87 tasks with >= 5 s: 5999 s -> 952 s (elevators x15-17,
    data-network x13, childsnack x5-10, airport x4-6; tetris p06 803 -> 559 s).
  * Local rerun of the headline configurations (134 affected tasks, every plan valid):

    | configuration | solved | NormCov | PAR-2 |
    |---|---:|---:|---:|
    | hybrid, h2 pre, merging+tau + union | 330 -> **335** | 29.14 -> **29.56** | 117.2 -> **100.7** |
    | hybrid, h2 pre, merging+tau | 325 -> 332 | 28.85 -> 29.34 | 124.4 -> 105.7 |
    | full_trans, h2 pre, merging+tau + union | 327 -> 332 | 29.04 -> 29.46 | 121.8 -> 105.4 |
    | full_trans, h2 pre, merging+tau | 323 -> 330 | 28.78 -> 29.28 | 128.4 -> 109.5 |
    | bdd_sat, -shr | 301 -> 306 | 22.83 -> 23.36 | 167.7 -> 152.6 |

* **Label union** (`label_union()`, f90f966c4): +1 to +5 tasks in every encoder / h2 setting;
  best local configuration is hybrid + h2 pre + merging+tau + union. Closure case 3
  (`label_closure(multi_factor=true)`) adds nothing.

## Open

1. **Merge `transformation-speedup`** once the confirmation runs against the January results are
   done; then rerun the IPC headline configurations on the cluster (the IPC suite surely has
   the same slow-label-reduction tasks).

2. **Tasks that never get past preprocessing** (9 local tasks in the best configuration). Two
   separate causes:
   * **h2 is now the larger preprocessing cost** (best configuration, 395 tasks): median 0.2 s,
     p90 12 s, max 285 s, 29 tasks >= 30 s; versus the transformation with the new label
     reduction: median 0.4 s, p90 8 s, max 97 s, 13 tasks >= 30 s. Unsolved with h2 >= 30 s:
     airport p15 (285 s), transport p16/p17 (166 / 251 s), openstacks p13 (217 s), satellite
     p09/p10, tetris p06. Options: a shorter h2 time limit; `--h2-information` (h2 on a copy,
     used only if it finishes) degrades gracefully but does not prune operators.
   * **Atomic FTS construction**: the same tasks plus organic-synthesis-split also run out of
     memory or time building the atomic FTS without h2. Suspicion (unmeasured): every
     irrelevant label's self-loops are stored explicitly in every factor. Measure first.

3. **Rest of label reduction**: the iterations that do reduce still use the list-based
   `EquivalenceRelation` (to keep the label numbering). Remaining cost: tetris p06 559 s;
   airport, transport, childsnack, pipesworld p21 40-90 s. Faster would mean reproducing the
   exact block order of the old refinement, or accepting relabelled output (which shifts label
   orders and therefore results).

4. **Tasks the SAT solver finds hard** (not started). Observations:
   * blocks p01: h2 information 1.1 s vs h2 preprocessing 158 s; ged p02: 5 s vs 108 s.
   * nomystery p08, p09, p11 are solved only without merging (bdd_sat -shr + union).
   * Oracle over all 18 local configurations: 339 of 395 vs 330 for the best single one, i.e. per-
     task selection of the transformation has at most 9 tasks of headroom locally.
   Plan: rerun these ~20 tasks under the relevant configurations with conflict statistics
   (the local CSVs have conflicts = 0) and separate fewer steps from easier formulas.

5. **Measured, not built**:
   * label union of labels that each move several factors, as long as the moved factor sets
     are disjoint (counts measured, `experiments/fts_dumps/union_multi.py`);
   * cutting with state information (cross-factor state mutexes; see CLAUDE.md "Open ideas");
   * restricting the BDDs by the label-implication care set before the Tseitin translation.
   Union inflates the BDDs for bdd_sat; it pays off for the hybrid and full_transitions.

6. **Documentation**: CLAUDE.md / BDD.md do not yet describe the label-reduction speedup, the
   two label_sat fixes (f73c4c4f2 chain numbering, c8f7f7b5b per-step label group table) or the
   local rerun.
