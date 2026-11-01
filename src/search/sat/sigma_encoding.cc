#include "sigma_encoding.h"

#include "ipasir.h"
#include "sat_encoder.h"

#include "../options/option_parser.h"
#include "../options/options.h"
#include "../options/plugin.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/transition_system.h"
#include "../task_utils/label_order_finder.h"
#include "../utils/system.h"
#include "../utils/timer.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <functional>
#include <set>

using namespace std;
using namespace task_representation;

namespace sat_search {

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

SigmaEncodingFactory::SigmaEncodingFactory(const options::Options &opts)
    : SATEncodingFactory(opts.get<bool>("force_at_least_one_action")),
      labelOrderFinder(opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("label_order")),
      oneMove(opts.get<bool>("one_move")),
      strict(opts.get<bool>("strict")),
      groupSize(opts.get<int>("group_size")),
      groupMatrix(opts.get<bool>("group_matrix")),
      groupCompact(opts.get<bool>("group_compact")),
      groupPureAmo(opts.get<bool>("group_pure_amo")),
      competingGroups(opts.get<bool>("competing_groups")),
      groupOrder(opts.get<bool>("group_order")),
      factorGroups(opts.get<bool>("factor_groups")),
      factorGroupsExact(opts.get<bool>("factor_groups_exact")),
      groupAllowChains(opts.get<bool>("group_allow_chains")),
      groupKeepEnablers(opts.get<bool>("group_keep_enablers")),
      groupMax(opts.get<int>("group_max")),
      staticOptions(opts) {
    if (strict && !oneMove) {
        cerr << "sigma_sat: strict needs one_move" << endl;
        utils::exit_with(utils::ExitCode::INPUT_ERROR);
    }
}

/*
  group_order: a label order that puts competing labels next to each other, and the micro-step
  groups along it. Clusters are formed along the base order (d.order): a label joins the open
  cluster whose changed states overlap its own most (summed over the factors), provided that
  (1) no two labels of the cluster chain in a shared factor (one starts where another ends),
  (2) no label between the cluster's first member and this label in the base order moves into one of
      this label's real sources (moving it forward would lose that enabling), and
  (3) the cluster has fewer than group_max labels.
  Otherwise it opens a new cluster. Clusters are ordered by their first member's base position,
  labels inside a cluster by base position; each cluster is one group.
*/
void SigmaEncodingFactory::compute_group_order(SigmaData &d) const {
    const int numLabels = fts->get_num_labels();
    vector<int> pos(numLabels);
    for (size_t k = 0; k < d.order.size(); k++)
        pos[d.order[k]] = k;
    struct Moves { int ts; set<int> src, tgt; };
    vector<vector<Moves>> moves(numLabels);
    // (factor, state) -> base positions of the labels moving into it, sorted
    map<pair<int, int>, vector<int>> into;
    for (int ts = 0; ts < fts->get_size(); ts++)
        for (const SigmaLabelData &ld : d.factors[ts]) {
            if (ld.changed.empty()) continue;
            Moves m{ts, {}, {}};
            for (const auto &[x, ys] : ld.targets)
                for (int y : ys)
                    if (y != x) { m.src.insert(x); m.tgt.insert(y); into[{ts, y}].push_back(pos[ld.label]); }
            moves[ld.label].push_back(move(m));
        }
    for (auto &[k, v] : into) sort(v.begin(), v.end());
    struct Cluster { int first; vector<int> labels; map<int, set<int>> src, tgt; bool open = true; };
    vector<Cluster> clusters;
    map<pair<int, int>, vector<int>> byState;   // (factor, changed state) -> clusters
    vector<int> clusterOf(numLabels, -1);
    for (int label : d.order) {
        const int p = pos[label];
        map<int, int> overlap;   // cluster -> shared changed states
        for (const Moves &m : moves[label])
            for (const set<int> *ss : {&m.src, &m.tgt})
                for (int x : *ss) {
                    auto it = byState.find({m.ts, x});
                    if (it == byState.end()) continue;
                    for (int c : it->second) overlap[c]++;
                }
        int best = -1, bestOverlap = 0;
        for (auto [c, o] : overlap) {
            const Cluster &cl = clusters[c];
            if (!cl.open || (int) cl.labels.size() >= groupMax || o <= bestOverlap) continue;
            bool ok = true;
            for (const Moves &m : moves[label]) {
                auto cs = cl.src.find(m.ts), ct = cl.tgt.find(m.ts);
                if (!groupAllowChains) {
                    for (int x : m.src) if (ct != cl.tgt.end() && ct->second.count(x)) ok = false;   // cluster -> label
                    for (int y : m.tgt) if (cs != cl.src.end() && cs->second.count(y)) ok = false;   // label -> cluster
                }
                // an enabler of the label between the cluster's first member and the label
                if (groupKeepEnablers) for (int x : m.src) {
                    auto it = into.find({m.ts, x});
                    if (it == into.end()) continue;
                    auto lo = upper_bound(it->second.begin(), it->second.end(), cl.first);
                    if (lo != it->second.end() && *lo < p) ok = false;
                }
                if (!ok) break;
            }
            if (ok) { best = c; bestOverlap = o; }
        }
        if (best < 0) {
            best = clusters.size();
            clusters.push_back(Cluster{p, {}, {}, {}});
        }
        Cluster &cl = clusters[best];
        cl.labels.push_back(label);
        clusterOf[label] = best;
        for (const Moves &m : moves[label]) {
            cl.src[m.ts].insert(m.src.begin(), m.src.end());
            cl.tgt[m.ts].insert(m.tgt.begin(), m.tgt.end());
            for (const set<int> *ss : {&m.src, &m.tgt})
                for (int x : *ss) {
                    auto &v = byState[{m.ts, x}];
                    if (v.empty() || v.back() != best) v.push_back(best);
                }
        }
        if (moves[label].empty()) cl.open = false;   // a label without moves stays alone
    }
    // the new order: clusters by first member, labels inside by base position (already so)
    vector<int> order;
    d.groupOf.assign(numLabels, 0);
    for (size_t c = 0; c < clusters.size(); c++)
        for (int label : clusters[c].labels) {
            order.push_back(label);
            d.groupOf[label] = c;
        }
    d.order = order;
    vector<int> newPos(numLabels);
    for (size_t k = 0; k < order.size(); k++) newPos[order[k]] = k;
    for (auto &labels : d.factors)
        stable_sort(labels.begin(), labels.end(), [&](const SigmaLabelData &a, const SigmaLabelData &b) {
            return newPos[a.label] < newPos[b.label]; });
    size_t multi = 0, inMulti = 0;
    for (const Cluster &cl : clusters)
        if (cl.labels.size() > 1) { multi++; inMulti += cl.labels.size(); }
    cout << "SIGMAGROUPS groups " << clusters.size() << " labels " << order.size() << " merged "
         << order.size() - clusters.size() << " multi_groups " << multi << " labels_in_multi " << inMulti << endl;
}

void SigmaEncodingFactory::initialize() {
    utils::Timer timer;
    auto d = make_shared<SigmaData>();
    d->oneMove = oneMove;
    d->strict = strict;
    d->order = labelOrderFinder->find_order(*fts);
    d->groupMatrix = groupMatrix;
    d->groupCompact = groupCompact;
    d->groupPureAmo = groupPureAmo;
    d->groupOf.assign(fts->get_num_labels(), 0);
    if (!competingGroups) {
        for (size_t k = 0; k < d->order.size(); k++)
            d->groupOf[d->order[k]] = k / max(groupSize, 1);
    }
    d->factors.resize(fts->get_size());
    size_t changedVars = 0, labelsInFactors = 0, nondet = 0;
    for (int ts = 0; ts < fts->get_size(); ts++) {
        const TransitionSystem &tss = fts->get_ts(ts);
        for (int label : d->order) {
            if (tss.isIrrelevantLabel(label))
                continue;
            SigmaLabelData ld;
            ld.label = label;
            map<int, set<int>> targets, preds;
            set<int> changed, applicable;
            for (const Transition &t : tss.get_transitions_with_label(label)) {
                applicable.insert(t.src);
                targets[t.src].insert(t.target);
                if (t.src != t.target) {
                    changed.insert(t.src);
                    changed.insert(t.target);
                }
            }
            for (const Transition &t : tss.get_transitions_with_label(label))
                if (changed.count(t.target))
                    preds[t.target].insert(t.src);
            ld.changed.assign(changed.begin(), changed.end());
            ld.applicable.assign(applicable.begin(), applicable.end());
            for (auto &[x, ys] : targets) {
                ld.targets.emplace_back(x, vector<int>(ys.begin(), ys.end()));
                nondet += ys.size() > 1;
            }
            for (int s : ld.changed)
                ld.predecessors.emplace_back(s, vector<int>(preds[s].begin(), preds[s].end()));
            changedVars += ld.changed.size();
            labelsInFactors++;
            d->factors[ts].push_back(move(ld));
        }
    }
    if (factorGroups) {
        // per factor, along the factor's labels: a block of labels without self-loops there that do
        // not chain (no label starts where another of the block ends); such labels can never fire
        // one after another in a step, so a forall-step block loses no parallelism
        d->factorBlock.resize(fts->get_size());
        size_t blocks = 0, entries = 0;
        for (int ts = 0; ts < fts->get_size(); ts++) {
            const vector<SigmaLabelData> &ls = d->factors[ts];
            vector<int> &blk = d->factorBlock[ts];
            blk.assign(ls.size(), 0);
            int b = -1, size = 0;
            set<int> bSrc, bTgt, bSl;
            bool open = false;
            for (size_t k = 0; k < ls.size(); k++) {
                const SigmaLabelData &ld = ls[k];
                set<int> src, tgt;
                bool selfLoop = false;
                for (const auto &[x, ys] : ld.targets)
                    for (int y : ys) {
                        if (y == x) selfLoop = true;
                        else { src.insert(x); tgt.insert(y); }
                    }
                bool join;
                if (factorGroupsExact) {
                    // exact: for a before b in the block, a's real targets are not where b is
                    // applicable, and a's self-loops are not b's real sources
                    join = size > 0 && size < groupMax;
                    if (join) {
                        for (int x : ld.applicable) if (bTgt.count(x)) join = false;
                        for (int x : src) if (bSl.count(x)) join = false;
                    }
                } else {
                    const bool mover = !selfLoop && !src.empty();
                    join = open && mover && size < groupMax;
                    if (join) {
                        for (int x : src) if (bTgt.count(x)) join = false;
                        for (int y : tgt) if (bSrc.count(y)) join = false;
                    }
                    if (!join) open = mover;
                }
                if (!join) {
                    b++; size = 0; bSrc.clear(); bTgt.clear(); bSl.clear();
                    blocks++;
                }
                blk[k] = b;
                size++;
                bSrc.insert(src.begin(), src.end());
                bTgt.insert(tgt.begin(), tgt.end());
                for (const auto &[x, ys] : ld.targets)
                    if (binary_search(ys.begin(), ys.end(), x)) bSl.insert(x);
                entries++;
            }
        }
        cout << "SIGMAGROUPS factor_blocks " << blocks << " label_factor_entries " << entries << endl;
    } else if (groupOrder) {
        compute_group_order(*d);
    } else if (competingGroups) {
        // per label: (factor, real sources, real targets)
        struct Moves { int ts; vector<int> src, tgt; };
        vector<vector<Moves>> moves(fts->get_num_labels());
        for (int ts = 0; ts < fts->get_size(); ts++)
            for (const SigmaLabelData &ld : d->factors[ts]) {
                if (ld.changed.empty()) continue;
                Moves m{ts, {}, {}};
                for (const auto &[x, ys] : ld.targets)
                    for (int y : ys)
                        if (y != x) { m.src.push_back(x); m.tgt.push_back(y); }
                sort(m.src.begin(), m.src.end()); m.src.erase(unique(m.src.begin(), m.src.end()), m.src.end());
                sort(m.tgt.begin(), m.tgt.end()); m.tgt.erase(unique(m.tgt.begin(), m.tgt.end()), m.tgt.end());
                moves[ld.label].push_back(move(m));
            }
        // greedily along the order: a label joins the current group if, in some factor, its
        // changed states overlap the group's (fewer variables), in no factor it starts from a
        // state a group label moves to (no chain is lost), and the group has fewer than group_max
        int group = 0, size = 0;
        map<int, set<int>> gChanged, gTargets;   // factor -> states of the current group
        size_t groups = 0, merged = 0;
        for (int label : d->order) {
            bool overlap = false, chain = false;
            for (const Moves &m : moves[label]) {
                auto c = gChanged.find(m.ts);
                if (c == gChanged.end()) continue;
                for (int x : m.src) if (c->second.count(x)) overlap = true;
                for (int y : m.tgt) if (c->second.count(y)) overlap = true;
                for (int x : m.src) if (gTargets[m.ts].count(x)) chain = true;
            }
            if (size > 0 && (!overlap || chain || size >= groupMax)) {
                group++; size = 0; gChanged.clear(); gTargets.clear();
            }
            if (size > 0) merged++;
            d->groupOf[label] = group;
            size++;
            for (const Moves &m : moves[label]) {
                gChanged[m.ts].insert(m.src.begin(), m.src.end());
                gChanged[m.ts].insert(m.tgt.begin(), m.tgt.end());
                gTargets[m.ts].insert(m.tgt.begin(), m.tgt.end());
            }
        }
        groups = group + 1;
        cout << "SIGMAGROUPS groups " << groups << " labels " << d->order.size() << " merged " << merged << endl;
    }
    data = d;
    staticConstraints = static_constraints::compute(*fts, staticOptions);
    cout << "SIGMASTAT sigma_vars_per_step " << changedVars << " label_factor_pairs " << labelsInFactors
         << " nondeterministic_sources " << nondet << " time " << timer() << endl;
    cout << "SAT init time: " << timer << endl;
}

unique_ptr<SATEncoding> SigmaEncodingFactory::createEncodingInstance(shared_ptr<sat_capsule> capsule) {
    auto encoding = make_unique<SigmaEncoding>(capsule, fts, forceAtLeastOneAction, data);
    encoding->setStaticConstraints(staticConstraints);
    return encoding;
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

SigmaEncoding::SigmaEncoding(shared_ptr<sat_capsule> capsule, const shared_ptr<FTSTask> &fts,
                             bool forceAtLeastOneAction, shared_ptr<const SigmaData> _data)
    : CommonEncoding(capsule, fts, forceAtLeastOneAction, false, true), data(_data) {
    // plan extraction executes the labels of a step in this order
    labelOrder = data->order;
}

void SigmaEncoding::generateAdditionalVariables(int /*fromTime*/) {
}

void SigmaEncoding::encode_frame_axioms(const vector<vector<int>> &, const vector<vector<int>> &, int) {
    // a label that does not fire copies the factor's state (encode_transition)
}

void SigmaEncoding::encode_transition(const vector<vector<int>> &previousStateVars,
                                      const vector<vector<int>> &nextStateVars, int fromTime) {
    const vector<int> &labelVars = allTimesLabelVars[fromTime];
    auto &updates = allTimesUpdates[fromTime];
    updates.assign(fts->get_size(), {});
    for (int ts = 0; ts < fts->get_size(); ts++) {
        vector<int> cur = previousStateVars[ts];   // sigma variables of the current position
        int moved = 0;                             // one_move: M of the current position (0 = false)
        const vector<SigmaLabelData> &labels = data->factors[ts];
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
        for (size_t i = 0; i < labels.size();) {
            // the labels of this factor in the same group
            size_t j = i + 1;
            if (!data->factorBlock.empty()) {
                const vector<int> &blk = data->factorBlock[ts];
                while (j < labels.size() && blk[j] == blk[i])
                    j++;
            } else {
                while (j < labels.size() && data->groupOf[labels[j].label] == data->groupOf[labels[i].label])
                    j++;
            }
            // preconditions (all labels: applicable at the group's start state)
            for (size_t k = i; k < j; k++) {
                const SigmaLabelData &ld = labels[k];
                const int l = labelVars[ld.label];
                vector<int> pre;
                for (int x : ld.applicable)
                    pre.push_back(cur[x]);
                sat->impliesOr(l, pre);
                if (data->strict) {
                    vector<int> start;
                    for (int x : ld.applicable)
                        start.push_back(previousStateVars[ts][x]);
                    sat->impliesOr(l, start);
                }
            }
            if (j == i + 1) {
                encode_single(ts, labels[i], labelVars, cur, fromTime, updates[ts], oneMoveClauses);
            } else if (data->groupCompact) {
                encode_group_compact(ts, labels, i, j, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
            } else if (data->groupMatrix) {
                encode_group_matrix(ts, labels, i, j, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
            } else {
                encode_group(ts, labels, i, j, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
            }
            i = j;
        }
        // the last sigma is the state at the end of the step
        for (size_t s = 0; s < cur.size(); s++) {
            if (cur[s] == nextStateVars[ts][s])
                continue;
            sat->implies(cur[s], nextStateVars[ts][s]);
            sat->implies(nextStateVars[ts][s], cur[s]);
        }
    }
}

/*
  One label in its own micro-step: new variables for the states it changes, not-fired copy,
  effect, at most one target per source, justification.
*/
void SigmaEncoding::encode_single(int ts, const SigmaLabelData &ld, const vector<int> &labelVars,
                                  vector<int> &cur, int fromTime, vector<SigmaUpdate> &updates,
                                  const function<void(const vector<int> &, const vector<int> &, int)> &oneMove) {
    (void) ts; (void) fromTime;   // debug variable names only
    if (ld.changed.empty())
        return;
    const int l = labelVars[ld.label];
    vector<int> next = cur;
    SigmaUpdate upd{ld.label, {}, 0, -1};
    for (int s : ld.changed) {
        next[s] = sat->new_variable();
        DEBUG(sat->registerVariable(next[s], "sigma_" + to_string(ts) + "_" + to_string(ld.label) +
                                    "_" + to_string(s) + "@" + to_string(fromTime)));
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
    if (data->oneMove)
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
void SigmaEncoding::encode_group(int ts, const vector<SigmaLabelData> &labels, size_t begin, size_t end,
                                 const vector<int> &labelVars, vector<int> &cur, int fromTime,
                                 vector<SigmaUpdate> &updates,
                                 const function<void(const vector<int> &, const vector<int> &, int)> &oneMove) {
    (void) ts; (void) fromTime;   // debug variable names only
    set<int> changedSet;
    for (size_t k = begin; k < end; k++)
        changedSet.insert(labels[k].changed.begin(), labels[k].changed.end());
    vector<int> changed(changedSet.begin(), changedSet.end());
    vector<int> next = cur;
    vector<pair<int, int>> vars;
    for (int s : changed) {
        next[s] = sat->new_variable();
        DEBUG(sat->registerVariable(next[s], "sigmaG_" + to_string(ts) + "_" + to_string(labels[begin].label) +
                                    "_" + to_string(s) + "@" + to_string(fromTime)));
        vars.emplace_back(s, next[s]);
    }
    vector<int> movers;
    for (size_t k = begin; k < end; k++) {
        const SigmaLabelData &ld = labels[k];
        const int l = labelVars[ld.label];
        set<int> selfLoops;
        for (const auto &[x, ys] : ld.targets)
            if (binary_search(ys.begin(), ys.end(), x))
                selfLoops.insert(x);
        if (ld.changed.empty()) {
            // only self-loops: also at the end state
            if (!changed.empty()) {
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
        // a firing label without a self-loop at the start or the end state is the mover
        for (int x : ld.applicable)
            if (!selfLoops.count(x))
                sat->andImpliesOr(vector<int>{l, cur[x]}, vector<int>{mv});
        for (int y : changed)
            if (!selfLoops.count(y))
                sat->andImpliesOr(vector<int>{l, next[y]}, vector<int>{mv});
        // the mover follows its transitions
        for (const auto &[x, ys] : ld.targets) {
            vector<int> eff;
            for (int y : ys)
                eff.push_back(next[y]);
            sat->andImpliesOr(vector<int>{mv, cur[x]}, eff);
            for (size_t a = 0; a < ys.size(); a++)
                for (size_t b = a + 1; b < ys.size(); b++)
                    sat->andImpliesOr(vector<int>{mv, cur[x], next[ys[a]], next[ys[b]]}, vector<int>{});
        }
        for (int y : changed) {
            vector<int> from;
            for (const auto &[x, ys] : ld.targets)
                if (binary_search(ys.begin(), ys.end(), y))
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
    if (data->oneMove)
        oneMove(next, changed, labels[begin].label);
    cur = move(next);
}

// at most one of xs: pairwise up to 8, else a sequential ladder (Sinz) with |xs|-1 helper variables
void SigmaEncoding::amoLadder(const vector<int> &xs) {
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
void SigmaEncoding::encode_group_compact(int ts, const vector<SigmaLabelData> &labels, size_t begin, size_t end,
                                         const vector<int> &labelVars, vector<int> &cur, int fromTime,
                                         vector<SigmaUpdate> &updates, const OneMove &oneMove) {
    (void) ts; (void) fromTime;   // debug variable names only
    set<int> changedSet;
    for (size_t k = begin; k < end; k++)
        changedSet.insert(labels[k].changed.begin(), labels[k].changed.end());
    vector<int> changed(changedSet.begin(), changedSet.end());
    vector<int> next = cur;
    vector<pair<int, int>> vars;
    for (int s : changed) {
        next[s] = sat->new_variable();
        vars.emplace_back(s, next[s]);
    }
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
    if (data->groupPureAmo)
        amoLadder(vector<int>(pureMovers.begin(), pureMovers.end()));
    for (auto &[ut, ls] : moves)
        for (size_t a = 0; a < ls.size(); a++)
            for (size_t b = a + 1; b < ls.size(); b++)
                if (ls[a].first != ls[b].first && (!ls[a].second || !ls[b].second)
                    && !(data->groupPureAmo && pureMovers.count(ls[a].first) && pureMovers.count(ls[b].first)))
                    sat->andImpliesOr(vector<int>{ls[a].first, ls[b].first, cur[ut.first], next[ut.second]}, vector<int>{});
    if (data->oneMove)
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
void SigmaEncoding::encode_group_matrix(int ts, const vector<SigmaLabelData> &labels, size_t begin, size_t end,
                                        const vector<int> &labelVars, vector<int> &cur, int fromTime,
                                        vector<SigmaUpdate> &updates, const OneMove &oneMove) {
    (void) ts; (void) fromTime;   // debug variable names only
    set<int> changedSet;
    for (size_t k = begin; k < end; k++)
        changedSet.insert(labels[k].changed.begin(), labels[k].changed.end());
    vector<int> changed(changedSet.begin(), changedSet.end());
    vector<int> next = cur;
    vector<pair<int, int>> vars;
    for (int s : changed) {
        next[s] = sat->new_variable();
        DEBUG(sat->registerVariable(next[s], "sigmaM_" + to_string(ts) + "_" + to_string(labels[begin].label) +
                                    "_" + to_string(s) + "@" + to_string(fromTime)));
        vars.emplace_back(s, next[s]);
    }
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
    if (data->oneMove)
        oneMove(next, changed, labels[begin].label);
    cur = move(next);
}

vector<vector<int>> SigmaEncoding::extractIntermediateStates(vector<int> &selectedLabels,
                                                             vector<int> &currentLastState,
                                                             vector<int> & /*nextState*/,
                                                             int labelTimestep) {
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
// Plugin
// ---------------------------------------------------------------------------

static shared_ptr<SATEncodingFactory> _parse(options::OptionParser &parser) {
    parser.document_synopsis("Intermediate-state encoding over an FTS",
                             "R2E step semantics (labels in label order, several moves per factor "
                             "and step) with one variable per factor state changed by a label "
                             "(Symbolic Pattern Planning's sigma); see sat/sigma_encoding.h.");
    parser.add_option<shared_ptr<label_order_finder::LabelOrderFinder>>(
        "label_order", "label order of the step semantics", "label_order_linear()");
    parser.add_option<bool>("one_move",
                            "exists-step (lax): at most one real move per factor and step", "false");
    parser.add_option<bool>("strict",
                            "with one_move: every firing label is applicable at the start of the step",
                            "false");
    parser.add_option<int>("group_size",
                           "consecutive labels of the label order per micro-step (1: every label "
                           "its own micro-step; >1: forall-step inside a micro-step)", "1");
    parser.add_option<bool>("competing_groups",
                            "micro-step groups chosen greedily along the label order: a label joins the "
                            "current group if its changed states overlap the group's in some factor and it "
                            "starts in no factor from a state a group label moves to (overrides group_size)",
                            "false");
    parser.add_option<bool>("group_order",
                            "reorder the labels to put competing labels next to each other and use the "
                            "resulting clusters as micro-step groups (see compute_group_order; label_order "
                            "is the base order; overrides group_size and competing_groups)", "false");
    parser.add_option<bool>("factor_groups",
                            "per factor, blocks of consecutive labels (in the factor's own sequence) without "
                            "self-loops there that do not chain form one forall micro-step (same semantics "
                            "as the plain encoding; overrides the other grouping options)", "false");
    parser.add_option<bool>("factor_groups_exact",
                            "factor_groups: a label joins the factor's block if no earlier block label moves "
                            "into a state where it is applicable and no earlier block label self-loops at one "
                            "of its real sources (exactly the condition for no loss of parallelism)", "false");
    parser.add_option<bool>("group_allow_chains",
                            "group_order: also group labels that chain in a factor (that chain then needs "
                            "two steps)", "false");
    parser.add_option<bool>("group_keep_enablers",
                            "group_order: do not move a label before a label that moves into one of its "
                            "sources", "true");
    parser.add_option<int>("group_max", "competing_groups: at most this many labels per group", "16");
    parser.add_option<bool>("group_compact",
                            "groups: compact forall encoding (one-hot end state, effect and frame clauses, "
                            "pairwise exclusion only for labels sharing a real transition)", "false");
    parser.add_option<bool>("group_pure_amo",
                            "group_compact: at most one firing label among a group's labels without "
                            "self-loops in the factor, directly on the label variables", "true");
    parser.add_option<bool>("group_matrix",
                            "group_size > 1: label_sat's forall conditions inside a group (every firing "
                            "label has the group's state pair as a transition; pairwise chain clauses) "
                            "instead of mover variables", "true");
    parser.add_option<bool>("force_at_least_one_action",
                            "force that every time step contains at least one action", "false");
    static_constraints::add_options(parser);
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<SigmaEncodingFactory>(opts);
}

static options::PluginShared<SATEncodingFactory> _plugin("sigma_sat", _parse);
}
