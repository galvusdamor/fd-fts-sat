/*
  Segments: per factor, a sequence of segments (a single label or a block of labels) between
  layers of state variables (see step_encoding.h). The code is sigma_encoding.cc's (sigma_sat) and,
  for the ROWS style, micro_steps_encoding.cc's (micro_steps_sat).
*/
#include "step_encoding.h"

#include "ipasir.h"
#include "sat_encoder.h"

#include "../task_representation/fts_task.h"
#include "../task_representation/transition_system.h"
#include "../utils/system.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <functional>
#include <set>

using namespace std;
using namespace task_representation;

namespace sat_search {

/*
  The states that get a new variable in a single-label segment's end layer: those the label moves,
  or every state with full_layers (micro_steps_sat without variable sharing). SUPPORT and
  PROJECTION create no layer for a label that moves nothing (it only gets its precondition).
*/
vector<int> StepEncoding::segment_new_states(const SigmaLabelData &ld, size_t numStates) const {
    if (sigma.fullLayers && sigma.clauses.style == 2) {
        vector<int> all(numStates);
        for (size_t s = 0; s < numStates; s++) all[s] = s;
        return all;
    }
    return ld.changed;
}

void StepEncoding::segments_encode_transition(const vector<vector<int>> &previousStateVars,
                                              const vector<vector<int>> &nextStateVars, int fromTime) {
    auto &updates = allTimesUpdates[fromTime];
    updates.assign(fts->get_size(), {});
    for (int ts = 0; ts < fts->get_size(); ts++)
        encode_factor_segments(ts, previousStateVars[ts], &nextStateVars, fromTime);
}

/*
  The layers of factor ts in the step from fromTime: the factor's labels form segments (a single
  label, or a block of labels with the same block / group id), each a micro-step from the current
  layer to a new one in which only the states the segment's labels change get new variables.
  With nextStateVars (the t+1 state variables, generated up front) a state's last change in the
  step is the t+1 variable itself (share_last_layer) and the other states are made equivalent to
  theirs; without, the last layer is returned and becomes the t+1 state variables.
*/
vector<int> StepEncoding::encode_factor_segments(int ts, vector<int> cur,
                                                 const vector<vector<int>> *nextStateVars, int fromTime) {
    const vector<int> &labelVars = allTimesLabelVars[fromTime];
    auto &updates = allTimesUpdates[fromTime];
    const vector<int> start = cur;                 // the state variables of time t
    int moved = 0;                                 // one_move: M of the current position (0 = false)
    const vector<SigmaLabelData> &labels = sigma.factors[ts];
    // one_move: entering a state sets M; after a move nothing can be entered
    auto oneMoveClauses = [&](const vector<int> &next, const vector<int> &changed, int label) {
        (void) label;   // debug variable names only
        const int m = sat->new_variable();
        DEBUG(sat->registerVariable(m, "moved_" + to_string(ts) + "_" + to_string(label) +
                                    "@" + to_string(fromTime)));
        if (moved) sat->implies(moved, m);
        for (int s : changed) {
            sat->andImpliesOr(vector<int>{-cur[s], next[s]}, vector<int>{m});
            if (moved)
                sat->andImpliesOr(vector<int>{moved, -cur[s], next[s]}, vector<int>{});
        }
        moved = m;
    };
    // segments: a single label or a group of this factor's labels forming one micro-step
    vector<pair<size_t, size_t>> segments;
    for (size_t i = 0; i < labels.size();) {
        size_t j = i + 1;
        if (!sigma.factorBlock.empty()) {
            const vector<int> &blk = sigma.factorBlock[ts];
            while (j < labels.size() && blk[j] == blk[i])
                j++;
        } else {
            while (j < labels.size() && sigma.groupOf[labels[j].label] == sigma.groupOf[labels[i].label])
                j++;
        }
        segments.emplace_back(i, j);
        i = j;
    }
    // share_last_layer: the last segment changing a state creates no new variable for it but
    // uses the state variable of time t+1
    vector<int> lastSegment(cur.size(), -1);
    if (nextStateVars && sigma.clauses.shareLast)
        for (size_t g = 0; g < segments.size(); g++)
            for (size_t k = segments[g].first; k < segments[g].second; k++)
                for (int x : labels[k].changed)
                    lastSegment[x] = g;
    size_t segment = 0;
    auto freshStateVar = [&](int x) {
        if (lastSegment[x] == (int) segment)
            return (*nextStateVars)[ts][x];
        return sat->new_variable();
    };
    curNextStateVars = nextStateVars;
    const int style = sigma.clauses.style;
    for (segment = 0; segment < segments.size(); segment++) {
        const size_t i = segments[segment].first, j = segments[segment].second;
        // preconditions (all labels: applicable at the group's start state; the PROJECTION and
        // ROWS styles encode a single label's precondition themselves)
        for (size_t k = i; k < j; k++) {
            const SigmaLabelData &ld = labels[k];
            const int l = labelVars[ld.label];
            vector<int> pre;
            for (int x : ld.applicable)
                pre.push_back(cur[x]);
            if (!(style != 0 && j == i + 1))
                sat->impliesOr(l, pre);
            if (sigma.strict) {
                vector<int> startStates;
                for (int x : ld.applicable)
                    startStates.push_back(start[x]);
                sat->impliesOr(l, startStates);
            }
        }
        // the segment's end layer: new variables only for the states its labels change
        vector<int> newStates;
        if (j == i + 1) {
            newStates = segment_new_states(labels[i], cur.size());
        } else {
            set<int> changed;
            for (size_t k = i; k < j; k++)
                changed.insert(labels[k].changed.begin(), labels[k].changed.end());
            newStates.assign(changed.begin(), changed.end());
        }
        vector<int> layer = cur;
        for (int x : newStates) {
            layer[x] = freshStateVar(x);
            DEBUG(sat->registerVariable(layer[x], "layer_" + to_string(ts) + "_" + to_string(labels[i].label) +
                                        "_" + to_string(x) + "@" + to_string(fromTime)));
        }
        if (j == i + 1 && style == 2) {
            encode_single_rows(ts, labels[i], layer, labelVars, cur, fromTime, updates[ts]);
        } else if (j == i + 1 && style == 1) {
            encode_single_projection(ts, labels[i], layer, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
        } else if (j == i + 1) {
            encode_single(ts, labels[i], layer, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
        } else if (sigma.groupExists && sigma.groupCompact) {
            encode_group_exists_compact(ts, labels, i, j, layer, labelVars, cur, fromTime, updates[ts]);
        } else if (sigma.groupExists) {
            encode_group(ts, labels, i, j, layer, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
        } else if (sigma.groupCompact) {
            encode_group_compact(ts, labels, i, j, layer, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
        } else if (sigma.groupMatrix) {
            encode_group_matrix(ts, labels, i, j, layer, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
        } else {
            encode_group(ts, labels, i, j, layer, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
        }
    }
    if (nextStateVars) {
        // the last layer is the state at the end of the step
        const vector<int> &next = (*nextStateVars)[ts];
        for (size_t s = 0; s < cur.size(); s++) {
            if (cur[s] == next[s])
                continue;
            sat->implies(cur[s], next[s]);
            sat->implies(next[s], cur[s]);
        }
    }
    return cur;
}

/*
  One label in its own micro-step: new variables for the states it changes, not-fired copy,
  effect, at most one target per source, justification.
*/
void StepEncoding::encode_single(int ts, const SigmaLabelData &ld, const vector<int> &layer, const vector<int> &labelVars,
                                  vector<int> &cur, int fromTime, vector<SigmaUpdate> &updates,
                                  const function<void(const vector<int> &, const vector<int> &, int)> &oneMove) {
    (void) ts; (void) fromTime;   // debug variable names only
    if (ld.changed.empty())
        return;
    const int l = labelVars[ld.label];
    vector<int> next = layer;
    SigmaUpdate upd{ld.label, {}, 0, -1};
    for (int s : ld.changed) {
        upd.vars.emplace_back(s, next[s]);
        sat->andImpliesOr(vector<int>{-l, cur[s]}, vector<int>{next[s]});
        sat->andImpliesOr(vector<int>{-l, next[s]}, vector<int>{cur[s]});
    }
    for (const auto &[x, ys] : ld.targets) {
        if (ys.size() == 1 && ys[0] == x && next[x] == cur[x])
            continue;   // pure self-loop at an unchanged state
        vector<int> eff;
        for (int y : ys)
            eff.push_back(next[y]);
        sat->andImpliesOr(vector<int>{l, cur[x]}, eff);
        for (size_t a = 0; a < ys.size(); a++)
            for (size_t b = a + 1; b < ys.size(); b++)
                sat->andImpliesOr(vector<int>{l, cur[x], next[ys[a]], next[ys[b]]}, vector<int>{});
    }
    for (const auto &[s, xs] : ld.predecessors) {
        vector<int> from;
        for (int x : xs)
            from.push_back(cur[x]);
        sat->andImpliesOr(vector<int>{l, next[s]}, from);
    }
    layer_one_hot(ts, next, ld.changed);
    if (sigma.oneMove)
        oneMove(next, ld.changed, ld.label);
    updates.push_back(move(upd));
    cur = move(next);
}

bool StepEncoding::below_threshold(size_t ones, size_t numStates) const {
    if (ones <= opt.oneEncodingThreshold) return true;
    return (int) ones * 100 <= (int) numStates * opt.oneEncodingThresholdPercent;
}

/*
  layer_alo / layer_amo: exactly-one on a new layer. The unchanged states share their variables
  with the previous layer, so at-most-one only needs the pairs involving a changed state; a layer
  that consists of the t+1 state variables already has exactly-one.
*/
void StepEncoding::layer_one_hot(int ts, const vector<int> &next, const vector<int> &changed) {
    if (changed.empty()) return;
    auto isStateVar = [&](size_t s) { return curNextStateVars && next[s] == (*curNextStateVars)[ts][s]; };
    if (sigma.clauses.layerAlo) {
        bool all = true;
        for (size_t s = 0; s < next.size(); s++) all = all && isStateVar(s);
        if (!all) sat->atLeastOne(next);
    }
    if (sigma.clauses.layerAmo) {
        vector<char> isChanged(next.size(), 0);
        for (int m : changed) isChanged[m] = 1;
        for (int m : changed)
            for (size_t s = 0; s < next.size(); s++) {
                if ((int) s == m || (isChanged[s] && (int) s < m)) continue;
                if (isStateVar(m) && isStateVar(s)) continue;
                sat->andImpliesOr(vector<int>{next[m], next[s]}, vector<int>{});
            }
    }
}

/*
  PROJECTION style (the micro-step encoding of layers_in_layers.tex, with label_sat's options): the
  micro-step of label l relates layer p to layer n through the matrix T(l) of (source, target)
  pairs; a skipped label keeps the state. With S the factor's states, M the states l changes
  (new variables), n_s = p_s for s outside M:
    frame        -l & p_s -> n_s                                    s in M
    rows         primal: p_s -> -l for s not applicable;  dual: l -> OR p_app
    cols         primal: l -> -n_j for j in M not a target;  dual: l -> OR n_targets
    pillars      primal: p_i -> -n_j for j in M, j != i, (i, j) not in T(l);
                 dual: p_i -> OR n_{T(l,i) + i};  n_j -> OR p_{T^-1(l,j) + j}
    residual     primal: l & p_i -> -n_j for the remaining j;  dual: l & p_i -> OR n_{T(l,i)}
  A dual form is used if the number of ones (without the "stay" entry) is at most
  one_encoding_threshold (or the percentage) and use_positive_one / use_ones_in_last_dimension is
  set; the dual residual needs layer_amo. Without layer_alo the residual's dual form is added
  for every applicable source (it is what forces a target); without layer_amo the reverse frame
  and pairwise exclusion of nondeterministic targets are added. Pairs of unchanged states are
  covered by the previous layer's exactly-one.
*/
void StepEncoding::encode_single_projection(int ts, const SigmaLabelData &ld, const vector<int> &layer, const vector<int> &labelVars,
                                             vector<int> &cur, int fromTime, vector<SigmaUpdate> &updates,
                                             const OneMove &oneMove) {
    (void) fromTime;
    const SigmaClauses &c = sigma.clauses;
    const int N = cur.size();
    const int l = labelVars[ld.label];
    vector<char> inApp(N, 0), inM(N, 0), inTgt(N, 0);
    vector<vector<int>> T(N), pred(N);
    for (const auto &[x, ys] : ld.targets) {
        inApp[x] = 1;
        T[x] = ys;
        for (int y : ys) { inTgt[y] = 1; pred[y].push_back(x); }
    }
    for (int m : ld.changed) inM[m] = 1;
    vector<char> srcCovered(N, 0), tgtCovered(N, 0);
    // rows: which sources l can fire in (always encoded when l only self-loops here)
    if (opt.useEmptyRows || ld.changed.empty()) {
        if (opt.usePositiveOneForEmpty && below_threshold(ld.applicable.size(), N)) {
            vector<int> ones;
            for (int x : ld.applicable) ones.push_back(cur[x]);
            sat->impliesOr(l, ones);
        } else {
            for (int x = 0; x < N; x++)
                if (!inApp[x]) sat->andImpliesOr(vector<int>{cur[x], l}, vector<int>{});
        }
        for (int x = 0; x < N; x++)
            if (!inApp[x]) srcCovered[x] = 1;
    }
    if (ld.changed.empty())
        return;
    vector<int> next = layer;
    SigmaUpdate upd{ld.label, {}, 0, -1};
    for (int m : ld.changed) {
        upd.vars.emplace_back(m, next[m]);
        sat->andImpliesOr(vector<int>{-l, cur[m]}, vector<int>{next[m]});
        if (c.frameReverse || !c.layerAmo)
            sat->andImpliesOr(vector<int>{-l, next[m]}, vector<int>{cur[m]});
    }
    // cols: which states l can end in
    if (opt.useEmptyCols) {
        vector<int> tg;
        for (int j = 0; j < N; j++) if (inTgt[j]) tg.push_back(j);
        if (opt.usePositiveOneForEmpty && below_threshold(tg.size(), N)) {
            vector<int> ones;
            for (int j : tg) ones.push_back(next[j]);
            sat->impliesOr(l, ones);
            // a list of possible end states excludes the others only with at-most-one on the layer
            if (c.layerAmo)
                for (int j = 0; j < N; j++) if (!inTgt[j]) tgtCovered[j] = 1;
        } else {
            for (int j : ld.changed)
                if (!inTgt[j]) {
                    sat->andImpliesOr(vector<int>{l, next[j]}, vector<int>{});
                    tgtCovered[j] = 1;
                }
        }
    }
    // pillars: (source, target) pairs no execution of the micro-step connects
    vector<set<int>> stCovered(N);
    if (opt.useEmptyPillars) {
        if (opt.usePositiveOneForEmpty) {
            for (int i = 0; i < N; i++) {
                set<int> R(T[i].begin(), T[i].end());
                R.insert(i);
                if (!below_threshold(R.size() - 1, N)) continue;
                if (R.size() == 1 && next[i] == cur[i]) continue;   // p_i -> p_i
                vector<int> ones;
                for (int j : R) ones.push_back(next[j]);
                sat->impliesOr(cur[i], ones);
                if (c.layerAmo)
                    for (int j : ld.changed) if (!R.count(j)) stCovered[i].insert(j);
            }
            for (int j : ld.changed) {
                set<int> P(pred[j].begin(), pred[j].end());
                P.insert(j);
                if (!below_threshold(P.size() - 1, N)) continue;
                vector<int> ones;
                for (int i : P) ones.push_back(cur[i]);
                sat->impliesOr(next[j], ones);
                for (int i = 0; i < N; i++) if (!P.count(i)) stCovered[i].insert(j);
            }
        }
        for (int i = 0; i < N; i++)
            for (int j : ld.changed) {
                if (j == i || stCovered[i].count(j)) continue;
                if (binary_search(T[i].begin(), T[i].end(), j)) continue;
                sat->andImpliesOr(vector<int>{cur[i], next[j]}, vector<int>{});
                stCovered[i].insert(j);
            }
    }
    // residual: l fired in source i
    for (int i = 0; i < N; i++) {
        if (srcCovered[i]) continue;
        vector<int> forbid;
        for (int j = 0; j < N; j++) {
            if (binary_search(T[i].begin(), T[i].end(), j)) continue;
            if (!inM[j] && j != i) continue;      // two unchanged states: previous layer's exactly-one
            if (tgtCovered[j] || stCovered[i].count(j)) continue;
            forbid.push_back(j);
        }
        const bool dual = c.layerAmo && opt.useOnesInLastDimension && below_threshold(T[i].size(), N);
        if ((dual && !forbid.empty()) || (!c.layerAlo && inApp[i])) {
            vector<int> ones;
            for (int y : T[i]) ones.push_back(next[y]);
            sat->andImpliesOr(vector<int>{l, cur[i]}, ones);
        }
        if (!dual)
            for (int j : forbid)
                sat->andImpliesOr(vector<int>{l, cur[i], next[j]}, vector<int>{});
        if (!c.layerAmo)
            for (size_t a = 0; a < T[i].size(); a++)
                for (size_t b = a + 1; b < T[i].size(); b++)
                    sat->andImpliesOr(vector<int>{l, cur[i], next[T[i][a]], next[T[i][b]]}, vector<int>{});
    }
    if (c.justification)
        for (const auto &[y, xs] : ld.predecessors) {
            vector<int> from;
            for (int x : xs) from.push_back(cur[x]);
            sat->andImpliesOr(vector<int>{l, next[y]}, from);
        }
    layer_one_hot(ts, next, ld.changed);
    if (sigma.oneMove)
        oneMove(next, ld.changed, ld.label);
    updates.push_back(move(upd));
    cur = move(next);
}

/*
  Several labels in one micro-step, forall-step semantics inside: per factor at most one of them
  moves (from the group's start state p to its end state n), every other firing label self-loops
  at both p and n. mover_l ("l moves the factor in this group") is forced by a firing label that
  lacks a self-loop at p or n, at most one mover_l is true, and the move follows the mover's
  transitions; with no mover, n = p. Labels without real transitions in the factor only need
  self-loops at p and n.
*/
void StepEncoding::encode_group(int ts, const vector<SigmaLabelData> &labels, size_t begin, size_t end, const vector<int> &layer,
                                 const vector<int> &labelVars, vector<int> &cur, int fromTime,
                                 vector<SigmaUpdate> &updates,
                                 const function<void(const vector<int> &, const vector<int> &, int)> &oneMove) {
    (void) ts; (void) fromTime;   // debug variable names only
    set<int> changedSet;
    for (size_t k = begin; k < end; k++)
        changedSet.insert(labels[k].changed.begin(), labels[k].changed.end());
    vector<int> changed(changedSet.begin(), changedSet.end());
    vector<int> next = layer;
    vector<pair<int, int>> vars;
    for (int s : changed)
        vars.emplace_back(s, next[s]);
    vector<int> movers;
    for (size_t k = begin; k < end; k++) {
        const SigmaLabelData &ld = labels[k];
        const int l = labelVars[ld.label];
        set<int> selfLoops;
        for (const auto &[x, ys] : ld.targets)
            if (binary_search(ys.begin(), ys.end(), x))
                selfLoops.insert(x);
        if (ld.changed.empty()) {
            // only self-loops: also at the end state (forall; exists: it runs before the mover)
            if (!changed.empty() && !sigma.groupExists) {
                vector<int> post;
                for (int x : selfLoops)
                    post.push_back(next[x]);
                sat->impliesOr(l, post);
            }
            continue;
        }
        const int mv = sat->new_variable();
        DEBUG(sat->registerVariable(mv, "mover_" + to_string(ts) + "_" + to_string(ld.label) + "@" +
                                    to_string(fromTime)));
        movers.push_back(mv);
        sat->implies(mv, l);
        if (sigma.groupExists) {
            // exists: a firing label that self-loops neither at the start nor at the end state is
            // the mover (end states other than the changed ones only arise as "no move")
            for (int x : ld.applicable) {
                if (selfLoops.count(x)) continue;
                for (int y : changed)
                    if (!selfLoops.count(y))
                        sat->andImpliesOr(vector<int>{l, cur[x], next[y]}, vector<int>{mv});
                if (!changedSet.count(x))
                    sat->andImpliesOr(vector<int>{l, cur[x]}, vector<int>{mv});
            }
        } else {
            // a firing label without a self-loop at the start or the end state is the mover
            for (int x : ld.applicable)
                if (!selfLoops.count(x))
                    sat->andImpliesOr(vector<int>{l, cur[x]}, vector<int>{mv});
            for (int y : changed)
                if (!selfLoops.count(y))
                    sat->andImpliesOr(vector<int>{l, next[y]}, vector<int>{mv});
        }
        // the mover follows its transitions (exists: a real one -- a label that only self-loops is
        // no mover, which removes the choice of a non-moving mover)
        const bool real = sigma.groupExists;
        for (const auto &[x, ys] : ld.targets) {
            vector<int> eff;
            for (int y : ys)
                if (!real || y != x)
                    eff.push_back(next[y]);
            sat->andImpliesOr(vector<int>{mv, cur[x]}, eff);
            for (size_t a = 0; a < ys.size(); a++)
                for (size_t b = a + 1; b < ys.size(); b++)
                    sat->andImpliesOr(vector<int>{mv, cur[x], next[ys[a]], next[ys[b]]}, vector<int>{});
        }
        if (real) {
            // not applicable: no move from there
            set<int> app(ld.applicable.begin(), ld.applicable.end());
            for (int x : changed)
                if (!app.count(x))
                    sat->andImpliesOr(vector<int>{mv, cur[x]}, vector<int>{});
        }
        for (int y : changed) {
            vector<int> from;
            for (const auto &[x, ys] : ld.targets)
                if (binary_search(ys.begin(), ys.end(), y) && (!real || x != y))
                    from.push_back(cur[x]);
            sat->andImpliesOr(vector<int>{mv, next[y]}, from);
        }
        updates.push_back(SigmaUpdate{ld.label, vars, mv, (int) begin});
    }
    sat->atMostOne(movers);
    // without a mover the state does not change
    for (int s : changed) {
        sat->andImpliesOr(vector<int>{cur[s], -next[s]}, movers);
        sat->andImpliesOr(vector<int>{-cur[s], next[s]}, movers);
    }
    if (sigma.oneMove)
        oneMove(next, changed, labels[begin].label);
    cur = move(next);
}

// at most one of xs: pairwise up to 8, else a sequential ladder (Sinz) with |xs|-1 helper variables
void StepEncoding::amoLadder(const vector<int> &xs) {
    if (xs.size() <= 1) return;
    if (xs.size() <= 8) {
        for (size_t a = 0; a < xs.size(); a++)
            for (size_t b = a + 1; b < xs.size(); b++)
                sat->andImpliesOr(vector<int>{xs[a], xs[b]}, vector<int>{});
        return;
    }
    int prev = 0;
    for (size_t i = 0; i + 1 < xs.size(); i++) {
        const int r = sat->new_variable();
        sat->implies(xs[i], r);
        if (prev) {
            sat->implies(prev, r);
            sat->andImpliesOr(vector<int>{prev, xs[i]}, vector<int>{});
        }
        prev = r;
    }
    sat->andImpliesOr(vector<int>{prev, xs.back()}, vector<int>{});
}

/*
  Compact forall group: as encode_group_matrix, but without its per (label, state) justification
  clauses. The end state is one state (at most one end variable, the frame and effect clauses give
  at least one), so every firing label's effect clause forces the end state among its targets from
  the start state, i.e. (start, end) is a transition of every firing label. Frame: leaving s needs a
  firing label with a real transition out of s, entering s one with a real transition into s. Two
  firing labels with the same real transition (u, t) and one lacking a self-loop at t cannot both
  run (the second would start at t): excluded pairwise. Labels with self-loops only also self-loop
  at the end state.
*/
void StepEncoding::encode_group_compact(int ts, const vector<SigmaLabelData> &labels, size_t begin, size_t end, const vector<int> &layer,
                                         const vector<int> &labelVars, vector<int> &cur, int fromTime,
                                         vector<SigmaUpdate> &updates, const OneMove &oneMove) {
    (void) ts; (void) fromTime;   // debug variable names only
    set<int> changedSet;
    for (size_t k = begin; k < end; k++)
        changedSet.insert(labels[k].changed.begin(), labels[k].changed.end());
    vector<int> changed(changedSet.begin(), changedSet.end());
    vector<int> next = layer;
    vector<pair<int, int>> vars;
    for (int s : changed)
        vars.emplace_back(s, next[s]);
    map<int, vector<int>> leaving, entering;            // state -> label variables
    map<pair<int, int>, vector<pair<int, bool>>> moves; // (u, t) -> (label variable, self-loop at t)
    set<int> pureMovers;                                // labels without self-loops in the factor
    for (size_t k = begin; k < end; k++) {
        const SigmaLabelData &ld = labels[k];
        const int l = labelVars[ld.label];
        set<int> selfLoops;
        for (const auto &[x, ys] : ld.targets)
            if (binary_search(ys.begin(), ys.end(), x))
                selfLoops.insert(x);
        if (selfLoops.empty() && !ld.changed.empty())
            pureMovers.insert(l);
        if (ld.changed.empty()) {
            if (!changed.empty()) {
                vector<int> post;
                for (int x : selfLoops)
                    post.push_back(next[x]);
                sat->impliesOr(l, post);
            }
            continue;
        }
        for (const auto &[x, ys] : ld.targets) {
            vector<int> eff;
            for (int y : ys) {
                eff.push_back(next[y]);
                if (y != x) {
                    leaving[x].push_back(l);
                    entering[y].push_back(l);
                    moves[{x, y}].emplace_back(l, selfLoops.count(y) > 0);
                }
            }
            sat->andImpliesOr(vector<int>{l, cur[x]}, eff);
        }
        updates.push_back(SigmaUpdate{ld.label, vars, -1, (int) begin});
    }
    vector<int> ns;
    for (int s : changed) {
        ns.push_back(next[s]);
        vector<int> out = leaving[s], in = entering[s];
        sort(out.begin(), out.end()); out.erase(unique(out.begin(), out.end()), out.end());
        sort(in.begin(), in.end()); in.erase(unique(in.begin(), in.end()), in.end());
        sat->andImpliesOr(vector<int>{cur[s], -next[s]}, out);
        sat->andImpliesOr(vector<int>{-cur[s], next[s]}, in);
    }
    // at most one end state over all states (unchanged ones keep the start variable): a start state
    // outside the changed states excludes every changed end state
    amoLadder(next);
    // labels without self-loops in the factor must move the factor when they fire, and two moves
    // in one forall group are impossible: at most one of them fires (directly on the labels)
    if (sigma.groupPureAmo)
        amoLadder(vector<int>(pureMovers.begin(), pureMovers.end()));
    for (auto &[ut, ls] : moves)
        for (size_t a = 0; a < ls.size(); a++)
            for (size_t b = a + 1; b < ls.size(); b++)
                if (ls[a].first != ls[b].first && (!ls[a].second || !ls[b].second)
                    && !(sigma.groupPureAmo && pureMovers.count(ls[a].first) && pureMovers.count(ls[b].first)))
                    sat->andImpliesOr(vector<int>{ls[a].first, ls[b].first, cur[ut.first], next[ut.second]}, vector<int>{});
    if (sigma.oneMove)
        oneMove(next, changed, labels[begin].label);
    cur = move(next);
}

/*
  Several labels in one micro-step, forall-step with label_sat's conditions instead of mover
  variables: every firing label with real transitions has the group's (start, end) state pair as a
  transition (a self-loop if the factor stays), a label with self-loops only self-loops at both, and
  if two firing labels can reach the end state t, every firing label reaching t self-loops at t
  (label_sat's chains, as pairwise clauses). Then executing the group's firing labels in label order
  is valid: the first with the real transition (start, end) moves, the others self-loop.
*/
void StepEncoding::encode_group_matrix(int ts, const vector<SigmaLabelData> &labels, size_t begin, size_t end, const vector<int> &layer,
                                        const vector<int> &labelVars, vector<int> &cur, int fromTime,
                                        vector<SigmaUpdate> &updates, const OneMove &oneMove) {
    (void) ts; (void) fromTime;   // debug variable names only
    set<int> changedSet;
    for (size_t k = begin; k < end; k++)
        changedSet.insert(labels[k].changed.begin(), labels[k].changed.end());
    vector<int> changed(changedSet.begin(), changedSet.end());
    vector<int> next = layer;
    vector<pair<int, int>> vars;
    for (int s : changed)
        vars.emplace_back(s, next[s]);
    vector<int> movers;                       // label variables of labels with real transitions
    map<int, vector<pair<int, bool>>> reaching; // t -> (label variable, self-loop at t)
    for (size_t k = begin; k < end; k++) {
        const SigmaLabelData &ld = labels[k];
        const int l = labelVars[ld.label];
        set<int> selfLoops;
        for (const auto &[x, ys] : ld.targets)
            if (binary_search(ys.begin(), ys.end(), x))
                selfLoops.insert(x);
        if (ld.changed.empty()) {
            if (!changed.empty()) {
                vector<int> post;
                for (int x : selfLoops)
                    post.push_back(next[x]);
                sat->impliesOr(l, post);
            }
            continue;
        }
        movers.push_back(l);
        for (const auto &[x, ys] : ld.targets) {
            vector<int> eff;
            for (int y : ys)
                eff.push_back(next[y]);
            sat->andImpliesOr(vector<int>{l, cur[x]}, eff);
            for (size_t a = 0; a < ys.size(); a++)
                for (size_t b = a + 1; b < ys.size(); b++)
                    sat->andImpliesOr(vector<int>{l, cur[x], next[ys[a]], next[ys[b]]}, vector<int>{});
        }
        for (int y : changed) {
            vector<int> from;
            bool reaches = false;
            for (const auto &[x, ys] : ld.targets)
                if (binary_search(ys.begin(), ys.end(), y)) {
                    from.push_back(cur[x]);
                    reaches = true;
                }
            sat->andImpliesOr(vector<int>{l, next[y]}, from);
            if (reaches)
                reaching[y].emplace_back(l, selfLoops.count(y) > 0);
        }
        updates.push_back(SigmaUpdate{ld.label, vars, -1, (int) begin});
    }
    // the end state is one state: labels without self-loops in the factor (pure movers) can never fire
    // one after another without a chain, so at most one of them fires; otherwise at most one end
    // state variable is true (sequential ladder above 8 states)
    bool pure = true;
    for (size_t k = begin; k < end; k++)
        for (const auto &[x, ys] : labels[k].targets)
            if (binary_search(ys.begin(), ys.end(), x)) pure = false;
    if (pure) {
        vector<int> ls;
        for (size_t k = begin; k < end; k++) ls.push_back(labelVars[labels[k].label]);
        amoLadder(ls);
    } else {
        vector<int> ns;
        for (int s : changed) ns.push_back(next[s]);
        amoLadder(ns);
    }
    // two firing labels reaching the end state t: the ones without a self-loop at t are excluded
    for (auto &[t, ls] : reaching)
        for (size_t a = 0; a < ls.size(); a++)
            for (size_t b = 0; b < ls.size(); b++)
                if (a != b && !ls[a].second && (b > a || ls[b].second))
                    sat->andImpliesOr(vector<int>{next[t], ls[a].first, ls[b].first}, vector<int>{});
    // without a firing label with real transitions the state does not change
    for (int s : changed) {
        sat->andImpliesOr(vector<int>{cur[s], -next[s]}, movers);
        sat->andImpliesOr(vector<int>{-cur[s], next[s]}, movers);
    }
    if (sigma.oneMove)
        oneMove(next, changed, labels[begin].label);
    cur = move(next);
}


/*
  Compact exists-step group (group_exists with group_compact): start layer p, end layer n
  (exactly one end state). A fired label l at a start state x where it does not self-loop either
  moves the factor (n is one of its real targets from x) or runs after the mover, self-looping at
  the end: l & p_x -> OR n_{T_real(l,x) + SL(l)}. A label that self-loops at x runs before the
  mover and needs no clause (its precondition is checked at the start). A change x -> y needs a
  fired label with the real transition (x, y); a fired label that self-loops neither at x nor at y
  while x -> y happens must be the mover, so at most one such label per (x, y); labels without any
  self-loop in the factor always move, so at most one of them fires. Which label is the mover is
  decided in plan extraction (serialise_exists_groups), consistently with the serialisation.
*/
void StepEncoding::encode_group_exists_compact(int ts, const vector<SigmaLabelData> &labels, size_t begin, size_t end,
                                               const vector<int> &layer, const vector<int> &labelVars,
                                               vector<int> &cur, int fromTime, vector<SigmaUpdate> &updates) {
    (void) ts; (void) fromTime;   // debug variable names only
    set<int> changedSet;
    for (size_t k = begin; k < end; k++)
        changedSet.insert(labels[k].changed.begin(), labels[k].changed.end());
    vector<int> changed(changedSet.begin(), changedSet.end());
    vector<int> next = layer;
    vector<pair<int, int>> vars;
    for (int s : changed)
        vars.emplace_back(s, next[s]);
    map<pair<int, int>, vector<int>> realBy, forced;    // (x, y) -> label variables
    map<int, vector<int>> leaving;                      // x -> labels with a real transition out of x
    set<int> pureMovers;
    for (size_t k = begin; k < end; k++) {
        const SigmaLabelData &ld = labels[k];
        const int l = labelVars[ld.label];
        updates.push_back(SigmaUpdate{ld.label, vars, -2, (int) begin});
        if (ld.changed.empty())
            continue;
        set<int> selfLoops;
        for (const auto &[x, ys] : ld.targets)
            if (binary_search(ys.begin(), ys.end(), x))
                selfLoops.insert(x);
        if (selfLoops.empty())
            pureMovers.insert(l);
        for (const auto &[x, ys] : ld.targets) {
            for (int y : ys) {
                if (y == x) continue;
                realBy[{x, y}].push_back(l);
                leaving[x].push_back(l);
                if (!selfLoops.count(x) && !selfLoops.count(y))
                    forced[{x, y}].push_back(l);
            }
            if (selfLoops.count(x)) continue;
            vector<int> role;
            for (int y : ys) role.push_back(next[y]);
            for (int t : selfLoops) role.push_back(next[t]);
            sat->andImpliesOr(vector<int>{l, cur[x]}, role);
        }
    }
    // exactly one end state. The unchanged states share their variables with the start layer,
    // which is one-hot: one variable "the factor is in an unchanged state" excludes every changed
    // end state, and the changed end states are at most one among themselves
    sat->atLeastOne(next);
    if (changed.size() < next.size()) {
        const int unchanged = sat->new_variable();
        for (size_t x = 0; x < next.size(); x++)
            if (!changedSet.count(x))
                sat->implies(cur[x], unchanged);
        vector<int> ends{unchanged};
        for (int y : changed) ends.push_back(next[y]);
        amoLadder(ends);
    } else {
        amoLadder(next);
    }
    // leaving a changed state needs a label with a real transition out of it, entering y from x
    // one with the real transition (x, y)
    for (int x : changed) {
        vector<int> out = leaving[x];
        sort(out.begin(), out.end()); out.erase(unique(out.begin(), out.end()), out.end());
        sat->andImpliesOr(vector<int>{cur[x], -next[x]}, out);
    }
    for (const auto &[x, out] : leaving) {
        (void) out;
        for (int y : changed) {
            if (y == x) continue;
            auto it = realBy.find({x, y});
            vector<int> by;
            if (it != realBy.end()) { by = it->second; sort(by.begin(), by.end()); by.erase(unique(by.begin(), by.end()), by.end()); }
            sat->andImpliesOr(vector<int>{cur[x], next[y]}, by);
        }
    }
    for (auto &[xy, ls] : forced)
        for (size_t a = 0; a < ls.size(); a++)
            for (size_t b = a + 1; b < ls.size(); b++)
                if (ls[a] != ls[b])
                    sat->andImpliesOr(vector<int>{ls[a], ls[b], cur[xy.first], next[xy.second]}, vector<int>{});
    amoLadder(vector<int>(pureMovers.begin(), pureMovers.end()));
    cur = move(next);
}

/*
  group_exists: the fired labels of an exists-step group have no fixed order. Per factor, a
  segment of the group has a start and an end state and (if it changes) a mover; a fired non-mover
  that self-loops at the start but not at the end runs before the mover, one that self-loops at
  the end but not at the start after it. The groups are exists-safe, so these precedences are
  acyclic; the fired labels of every group are reordered topologically (ties in label order).
*/
void StepEncoding::serialise_exists_groups(vector<int> &selectedLabels, const vector<int> &currentLastState,
                                           int labelTimestep) {
    const auto &updates = allTimesUpdates.at(labelTimestep);
    existsMover.clear();
    set<int> fired(selectedLabels.begin(), selectedLabels.end());
    map<int, set<int>> before;   // label -> labels that must run before it
    for (int ts = 0; ts < fts->get_size(); ts++) {
        map<int, const SigmaLabelData *> byLabel;
        for (const SigmaLabelData &ld : sigma.factors[ts]) byLabel[ld.label] = &ld;
        auto selfLoop = [&](int label, int state) {
            const SigmaLabelData &ld = *byLabel.at(label);
            for (const auto &[x, ys] : ld.targets)
                if (x == state) return binary_search(ys.begin(), ys.end(), state);
            return false;
        };
        int state = currentLastState[ts];
        for (size_t k = 0; k < updates[ts].size();) {
            // one segment: a single label (mover 0) or the updates of a group (same group start)
            size_t e = k + 1;
            if (updates[ts][k].mover != 0)
                while (e < updates[ts].size() && updates[ts][e].mover != 0 && updates[ts][e].group == updates[ts][k].group)
                    e++;
            const int start = state;
            int end = start;
            for (const auto &[x, var] : updates[ts][k].vars)
                if (ipasir_val(sat->solver, var) > 0) { end = x; break; }
            if (updates[ts][k].mover != 0 && updates[ts][k].mover != -1 && end != start) {
                int mover = -1;
                if (updates[ts][k].mover == -2) {
                    // compact: a fired label with the real transition (start, end) that self-loops
                    // at neither (it must move), else the first one with the transition
                    auto realMove = [&](int label) {
                        const SigmaLabelData &ld = *byLabel.at(label);
                        for (const auto &[x, ys] : ld.targets)
                            if (x == start) return binary_search(ys.begin(), ys.end(), end);
                        return false;
                    };
                    for (size_t j = k; j < e; j++) {
                        const int a = updates[ts][j].label;
                        if (!fired.count(a) || !realMove(a)) continue;
                        if (mover < 0) mover = a;
                        if (!selfLoop(a, start) && !selfLoop(a, end)) { mover = a; break; }
                    }
                    existsMover[{ts, updates[ts][k].group}] = mover;
                } else {
                    for (size_t j = k; j < e; j++)
                        if (ipasir_val(sat->solver, updates[ts][j].mover) > 0) mover = updates[ts][j].label;
                }
                assert(mover >= 0);
                // the group's labels relevant here: all of the factor's labels in that group
                const int g = sigma.groupOf[mover];
                for (const SigmaLabelData &ld : sigma.factors[ts]) {
                    const int a = ld.label;
                    if (a == mover || sigma.groupOf[a] != g || !fired.count(a)) continue;
                    const bool atStart = selfLoop(a, start), atEnd = selfLoop(a, end);
                    if (atStart && !atEnd) before[mover].insert(a);
                    else if (!atStart && atEnd) before[a].insert(mover);
                }
            }
            state = end;
            k = e;
        }
    }
    // topological order inside every group (the labels arrive sorted by the label order)
    vector<int> result;
    for (size_t i = 0; i < selectedLabels.size();) {
        size_t j = i;
        while (j < selectedLabels.size() && sigma.groupOf[selectedLabels[j]] == sigma.groupOf[selectedLabels[i]]) j++;
        vector<int> group(selectedLabels.begin() + i, selectedLabels.begin() + j);
        set<int> done;
        while (done.size() < group.size()) {
            bool progress = false;
            for (int a : group) {
                if (done.count(a)) continue;
                bool ready = true;
                for (int b : before[a]) if (!done.count(b)) ready = false;
                if (!ready) continue;
                result.push_back(a);
                done.insert(a);
                progress = true;
                break;
            }
            if (!progress) {
                cerr << "exists group without a serialisation" << endl;
                utils::exit_with(utils::ExitCode::CRITICAL_ERROR);
            }
        }
        i = j;
    }
    selectedLabels = result;
}

vector<vector<int>> StepEncoding::segments_extract_intermediate_states(vector<int> &selectedLabels,
                                                             vector<int> &currentLastState,
                                                             vector<int> & /*nextState*/,
                                                             int labelTimestep) {
    if (sigma.groupExists)
        serialise_exists_groups(selectedLabels, currentLastState, labelTimestep);
    // selectedLabels are in label order; per factor, a label that changes the state (its own
    // micro-step, or the mover of its group) sets it to the changed state whose variable is
    // true, or keeps it if none is (a self-loop at an unchanged state)
    const auto &updates = allTimesUpdates.at(labelTimestep);
    vector<map<int, const SigmaUpdate *>> byLabel(fts->get_size());
    for (int ts = 0; ts < fts->get_size(); ts++)
        for (const SigmaUpdate &u : updates[ts])
            byLabel[ts][u.label] = &u;
    vector<vector<int>> states;
    vector<int> state = currentLastState;
    vector<int> movedGroup(fts->get_size(), -1);
    for (size_t i = 0; i + 1 < selectedLabels.size(); i++) {
        for (int ts = 0; ts < fts->get_size(); ts++) {
            auto it = byLabel[ts].find(selectedLabels[i]);
            if (it == byLabel[ts].end())
                continue;
            const SigmaUpdate &u = *it->second;
            if (u.mover == -2) {
                // compact exists group: only the mover chosen by serialise_exists_groups moves
                auto it = existsMover.find({ts, u.group});
                if (it == existsMover.end() || it->second != u.label)
                    continue;
                for (const auto &[s, var] : u.vars)
                    if (ipasir_val(sat->solver, var) > 0) {
                        state[ts] = s;
                        break;
                    }
                continue;
            }
            if (u.mover == -1) {
                // matrix group: the first firing label with the real transition (start, end) moves
                if (movedGroup[ts] == u.group)
                    continue;
                int endState = state[ts];
                for (const auto &[s, var] : u.vars)
                    if (ipasir_val(sat->solver, var) > 0) {
                        endState = s;
                        break;
                    }
                if (endState == state[ts])
                    continue;
                for (const Transition &tr : fts->get_ts(ts).get_transitions_with_label(u.label))
                    if (tr.src == state[ts] && tr.target == endState) {
                        state[ts] = endState;
                        movedGroup[ts] = u.group;
                        break;
                    }
                continue;
            }
            if (u.mover && ipasir_val(sat->solver, u.mover) <= 0)
                continue;
            for (const auto &[s, var] : u.vars)
                if (ipasir_val(sat->solver, var) > 0) {
                    state[ts] = s;
                    break;
                }
        }
        states.push_back(state);
    }
    return states;
}



// ---------------------------------------------------------------------------
// ROWS style (micro_steps_sat, micro_steps_encoding.cc by Joao)
// ---------------------------------------------------------------------------

void StepEncoding::RowsStatistics::print() const {
    cout << "Micro-steps statistics (first time step):" << endl;
    cout << "  layer variables       : " << layerVars << endl;
    cout << "  at-most-one aux vars  : " << amoAuxVars << endl;
    cout << "  (22) at-least-one     : " << c22_alo << endl;
    cout << "  (22) at-most-one      : " << c22_amo << endl;
    cout << "  (23) frame axioms     : " << c23 << endl;
    cout << "  (24) src -> -label    : " << c24 << endl;
    cout << "  (25) label -> -tgt    : " << c25 << endl;
    cout << "  (26) src -> -tgt      : " << c26 << endl;
    cout << "  (27) src+label -> -src: " << c27 << endl;
    cout << "  (28) dual label src   : " << d28 << endl;
    cout << "  (29) dual label tgt   : " << d29 << endl;
    cout << "  (30) dual src guarded : " << d30 << endl;
    cout << "  (31) dual src         : " << d31 << endl;
    cout << "  (32) dual tgt guarded : " << d32 << endl;
    cout << "  (33) dual tgt         : " << d33 << endl;
    cout << "  links to next state   : " << links << endl;
}

// (22) exactly one state per layer; with variable sharing only the pairs involving a new variable
void StepEncoding::rows_layer_exactly_one(const vector<int> &layer, const vector<int> &newStates,
                                          const vector<char> &isNew) {
    sat->atLeastOne(layer);
    rowsStats.c22_alo++;

    if (newStates.size() == layer.size()) {
        int before = sat->get_number_of_clauses();
        sat->atMostOne(layer);
        rowsStats.c22_amo += sat->get_number_of_clauses() - before;
        return;
    }

    // Variable sharing: the at-most-one constraint between two shared states
    // is already enforced by an earlier layer. Only pairs involving at least
    // one new variable are needed.
    vector<int> newVars, sharedVars;
    for (size_t s = 0; s < layer.size(); s++)
        (isNew[s] ? newVars : sharedVars).push_back(layer[s]);

    int before = sat->get_number_of_clauses();
    if (layer.size() < 256) {
        sat->atMostOneBinomial(newVars);
        for (int a : newVars)
            for (int b : sharedVars)
                sat->impliesNot(a, b);
    } else {
        // Large layer: introduce one variable "some shared state is true" and
        // make the new states mutually exclusive with it.
        int someShared = sat->new_variable();
        DEBUG(sat->registerVariable(someShared, "MS-some-shared-state"));
        rowsStats.amoAuxVars++;
        for (int b : sharedVars)
            sat->implies(b, someShared);
        newVars.push_back(someShared);
        sat->atMostOne(newVars);
    }
    rowsStats.c22_amo += sat->get_number_of_clauses() - before;
}

/*
  ROWS: one label's micro-step from layer `cur` to a new layer (new variables for the states the
  label moves, or for all states with full_layers). Every row of the label's (source, target)
  matrix -- the targets of a source, the sources of a target -- is encoded by its dual clause if
  that has at most rows_dual_threshold literals, else by its primal clauses; the label-source and
  label-target projections likewise. The layer is exactly-one (22). Constraint numbers as in the
  R2E paper's micro-step section.
*/
void StepEncoding::encode_single_rows(int ts, const SigmaLabelData &ld, const vector<int> &layer, const vector<int> &labelVars,
                                      vector<int> &cur, int fromTime, vector<SigmaUpdate> &updates) {
    (void) ts; (void) fromTime;   // debug variable names only
    const int numStates = cur.size();
    const int threshold = sigma.clauses.rowsDualThreshold;
    auto useDual = [&](size_t arity) { return threshold < 0 || arity <= (size_t) threshold; };
    const int lab = labelVars[ld.label];

    vector<vector<int>> tgtOf(numStates), srcOf(numStates);
    vector<char> selfLoop(numStates, 0), isSrc(numStates, 0), isTgt(numStates, 0), isNew(numStates, 0);
    vector<char> dualSrc(numStates, 0), dualTgt(numStates, 0), mark(numStates, 0);
    set<int> targetSet;
    for (const auto &[s, ts_] : ld.targets)
        for (int t : ts_) {
            tgtOf[s].push_back(t);
            srcOf[t].push_back(s);
            if (s == t) selfLoop[s] = 1;
            targetSet.insert(t);
        }
    // srcOf in transition order (sorted by source, then target)
    for (int s : ld.applicable) isSrc[s] = 1;
    vector<int> targets(targetSet.begin(), targetSet.end());
    for (int t : targets) isTgt[t] = 1;

    // Row choices: dual iff the arity of the dual clause is <= tau.
    // Source row of s: (31) if l_k loops at s, else (30); symmetric for targets.
    for (int s = 0; s < numStates; s++) {
        dualSrc[s] = useDual((selfLoop[s] ? 1 : 2) + tgtOf[s].size());
        dualTgt[s] = useDual((selfLoop[s] ? 1 : 2) + srcOf[s].size());
    }
    const bool dual28 = useDual(1 + ld.applicable.size());
    const bool dual29 = useDual(1 + targets.size());

    // the new layer: new variables for the states the label moves (all with full_layers)
    const vector<int> newStates = segment_new_states(ld, numStates);
    vector<int> next = layer;
    SigmaUpdate upd{ld.label, {}, 0, -1};
    for (int s : newStates) {
        isNew[s] = 1;
        upd.vars.emplace_back(s, next[s]);
    }
    rowsStats.layerVars += newStates.size();
    const vector<int> &prev = cur;

    if (!newStates.empty()) {
        // (22) exactly one state per layer
        rows_layer_exactly_one(next, newStates, isNew);

        // (23) frame axioms: s^{k-1} & -l -> s^k (tautology for shared states)
        for (int s : newStates) {
            sat->andImplies(prev[s], -lab, next[s]);
            rowsStats.c23++;
        }
    }

    // (28) l -> OR_{s in src(l)} s^{k-1}
    if (dual28) {
        vector<int> rhs;
        for (int s : ld.applicable) rhs.push_back(prev[s]);
        sat->impliesOr(lab, rhs);
        rowsStats.d28++;
    }
    // (29) l -> OR_{t in tgt(l)} t^k
    if (dual29) {
        vector<int> rhs;
        for (int t : targets) rhs.push_back(next[t]);
        sat->impliesOr(lab, rhs);
        rowsStats.d29++;
    }

    // (24) s^{k-1} -> -l for s not in src(l). Coincides with the guarded
    // dual (30) of s, so it is generated if that row is dual or if (28) is not.
    for (int s = 0; s < numStates; s++) {
        if (isSrc[s]) continue;
        if (dualSrc[s] || !dual28) {
            sat->impliesNot(prev[s], lab);
            rowsStats.c24++;
        }
    }

    // (25) l -> -t^k for t not in tgt(l). For shared states it coincides
    // with (24). Coincides with the guarded dual (32) of t.
    for (int t : newStates) {
        if (isTgt[t]) continue;
        if (dualTgt[t] || !dual29) {
            sat->impliesNot(lab, next[t]);
            rowsStats.c25++;
        }
    }

    // Dual source rows (states not in src(l) are handled by (24) above;
    // the dual rows of shared states are tautologies).
    for (int s : ld.applicable) {
        if (!isNew[s] || !dualSrc[s]) continue;
        vector<int> rhs;
        for (int t : tgtOf[s]) rhs.push_back(next[t]);
        if (selfLoop[s]) {
            sat->impliesOr(prev[s], rhs);            // (31) s^{k-1} -> OR t^k
            rowsStats.d31++;
        } else {
            sat->andImpliesOr(prev[s], lab, rhs);    // (30) s^{k-1} & l -> OR t^k
            rowsStats.d30++;
        }
    }

    // Dual target rows (states not in tgt(l) are handled by (25) above)
    for (int t : targets) {
        if (!isNew[t] || !dualTgt[t]) continue;
        vector<int> rhs;
        for (int s : srcOf[t]) rhs.push_back(prev[s]);
        if (selfLoop[t]) {
            sat->impliesOr(next[t], rhs);            // (33) t^k -> OR s^{k-1}
            rowsStats.d33++;
        } else {
            sat->andImpliesOr(lab, next[t], rhs);    // (32) l & t^k -> OR s^{k-1}
            rowsStats.d32++;
        }
    }

    // (26) s^{k-1} -> -t^k for s != t and (s,l,t) not in T, unless the
    // source row of s or the target row of t is dual. Pairs involving a
    // shared state coincide with at-most-one clauses of layer k-1 or k.
    for (int s : newStates) {
        if (dualSrc[s]) continue;
        for (int t : tgtOf[s]) mark[t] = 1;
        for (int t : newStates) {
            if (t == s || mark[t] || dualTgt[t]) continue;
            sat->impliesNot(prev[s], next[t]);
            rowsStats.c26++;
        }
        for (int t : tgtOf[s]) mark[t] = 0;
    }

    // (27) s^{k-1} & l -> -s^k for s in src(l) without self-loop, unless
    // the source or target row of s is dual.
    for (int s : ld.applicable) {
        if (!isNew[s] || selfLoop[s] || dualSrc[s] || dualTgt[s]) continue;
        sat->andImplies(prev[s], lab, -next[s]);
        rowsStats.c27++;
    }

    if (!newStates.empty()) {
        updates.push_back(move(upd));
        cur = move(next);
    }
}

}
