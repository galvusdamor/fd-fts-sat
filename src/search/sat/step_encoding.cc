/*
  StepEncoding: factory, options of the three configurations (label_sat, sigma_sat,
  micro_steps_sat), the step's layers of state variables, and the dispatch to the segment
  encoders (step_encoding_forall.cc, step_encoding_segments.cc). See step_encoding.h.
*/
#include "step_encoding.h"

#include "fts_matrix.h"
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
#include <set>

using namespace std;
using namespace task_representation;

namespace sat_search {

shared_ptr<const LabelExistsData> compute_exists_data(const FTSTask & fts, const vector<int> & order);

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

StepEncodingFactory::StepEncodingFactory(const options::Options &opts, StepPreset preset)
    : SATEncodingFactory(opts.get<bool>("force_at_least_one_action")) {
    opt.preset = preset;
    if (preset == StepPreset::LABEL_SAT) {
        opt.useLabelGroups = opts.get<bool>("use_label_group");
        opt.useSelfloopOptimisation = opts.get<bool>("use_self_loop_optimisation");
        opt.useEmptyRows = opts.get<bool>("use_empty_rows");
        opt.useEmptyCols = opts.get<bool>("use_empty_cols");
        opt.useEmptyPillars = opts.get<bool>("use_empty_pillars");
        opt.useOnesInLastDimension = opts.get<bool>("use_ones_in_last_dimension");
        opt.usePositiveOneForEmpty = opts.get<bool>("use_positive_one");
        opt.oneEncodingThreshold = size_t(opts.get<int>("one_encoding_threshold"));
        opt.oneEncodingThresholdPercent = opts.get<int>("one_encoding_threshold_percent");
        opt.encoding = encoding_type(opts.get_enum("encoding"));
        opt.chainsInterleaved = opts.get<bool>("chains_interleaved");
        opt.existsStep = opts.get<bool>("exists_step");
        opt.existsLax = opts.get<bool>("exists_lax");
        opt.existsPersist = opts.get<bool>("exists_effects_persist");
        opt.existsOnlyChains = opts.get<bool>("exists_only_chains");
        opt.existsPureEvents = opts.get<bool>("exists_pure_events");
        labelOrderFinder = opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("label_order");
        if (opt.encoding != SEQUENTIAL && opt.useSelfloopOptimisation == false){
            cerr << "Parallel label-based encodings may only be used together with the self-loop optimisation" << endl;
            assert(false);
        }
        if (opt.existsStep && (opt.encoding != CHAINS_PARALLEL || opt.useLabelGroups)){
            cerr << "exists_step needs encoding=CHAINS_PARALLEL and use_label_group=false" << endl;
            utils::exit_with(utils::ExitCode::INPUT_ERROR);
        }
    } else if (preset == StepPreset::SIGMA) {
        labelOrderFinder = opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("label_order");
        opt.oneMove = opts.get<bool>("one_move");
        opt.strict = opts.get<bool>("strict");
        opt.groupSize = opts.get<int>("group_size");
        opt.groupMatrix = opts.get<bool>("group_matrix");
        opt.groupCompact = opts.get<bool>("group_compact");
        opt.groupPureAmo = opts.get<bool>("group_pure_amo");
        opt.competingGroups = opts.get<bool>("competing_groups");
        opt.groupOrder = opts.get<bool>("group_order");
        opt.factorGroups = opts.get<bool>("factor_groups");
        opt.factorGroupsExact = opts.get<bool>("factor_groups_exact");
        opt.groupAllowChains = opts.get<bool>("group_allow_chains");
        opt.groupKeepEnablers = opts.get<bool>("group_keep_enablers");
        opt.groupMax = opts.get<int>("group_max");
        opt.commuteOrder = opts.get<int>("commute_order");
        opt.reportOrder = opts.get<bool>("report_order");
        opt.groupRelax = opts.get<int>("group_relax");
        opt.clauses.style = opts.get<int>("sigma_style");
        opt.useEmptyRows = opts.get<bool>("use_empty_rows");
        opt.useEmptyCols = opts.get<bool>("use_empty_cols");
        opt.useEmptyPillars = opts.get<bool>("use_empty_pillars");
        opt.useOnesInLastDimension = opts.get<bool>("use_ones_in_last_dimension");
        opt.usePositiveOneForEmpty = opts.get<bool>("use_positive_one");
        opt.oneEncodingThreshold = size_t(opts.get<int>("one_encoding_threshold"));
        opt.oneEncodingThresholdPercent = opts.get<int>("one_encoding_threshold_percent");
        opt.clauses.layerAlo = opts.get<bool>("layer_alo");
        opt.clauses.layerAmo = opts.get<bool>("layer_amo");
        opt.clauses.shareLast = opts.get<bool>("share_last_layer");
        opt.clauses.frameReverse = opts.get<bool>("frame_reverse");
        opt.clauses.justification = opts.get<bool>("justification");
        opt.clauses.rowsDualThreshold = opts.get<int>("rows_dual_threshold");
        opt.nextStateFromLayers = opts.get<bool>("next_state_from_layers");
        staticOptions = make_unique<static_constraints::Options>(opts);
        if (opt.strict && !opt.oneMove) {
            cerr << "sigma_sat: strict needs one_move" << endl;
            utils::exit_with(utils::ExitCode::INPUT_ERROR);
        }
        if (opt.clauses.style == 2 && opt.oneMove) {
            cerr << "sigma_sat: one_move is not implemented for sigma_style=2" << endl;
            utils::exit_with(utils::ExitCode::INPUT_ERROR);
        }
    } else {
        // micro_steps_sat: the reversed label order, one segment per label, ROWS clauses
        opt.clauses.style = 2;
        opt.clauses.rowsDualThreshold = opts.get<int>("dual_threshold");
        opt.fullLayers = !opts.get<bool>("use_self_loop_optimisation");
        opt.nextStateFromLayers = true;
    }
}

const vector<pair<int, int>> & StepEncodingFactory::labelOrderFinder_chain_pairs() const {
    static const vector<pair<int, int>> none;
    return labelOrderFinder ? labelOrderFinder->chain_pairs : none;
}

vector<int> StepEncodingFactory::segment_label_order() const {
    if (opt.preset == StepPreset::MICRO_STEPS) {
        // the reversed label order of the TAV and TAC encodings
        vector<int> order;
        for (int l = fts->get_num_labels() - 1; l >= 0; l--) order.push_back(l);
        return order;
    }
    return labelOrderFinder->find_order(*fts);
}

void StepEncodingFactory::initialize() {
    auto d = make_shared<StepData>();
    d->opt = opt;
    if (opt.preset == StepPreset::LABEL_SAT) {
        initialize_forall(*d);
    } else {
        if (opt.preset == StepPreset::MICRO_STEPS) {
            cout << "Initialising" << fts << endl;
            cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;
        }
        d->segments = true;
        initialize_segments(*d);
    }
    data = d;
}

void StepEncodingFactory::initialize_forall(StepData &d) {
    utils::Timer sat_init_timer;
    cout << "Initialising" << fts << endl;
    cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

    for (const auto & ts : fts->get_transition_systems()) {
        d.ftsMatrices.push_back(make_shared<FTSMatrix>(*ts));
    }
    if (opt.existsStep) {
        d.sigma.order = labelOrderFinder->find_order(*fts);
        d.existsData = compute_exists_data(*fts, d.sigma.order);
    }

    cout << "SAT init time: " << sat_init_timer << endl;
}

unique_ptr<SATEncoding> StepEncodingFactory::createEncodingInstance(shared_ptr<sat_capsule> capsule) {
    bool oldStatisticsPrinted = statisticsPrinted;
    statisticsPrinted = true;
    auto encoding = make_unique<StepEncoding>(capsule, fts, forceAtLeastOneAction, oldStatisticsPrinted, data);
    encoding->setStaticConstraints(staticConstraints);
    return encoding;
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
void StepEncodingFactory::compute_group_order(SigmaData &d) const {
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
            if (!cl.open || (int) cl.labels.size() >= opt.groupMax || o <= bestOverlap) continue;
            bool ok = true;
            for (const Moves &m : moves[label]) {
                auto cs = cl.src.find(m.ts), ct = cl.tgt.find(m.ts);
                if (!opt.groupAllowChains) {
                    for (int x : m.src) if (ct != cl.tgt.end() && ct->second.count(x)) ok = false;   // cluster -> label
                    for (int y : m.tgt) if (cs != cl.src.end() && cs->second.count(y)) ok = false;   // label -> cluster
                }
                // an enabler of the label between the cluster's first member and the label
                if (opt.groupKeepEnablers) for (int x : m.src) {
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
void StepEncodingFactory::commute_labels(SigmaData &d) const {
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
    if (opt.commuteOrder == 3) {
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
    if (opt.commuteOrder == 2) reverse(seq.begin(), seq.end());
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
            if (opt.commuteOrder == 1) {   // earlier a feeds b
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
    if (opt.commuteOrder == 1)
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
    cout << "SIGMACOMMUTE mode " << opt.commuteOrder << " levels " << edgesBound << " labels " << order.size()
         << " moved " << moved << endl;
}

void StepEncodingFactory::initialize_segments(StepData &sd) {
    utils::Timer timer;
    SigmaData *d = &sd.sigma;
    d->oneMove = opt.oneMove;
    d->strict = opt.strict;
    d->order = segment_label_order();
    d->groupMatrix = opt.groupMatrix;
    d->groupCompact = opt.groupCompact;
    d->groupPureAmo = opt.groupPureAmo;
    d->clauses = opt.clauses;
    d->fullLayers = opt.fullLayers;
    d->groupOf.assign(fts->get_num_labels(), 0);
    if (!opt.competingGroups) {
        for (size_t k = 0; k < d->order.size(); k++)
            d->groupOf[d->order[k]] = k / max(opt.groupSize, 1);
    }
    d->factors.resize(fts->get_size());
    size_t changedVars = 0, labelsInFactors = 0, nondet = 0;
    for (int ts = 0; ts < fts->get_size(); ts++) {
        const TransitionSystem &tss = fts->get_ts(ts);
        for (int label : d->order) {
            if (tss.isIrrelevantLabel(label) && !opt.fullLayers)
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
    if (opt.commuteOrder)
        commute_labels(*d);
    if (opt.factorGroups) {
        // per factor, along the factor's labels: a block of labels without self-loops there that do
        // not chain (no label starts where another of the block ends); such labels can never fire
        // one after another in a step, so a forall-step block loses no parallelism
        d->factorBlock.resize(fts->get_size());
        size_t blocks = 0, entries = 0;
        const set<pair<int, int>> hints(labelOrderFinder_chain_pairs().begin(), labelOrderFinder_chain_pairs().end());
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
                if (opt.factorGroupsExact) {
                    // exact: for a before b in the block, a's real targets are not where b is
                    // applicable, and a's self-loops are not b's real sources
                    join = size > 0 && size < opt.groupMax;
                    if (join && opt.groupRelax == 2) {
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
                        if (opt.groupRelax != 1)
                            for (int x : src) if (bSl.count(x)) join = false;
                    }
                } else {
                    const bool mover = !selfLoop && !src.empty();
                    join = open && mover && size < opt.groupMax;
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
    } else if (opt.groupOrder) {
        compute_group_order(*d);
    } else if (opt.competingGroups) {
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
            if (size > 0 && (!overlap || chain || size >= opt.groupMax)) {
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
    if (opt.preset == StepPreset::MICRO_STEPS) {
        // micro_steps_sat's output
        long long layersWithNewVars = 0, sharedLayerVars = 0, fullLayerVars = 0;
        for (int ts = 0; ts < fts->get_size(); ts++) {
            for (const SigmaLabelData &ld : d->factors[ts]) {
                size_t newVars = opt.fullLayers ? fts->get_ts(ts).get_size() : ld.changed.size();
                if (newVars) layersWithNewVars++;
                sharedLayerVars += newVars;
            }
            fullLayerVars += (long long)fts->get_num_labels() * fts->get_ts(ts).get_size();
        }
        cout << "Micro-steps: dual threshold " << opt.clauses.rowsDualThreshold
             << ", variable sharing " << (opt.fullLayers ? "off" : "on") << endl;
        cout << "Micro-steps: layer variables per time step " << sharedLayerVars
             << " (without sharing: " << fullLayerVars << "), layers with new variables "
             << layersWithNewVars << endl;
        cout << "SAT init time: " << timer << endl;
        return;
    }
    if (opt.reportOrder) {
        cout << "SIGMAORDER";
        for (int l : d->order) cout << " " << l;
        cout << endl;
        cout << "SIGMAHINTS";
        for (auto [a, b] : labelOrderFinder_chain_pairs()) cout << " " << a << ">" << b;
        cout << endl;
    }
    if (staticOptions)
        staticConstraints = static_constraints::compute(*fts, *staticOptions);
    cout << "SIGMASTAT sigma_vars_per_step " << changedVars << " label_factor_pairs " << labelsInFactors
         << " nondeterministic_sources " << nondet << " time " << timer() << endl;
    cout << "SAT init time: " << timer << endl;
}


// ---------------------------------------------------------------------------
// Encoding: layers and dispatch
// ---------------------------------------------------------------------------

StepEncoding::StepEncoding(shared_ptr<sat_capsule> capsule, const shared_ptr<FTSTask> &fts,
                           bool forceAtLeastOneAction, bool _statisticsPrinted, shared_ptr<const StepData> _data)
    : CommonEncoding(capsule, fts, forceAtLeastOneAction,
                     _data->segments ? false : _data->opt.useLabelGroups,
                     _data->segments ? _data->opt.preset == StepPreset::SIGMA || !_data->opt.fullLayers
                                     : _data->opt.useSelfloopOptimisation),
      data(_data), opt(_data->opt), statisticsPrinted(_statisticsPrinted),
      fts_matrices(_data->ftsMatrices), existsData(_data->existsData),
      existsLax(_data->existsData && opt.existsLax && !opt.existsOnlyChains),
      existsWeaken(_data->existsData && !opt.existsOnlyChains),
      existsPersist(_data->existsData && opt.existsPersist),
      existsPureEvents(opt.existsPureEvents),
      sigma(_data->sigma) {
    // plan extraction executes the labels of a step in this order (label_sat: only exists_step
    // has an order; otherwise the labels are taken by id)
    if (data->segments || existsData)
        labelOrder = sigma.order;
}

/*
  The layers of a step. Up front (label_sat, sigma_sat by default): the t+1 state variables are
  generated with the step, and CommonEncoding::encode runs the hooks below. From the layers
  (micro_steps_sat, next_state_from_layers): every factor's segments are encoded from the time t
  variables on, and their last layer becomes the t+1 state variables -- a state no label of the
  step moves keeps its variable.
*/
void StepEncoding::encode(int fromTime, int toTime) {
    rowsStats = RowsStatistics();
    if (!(data->segments && opt.nextStateFromLayers)) {
        CommonEncoding::encode(fromTime, toTime);
    } else {
        auto preStateVarFind = allTimesStateVars.find(fromTime);
        const vector<vector<int>> &previousStateVars = (preStateVarFind == allTimesStateVars.end()) ?
            (allTimesStateVars[fromTime] = generateStateVars()) :
            preStateVarFind->second;

        assert(!allTimesLabelVars.contains(fromTime));
        allTimesLabelVars[fromTime] = generateLabelVars();
        if (forceAtLeastOneAction)
            sat->atLeastOne(allTimesLabelVars[fromTime]);

        allTimesUpdates[fromTime].assign(fts->get_size(), {});
        vector<vector<int>> lastLayers(fts->get_size());
        for (int ts = 0; ts < fts->get_size(); ts++)
            lastLayers[ts] = encode_factor_segments(ts, previousStateVars[ts], nullptr, fromTime);

        auto nextStateVarFind = allTimesStateVars.find(toTime);
        if (nextStateVarFind == allTimesStateVars.end()) {
            // the last layer of each factor is the state of time step toTime
            allTimesStateVars[toTime] = std::move(lastLayers);
        } else {
            // The state variables of toTime already exist (e.g., the goal side of
            // the incremental search). Link them to the last layer.
            const vector<vector<int>> &nextStateVars = nextStateVarFind->second;
            for (int ts = 0; ts < fts->get_size(); ts++) {
                for (size_t s = 0; s < nextStateVars[ts].size(); s++) {
                    if (lastLayers[ts][s] == nextStateVars[ts][s]) continue;
                    sat->implies(lastLayers[ts][s], nextStateVars[ts][s]);
                    sat->implies(nextStateVars[ts][s], lastLayers[ts][s]);
                    rowsStats.links += 2;
                }
            }
        }
        if (staticConstraints && !staticConstraints->empty())
            static_constraints::encode(*sat, *staticConstraints, allTimesStateVars[toTime], allTimesLabelVars[fromTime]);
    }
    if (data->segments && sigma.clauses.style == 2 && !statisticsPrinted) {
        statisticsPrinted = true;
        rowsStats.print();
    }
}

void StepEncoding::generateAdditionalVariables(int fromTime) {
    if (!data->segments)
        forall_generate_additional_variables(fromTime);
}

void StepEncoding::encode_frame_axioms(const vector<vector<int>> &previousStateVars,
                                       const vector<vector<int>> &nextStateVars, int fromTime) {
    // segments: a label that does not fire keeps the factor's state (frame clauses per segment)
    if (!data->segments)
        forall_encode_frame_axioms(previousStateVars, nextStateVars, fromTime);
}

void StepEncoding::encode_transition(const vector<vector<int>> &previousStateVars,
                                     const vector<vector<int>> &nextStateVars, int fromTime) {
    if (data->segments)
        segments_encode_transition(previousStateVars, nextStateVars, fromTime);
    else
        forall_encode_transition(previousStateVars, nextStateVars, fromTime);
}

vector<vector<int>> StepEncoding::extractIntermediateStates(vector<int> &selectedLabels, vector<int> &currentLastState,
                                                            vector<int> &nextState, int labelTimestep) {
    if (data->segments)
        return segments_extract_intermediate_states(selectedLabels, currentLastState, nextState, labelTimestep);
    return forall_extract_intermediate_states(selectedLabels, currentLastState, nextState, labelTimestep);
}

// ---------------------------------------------------------------------------
// Plugins
// ---------------------------------------------------------------------------

static shared_ptr<SATEncodingFactory> _parse_label_based_sat_factory(options::OptionParser &parser) {
	vector<string> base_encoding;
	vector<string> base_encoding_doc;
	base_encoding.push_back("SEQUENTIAL");
	base_encoding_doc.push_back("sequential encoding");
	base_encoding.push_back("SELF_LOOP_PARALLEL");
	base_encoding_doc.push_back("self loop parallelism");
	base_encoding.push_back("CHAINS_PARALLEL");
	base_encoding_doc.push_back( "chains parallelism");
	parser.add_enum_option("encoding",
	                       base_encoding,
	                       "base encoding to be used",
	                       "SEQUENTIAL",
	                       base_encoding_doc);

	parser.add_option<bool>(
    	"use_label_group",
    	"use label group optimisation",
    	"false");

	parser.add_option<bool>(
    	"use_self_loop_optimisation",
    	"use optimisation for self loops",
    	"false");

	parser.add_option<bool>(
    	"use_empty_rows",
    	"use separate encoding for empty rows",
    	"false");

	parser.add_option<bool>(
    	"use_empty_cols",
    	"use separate encoding for empty cols",
    	"false");

	parser.add_option<bool>(
    	"use_empty_pillars",
    	"use separate encoding for empty pillars",
    	"false");

	parser.add_option<bool>(
    	"use_ones_in_last_dimension",
    	"if in the last dimension implication (source + label -> target), there is only one possible target use the positive edge for encoding instead of the negative edge",
    	"true");

	parser.add_option<bool>(
    	"use_positive_one",
    	"when encoding empty row/col/pillars, use special encoding if there is only one 1 (then positive encoding instead of negative)",
    	"false");

	parser.add_option<bool>(
    	"force_at_least_one_action",
    	"force that every time step contains at least one action",
    	"false");

	parser.add_option<bool>(
    	"chains_interleaved",
    	"CHAINS_PARALLEL: allocate the chain helper variables per (factor, state) together with their "
    	"clauses (the variant from May 2026) instead of all forward, then all backward helpers first; "
    	"same clauses, different variable numbering",
    	"false");

	parser.add_option<bool>(
    	"exists_step",
    	"strict exists-step semantics in the label order label_order (see step_encoding.h); "
    	"needs CHAINS_PARALLEL, whose forall chains it replaces",
    	"false");

	parser.add_option<bool>(
    	"exists_lax",
    	"exists_step: lax semantics (a label after the mover need not be applicable at the start "
    	"state, only self-loop at the end state)",
    	"false");

	parser.add_option<bool>(
    	"exists_effects_persist",
    	"exists_step: a label with a real transition in a factor ends the step in one of its targets "
    	"there (as in FDR, where an action's effect holds at the end of an exists step); keeps the "
    	"label -> target clauses of the forall encoding",
    	"false");

	parser.add_option<bool>(
    	"exists_pure_events",
    	"exists_step: for a label without any self-loop in a factor (it fires there only as the "
    	"mover) the chain event is label & next_t -> I_t instead of one event per source state",
    	"false");

	parser.add_option<bool>(
    	"exists_only_chains",
    	"diagnostic: replace the forall chains by the exists chains but keep every other forall "
    	"clause unweakened (forall-like semantics on the exists machinery; exists_lax ignored)",
    	"false");

	parser.add_option<shared_ptr<label_order_finder::LabelOrderFinder>>(
    	"label_order",
    	"exists_step: the label order of the step semantics",
    	"label_order_linear()");

	parser.add_option<int>(
    	"one_encoding_threshold",
    	"if ones in src/target/label is less or equal to this number, use positive encoding instead of negative one",
    	"1");

	parser.add_option<int>(
    	"one_encoding_threshold_percent",
    	"if percentage of ones in src/target/label is less or equal to this number, use positive encoding instead of negative one",
    	"0");

    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<StepEncodingFactory>(opts, StepPreset::LABEL_SAT);
}


static shared_ptr<SATEncodingFactory> _parse_sigma(options::OptionParser &parser) {
    parser.document_synopsis("Intermediate-state encoding over an FTS",
                             "R2E step semantics (labels in label order, several moves per factor "
                             "and step) with one variable per factor state changed by a label "
                             "(Symbolic Pattern Planning's sigma); see sat/step_encoding.h.");
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
                           "micro-step matrix), 2 ROWS (micro_steps_sat's row-wise primal/dual clauses with "
                           "exactly-one layers); see step_encoding.h", "0");
    parser.add_option<int>("rows_dual_threshold",
                           "ROWS: a row is encoded by its dual clause iff that has at most this many "
                           "literals (negative: always dual)", "3");
    parser.add_option<bool>("next_state_from_layers",
                            "the t+1 state variables are the last layer of the step: a state no label moves "
                            "keeps its variable from time t (no new variable, no equivalence clauses); "
                            "otherwise they are generated up front", "false");
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
    return make_shared<StepEncodingFactory>(opts, StepPreset::SIGMA);
}


static shared_ptr<SATEncodingFactory> _parse_micro_steps_sat_factory(options::OptionParser &parser) {
    parser.add_option<bool>(
        "use_self_loop_optimisation",
        "variable sharing: only create new layer variables for the states moved by a label "
        "(source or target of one of its non-self-loop transitions) and skip labels that are "
        "irrelevant for a factor",
        "false");

    parser.add_option<int>(
        "dual_threshold",
        "arity threshold tau: a row of a micro-step is encoded by its dual constraint iff the "
        "dual clause has at most tau literals, and by its primal instances otherwise. "
        "0 yields a fully primal encoding, a negative value a fully dual one (tau = infinity)",
        "3");

    parser.add_option<bool>(
        "force_at_least_one_action",
        "force that every time step contains at least one action",
        "false");

    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<StepEncodingFactory>(opts, StepPreset::MICRO_STEPS);
}


static options::PluginShared<SATEncodingFactory> _plugin_label_sat("label_sat", _parse_label_based_sat_factory);
static options::PluginShared<SATEncodingFactory> _plugin_sigma_sat("sigma_sat", _parse_sigma);
static options::PluginShared<SATEncodingFactory> _plugin_micro_steps_sat("micro_steps_sat", _parse_micro_steps_sat_factory);

}
