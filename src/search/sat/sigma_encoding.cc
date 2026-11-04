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
      commuteOrder(opts.get<int>("commute_order")),
      reportOrder(opts.get<bool>("report_order")),
      groupRelax(opts.get<int>("group_relax")),
      staticOptions(opts) {
    clauseOptions.style = opts.get<int>("sigma_style");
    clauseOptions.rows = opts.get<bool>("use_empty_rows");
    clauseOptions.cols = opts.get<bool>("use_empty_cols");
    clauseOptions.pillars = opts.get<bool>("use_empty_pillars");
    clauseOptions.onesLast = opts.get<bool>("use_ones_in_last_dimension");
    clauseOptions.positiveOne = opts.get<bool>("use_positive_one");
    clauseOptions.threshold = opts.get<int>("one_encoding_threshold");
    clauseOptions.thresholdPercent = opts.get<int>("one_encoding_threshold_percent");
    clauseOptions.layerAlo = opts.get<bool>("layer_alo");
    clauseOptions.layerAmo = opts.get<bool>("layer_amo");
    clauseOptions.shareLast = opts.get<bool>("share_last_layer");
    clauseOptions.frameReverse = opts.get<bool>("frame_reverse");
    clauseOptions.justification = opts.get<bool>("justification");
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

/*
  commute_order: reorder the labels along the "feeds" DAG. Label a feeds b (a before b in the order)
  if in some factor a's real target is a state where b is applicable, or a self-loops at a real
  source of b -- exactly the situations in which a and then b can fire in one step with a's
  presence mattering. If a does not feed b, the two can fire together in that order only as
  self-loops at the same state in every shared factor, so they commute: every linearisation of the
  DAG keeps all one-step label sequences of the original order (and may add some), so horizons do
  not grow. Mode 1: ASAP layering (level = 1 + max level of the labels feeding it), ties by the
  original position; mode 2: ALAP layering (as late as possible). Non-feeding labels then sit
  next to each other, which lets the exact per-factor blocks grow.
*/
void SigmaEncodingFactory::commute_labels(SigmaData &d) const {
    const int numLabels = fts->get_num_labels();
    vector<int> pos(numLabels);
    for (size_t k = 0; k < d.order.size(); k++)
        pos[d.order[k]] = k;
    struct Role { int ts; vector<int> app, src, tgt, sl; };
    vector<vector<Role>> roles(numLabels);
    for (int ts = 0; ts < fts->get_size(); ts++)
        for (const SigmaLabelData &ld : d.factors[ts]) {
            Role r{ts, ld.applicable, {}, {}, {}};
            set<int> src, tgt, sl;
            for (const auto &[x, ys] : ld.targets)
                for (int y : ys) {
                    if (y == x) sl.insert(x);
                    else { src.insert(x); tgt.insert(y); }
                }
            r.src.assign(src.begin(), src.end());
            r.tgt.assign(tgt.begin(), tgt.end());
            r.sl.assign(sl.begin(), sl.end());
            roles[ld.label].push_back(move(r));
        }
    if (commuteOrder == 3) {
        // per factor and state: labels with a real target there / a self-loop there
        vector<vector<vector<int>>> tgtL(fts->get_size()), slL(fts->get_size());
        for (int ts = 0; ts < fts->get_size(); ts++) {
            tgtL[ts].assign(fts->get_ts(ts).get_size(), {});
            slL[ts].assign(fts->get_ts(ts).get_size(), {});
        }
        for (int l = 0; l < numLabels; l++)
            for (const Role &r : roles[l]) {
                for (int x : r.tgt) tgtL[r.ts][x].push_back(l);
                for (int x : r.sl) slL[r.ts][x].push_back(l);
            }
        auto mark_feeders = [&](int u, vector<int> &stamp) {
            for (const Role &r : roles[u]) {
                for (int x : r.app) for (int w : tgtL[r.ts][x]) stamp[w] = u;
                for (int x : r.src) for (int w : slL[r.ts][x]) stamp[w] = u;
            }
        };
        auto forward_pairs = [&](const vector<int> &ord) {
            vector<int> p(numLabels), stamp(numLabels, -1);
            for (size_t k = 0; k < ord.size(); k++) p[ord[k]] = k;
            long fwd = 0, bwd = 0;
            for (int u : ord) {
                mark_feeders(u, stamp);
                for (int w = 0; w < numLabels; w++)
                    if (stamp[w] == u && w != u) (p[w] < p[u] ? fwd : bwd)++;
            }
            return make_pair(fwd, bwd);
        };
        const bool count = numLabels <= 5000;   // the count is quadratic
        pair<long, long> before = count ? forward_pairs(d.order) : make_pair(-1L, -1L);
        // left shift: a label moves before the earliest label it feeds among those it can pass (no
        // label between feeds it), right shift symmetrically after the latest label feeding it
        // among those it can pass. A label passes only labels it is not ordered against, so no
        // forward feeding pair is lost, and every move gains one: the passes reach a fixpoint.
        vector<vector<vector<int>>> appL(fts->get_size()), srcL(fts->get_size());
        for (int ts = 0; ts < fts->get_size(); ts++) {
            appL[ts].assign(fts->get_ts(ts).get_size(), {});
            srcL[ts].assign(fts->get_ts(ts).get_size(), {});
        }
        for (int l = 0; l < numLabels; l++)
            for (const Role &r : roles[l]) {
                for (int x : r.app) appL[r.ts][x].push_back(l);
                for (int x : r.src) srcL[r.ts][x].push_back(l);
            }
        vector<int> order = d.order, feeder(numLabels, -1), fed(numLabels, -1), at(numLabels);
        for (size_t k = 0; k < order.size(); k++) at[order[k]] = k;
        // deduplicated feeder / fed lists, built once; the passes stop after a deterministic amount
        // of work (list entries visited)
        vector<vector<int>> feedersOf(numLabels), fedBy(numLabels);
        long work = 0;
        for (int u = 0; u < numLabels; u++) {
            for (const Role &r : roles[u]) {
                for (int x : r.app) for (int w : tgtL[r.ts][x]) if (w != u && feeder[w] != u) { feeder[w] = u; feedersOf[u].push_back(w); }
                for (int x : r.src) for (int w : slL[r.ts][x]) if (w != u && feeder[w] != u) { feeder[w] = u; feedersOf[u].push_back(w); }
                for (int x : r.tgt) for (int w : appL[r.ts][x]) if (w != u && fed[w] != u) { fed[w] = u; fedBy[u].push_back(w); }
                for (int x : r.sl) for (int w : srcL[r.ts][x]) if (w != u && fed[w] != u) { fed[w] = u; fedBy[u].push_back(w); }
            }
            work += feedersOf[u].size() + fedBy[u].size();
        }
        const long workPerPass = work, workLimit = 200000000L;
        long workDone = 0;
        auto move_to = [&](int i, int target) {
            const int u = order[i];
            order.erase(order.begin() + i);
            order.insert(order.begin() + target, u);
            for (int k = min(i, target); k <= max(i, target); k++) at[order[k]] = k;
        };
        size_t moved = 0, rounds = 0;
        for (bool change = true; change && rounds < 50 && workDone + 2 * workPerPass <= workLimit; rounds++) {
            change = false;
            workDone += 2 * workPerPass;
            for (int i = 0; i < (int) order.size(); i++) {   // left: before the earliest fed label after the last feeder
                const int u = order[i];
                const vector<int> &feeders = feedersOf[u], &feds = fedBy[u];
                int lo = -1, target = i;
                for (int w : feeders) if (at[w] < i) lo = max(lo, at[w]);
                for (int w : feds) if (at[w] > lo && at[w] < target) target = at[w];
                if (target < i) { move_to(i, target); moved++; change = true; }
            }
            for (int i = (int) order.size() - 1; i >= 0; i--) {   // right: after the latest feeder before the first fed label
                const int u = order[i];
                const vector<int> &feeders = feedersOf[u], &feds = fedBy[u];
                int hi = order.size(), target = i;
                for (int w : feds) if (at[w] > i) hi = min(hi, at[w]);
                for (int w : feeders) if (at[w] < hi && at[w] > target) target = at[w];
                if (target > i) { move_to(i, target); moved++; change = true; }
            }
        }
        pair<long, long> after = count ? forward_pairs(order) : make_pair(-1L, -1L);
        d.order = order;
        vector<int> newPos(numLabels);
        for (size_t k = 0; k < order.size(); k++) newPos[order[k]] = k;
        for (auto &labels : d.factors)
            stable_sort(labels.begin(), labels.end(), [&](const SigmaLabelData &a, const SigmaLabelData &b) {
                return newPos[a.label] < newPos[b.label]; });
        cout << "SIGMACOMMUTE mode 3 moved " << moved << " rounds " << rounds << " edges " << workPerPass << " forward_pairs " << before.first << " -> " << after.first
             << " backward_pairs " << before.second << " -> " << after.second << endl;
        return;
    }
    vector<int> level(numLabels, 0);
    vector<int> seq = d.order;
    if (commuteOrder == 2) reverse(seq.begin(), seq.end());
    // per factor and state: the highest level of an already placed label in that role
    vector<vector<int>> maxTgt(fts->get_size()), maxSl(fts->get_size()),
        maxApp(fts->get_size()), maxSrc(fts->get_size());
    for (int ts = 0; ts < fts->get_size(); ts++) {
        const int n = fts->get_ts(ts).get_size();
        maxTgt[ts].assign(n, 0); maxSl[ts].assign(n, 0); maxApp[ts].assign(n, 0); maxSrc[ts].assign(n, 0);
    }
    size_t edgesBound = 0;
    for (int b : seq) {
        int lv = 0;
        for (const Role &r : roles[b]) {
            if (commuteOrder == 1) {   // earlier a feeds b
                for (int x : r.app) lv = max(lv, maxTgt[r.ts][x]);
                for (int x : r.src) lv = max(lv, maxSl[r.ts][x]);
            } else {                   // reversed: b feeds the later a
                for (int x : r.tgt) lv = max(lv, maxApp[r.ts][x]);
                for (int x : r.sl) lv = max(lv, maxSrc[r.ts][x]);
            }
        }
        level[b] = lv + 1;
        edgesBound = max(edgesBound, (size_t) lv + 1);
        for (const Role &r : roles[b]) {
            for (int x : r.tgt) maxTgt[r.ts][x] = max(maxTgt[r.ts][x], lv + 1);
            for (int x : r.sl) maxSl[r.ts][x] = max(maxSl[r.ts][x], lv + 1);
            for (int x : r.app) maxApp[r.ts][x] = max(maxApp[r.ts][x], lv + 1);
            for (int x : r.src) maxSrc[r.ts][x] = max(maxSrc[r.ts][x], lv + 1);
        }
    }
    vector<int> order = d.order;
    if (commuteOrder == 1)
        stable_sort(order.begin(), order.end(), [&](int a, int b) { return level[a] < level[b]; });
    else
        stable_sort(order.begin(), order.end(), [&](int a, int b) { return level[a] > level[b]; });
    d.order = order;
    vector<int> newPos(numLabels);
    for (size_t k = 0; k < order.size(); k++) newPos[order[k]] = k;
    for (auto &labels : d.factors)
        stable_sort(labels.begin(), labels.end(), [&](const SigmaLabelData &a, const SigmaLabelData &b) {
            return newPos[a.label] < newPos[b.label]; });
    size_t moved = 0;
    for (int l = 0; l < numLabels; l++) moved += newPos[l] != pos[l];
    cout << "SIGMACOMMUTE mode " << commuteOrder << " levels " << edgesBound << " labels " << order.size()
         << " moved " << moved << endl;
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
    d->clauses = clauseOptions;
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
    if (commuteOrder)
        commute_labels(*d);
    if (factorGroups) {
        // per factor, along the factor's labels: a block of labels without self-loops there that do
        // not chain (no label starts where another of the block ends); such labels can never fire
        // one after another in a step, so a forall-step block loses no parallelism
        d->factorBlock.resize(fts->get_size());
        size_t blocks = 0, entries = 0;
        const set<pair<int, int>> hints(labelOrderFinder->chain_pairs.begin(), labelOrderFinder->chain_pairs.end());
        for (int ts = 0; ts < fts->get_size(); ts++) {
            const vector<SigmaLabelData> &ls = d->factors[ts];
            vector<int> &blk = d->factorBlock[ts];
            blk.assign(ls.size(), 0);
            int b = -1, size = 0;
            set<int> bSrc, bTgt, bSl;
            vector<size_t> members;   // group_relax=2: entries of the current block
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
                    if (join && groupRelax == 2) {
                        // only a feeding pair the goal chains use separates blocks
                        for (size_t m : members) {
                            const SigmaLabelData &la = ls[m];
                            if (!hints.count({la.label, ld.label})) continue;
                            bool feeds = false;
                            for (const auto &[x, ys] : la.targets)
                                for (int y : ys) {
                                    if (y != x && binary_search(ld.applicable.begin(), ld.applicable.end(), y)) feeds = true;
                                    if (y == x && src.count(x)) feeds = true;
                                }
                            if (feeds) join = false;
                        }
                    } else if (join) {
                        for (int x : ld.applicable) if (bTgt.count(x)) join = false;
                        if (groupRelax != 1)
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
                    b++; size = 0; bSrc.clear(); bTgt.clear(); bSl.clear(); members.clear();
                    blocks++;
                }
                blk[k] = b;
                size++;
                members.push_back(k);
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
    if (reportOrder) {
        cout << "SIGMAORDER";
        for (int l : d->order) cout << " " << l;
        cout << endl;
        cout << "SIGMAHINTS";
        for (auto [a, b] : labelOrderFinder->chain_pairs) cout << " " << a << ">" << b;
        cout << endl;
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
        // segments: a single label or a group of this factor's labels forming one micro-step
        vector<pair<size_t, size_t>> segments;
        for (size_t i = 0; i < labels.size();) {
            size_t j = i + 1;
            if (!data->factorBlock.empty()) {
                const vector<int> &blk = data->factorBlock[ts];
                while (j < labels.size() && blk[j] == blk[i])
                    j++;
            } else {
                while (j < labels.size() && data->groupOf[labels[j].label] == data->groupOf[labels[i].label])
                    j++;
            }
            segments.emplace_back(i, j);
            i = j;
        }
        // share_last_layer: the last segment changing a state creates no new variable for it but
        // uses the state variable of time t+1
        vector<int> lastSegment(cur.size(), -1);
        if (data->clauses.shareLast)
            for (size_t g = 0; g < segments.size(); g++)
                for (size_t k = segments[g].first; k < segments[g].second; k++)
                    for (int x : labels[k].changed)
                        lastSegment[x] = g;
        size_t segment = 0;
        freshStateVar = [&](int x) {
            if (lastSegment[x] == (int) segment)
                return nextStateVars[ts][x];
            return sat->new_variable();
        };
        curNextStateVars = &nextStateVars;
        const bool projection = data->clauses.style == 1;
        for (segment = 0; segment < segments.size(); segment++) {
            const size_t i = segments[segment].first, j = segments[segment].second;
            // preconditions (all labels: applicable at the group's start state; PROJECTION encodes
            // a single label's precondition itself)
            for (size_t k = i; k < j; k++) {
                const SigmaLabelData &ld = labels[k];
                const int l = labelVars[ld.label];
                vector<int> pre;
                for (int x : ld.applicable)
                    pre.push_back(cur[x]);
                if (!(projection && j == i + 1))
                    sat->impliesOr(l, pre);
                if (data->strict) {
                    vector<int> start;
                    for (int x : ld.applicable)
                        start.push_back(previousStateVars[ts][x]);
                    sat->impliesOr(l, start);
                }
            }
            if (j == i + 1 && projection) {
                encode_single_projection(ts, labels[i], labelVars, cur, fromTime, updates[ts], oneMoveClauses);
            } else if (j == i + 1) {
                encode_single(ts, labels[i], labelVars, cur, fromTime, updates[ts], oneMoveClauses);
            } else if (data->groupCompact) {
                encode_group_compact(ts, labels, i, j, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
            } else if (data->groupMatrix) {
                encode_group_matrix(ts, labels, i, j, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
            } else {
                encode_group(ts, labels, i, j, labelVars, cur, fromTime, updates[ts], oneMoveClauses);
            }
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
        next[s] = freshStateVar(s);
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
    layer_one_hot(ts, next, ld.changed);
    if (data->oneMove)
        oneMove(next, ld.changed, ld.label);
    updates.push_back(move(upd));
    cur = move(next);
}

bool SigmaEncoding::below_threshold(size_t ones, size_t numStates) const {
    if ((int) ones <= data->clauses.threshold) return true;
    return (int) ones * 100 <= (int) numStates * data->clauses.thresholdPercent;
}

/*
  layer_alo / layer_amo: exactly-one on a new layer. The unchanged states share their variables
  with the previous layer, so at-most-one only needs the pairs involving a changed state; a layer
  that consists of the t+1 state variables already has exactly-one.
*/
void SigmaEncoding::layer_one_hot(int ts, const vector<int> &next, const vector<int> &changed) {
    if (changed.empty()) return;
    const vector<int> &stateVars = (*curNextStateVars)[ts];
    auto isStateVar = [&](size_t s) { return next[s] == stateVars[s]; };
    if (data->clauses.layerAlo) {
        bool all = true;
        for (size_t s = 0; s < next.size(); s++) all = all && isStateVar(s);
        if (!all) sat->atLeastOne(next);
    }
    if (data->clauses.layerAmo) {
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
void SigmaEncoding::encode_single_projection(int ts, const SigmaLabelData &ld, const vector<int> &labelVars,
                                             vector<int> &cur, int fromTime, vector<SigmaUpdate> &updates,
                                             const OneMove &oneMove) {
    (void) fromTime;
    const SigmaClauses &c = data->clauses;
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
    if (c.rows || ld.changed.empty()) {
        if (c.positiveOne && below_threshold(ld.applicable.size(), N)) {
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
    vector<int> next = cur;
    SigmaUpdate upd{ld.label, {}, 0, -1};
    for (int m : ld.changed) {
        next[m] = freshStateVar(m);
        DEBUG(sat->registerVariable(next[m], "sigma_" + to_string(ts) + "_" + to_string(ld.label) +
                                    "_" + to_string(m) + "@" + to_string(fromTime)));
        upd.vars.emplace_back(m, next[m]);
        sat->andImpliesOr(vector<int>{-l, cur[m]}, vector<int>{next[m]});
        if (c.frameReverse || !c.layerAmo)
            sat->andImpliesOr(vector<int>{-l, next[m]}, vector<int>{cur[m]});
    }
    // cols: which states l can end in
    if (c.cols) {
        vector<int> tg;
        for (int j = 0; j < N; j++) if (inTgt[j]) tg.push_back(j);
        if (c.positiveOne && below_threshold(tg.size(), N)) {
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
    if (c.pillars) {
        if (c.positiveOne) {
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
        const bool dual = c.layerAmo && c.onesLast && below_threshold(T[i].size(), N);
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
        next[s] = freshStateVar(s);
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
        next[s] = freshStateVar(s);
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
        next[s] = freshStateVar(s);
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
    parser.add_option<int>("commute_order",
                           "reorder the labels along the DAG of labels that feed each other (see "
                           "commute_labels; no one-step sequence is lost): 0 off, 1 ASAP layering, "
                           "2 ALAP layering, 3 left shift (every label right after its last feeder: turns "
                           "backward feeding pairs into forward ones)", "0");
    parser.add_option<bool>("report_order", "print the final label order (SIGMAORDER line)", "false");
    parser.add_option<int>("group_relax",
                           "factor_groups_exact, lossy: 0 exact; 1 a label may join after one that "
                           "self-loops at its real source; 2 only a feeding pair that the goal chains "
                           "use (label_order's chain pairs) separates blocks", "0");
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
    parser.add_option<int>("sigma_style",
                           "clauses of a single-label micro-step: 0 SUPPORT (effect / justification "
                           "clauses), 1 PROJECTION (label_sat's primal/dual projection encoding of the "
                           "micro-step matrix; see sigma_encoding.h)", "0");
    parser.add_option<bool>("use_empty_rows", "PROJECTION: label-source projection", "true");
    parser.add_option<bool>("use_empty_cols", "PROJECTION: label-target projection", "true");
    parser.add_option<bool>("use_empty_pillars",
                            "PROJECTION: source-target projection (unguarded by the label)", "true");
    parser.add_option<bool>("use_ones_in_last_dimension",
                            "PROJECTION: residual (label, source) -> target in dual form if the targets "
                            "are below the threshold (needs layer_amo)", "true");
    parser.add_option<bool>("use_positive_one",
                            "PROJECTION: projections in dual form if the ones are below the threshold",
                            "true");
    parser.add_option<int>("one_encoding_threshold",
                           "PROJECTION: dual form if the number of ones is at most this", "1");
    parser.add_option<int>("one_encoding_threshold_percent",
                           "PROJECTION: dual form if the ones are at most this percentage of the states", "0");
    parser.add_option<bool>("layer_alo", "at least one state per intermediate layer", "false");
    parser.add_option<bool>("layer_amo",
                            "at most one state per intermediate layer (pairs involving a new variable)",
                            "false");
    parser.add_option<bool>("share_last_layer",
                            "the last change of a state in a step uses the state variable of t+1 "
                            "instead of a new variable and equivalence clauses", "true");
    parser.add_option<bool>("frame_reverse",
                            "PROJECTION: also -l & n_s -> p_s (always added without layer_amo)", "false");
    parser.add_option<bool>("justification",
                            "PROJECTION: also l & n_y -> OR p_x over the sources x of y", "false");
    static_constraints::add_options(parser);
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<SigmaEncodingFactory>(opts);
}

static options::PluginShared<SATEncodingFactory> _plugin("sigma_sat", _parse);
}
