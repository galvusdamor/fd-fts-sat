#include "exists_encoding.h"

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
#include <climits>
#include <iostream>
#include <queue>
#include <set>
#include <map>

using namespace std;
using namespace task_representation;

namespace sat_search {

// ---------------------------------------------------------------------------
// Factory: per-factor tables, conflict order, guards
// ---------------------------------------------------------------------------

ExistsEncodingFactory::ExistsEncodingFactory(const options::Options &opts)
    : SATEncodingFactory(opts.get<bool>("force_at_least_one_action")),
      semantics(ExistsSemantics(opts.get_enum("semantics"))),
      exploitOrder(opts.get<bool>("exploit_order")),
      conflictOrder(opts.get<bool>("conflict_order")),
      impliedClauses(opts.get<bool>("implied_clauses")),
      breakMoverSymmetry(opts.get<bool>("break_mover_symmetry")),
      labelOrderFinder(opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("label_order")),
      staticOptions(opts) {
}

namespace {
// labels of a factor per state: self-loop labels and labels applicable there
struct StateLabels {
    vector<vector<int>> selfLoop, applicable;
    // real moves: (source, target) -> labels
    map<pair<int, int>, vector<int>> moveLabels;
};

StateLabels collect(const TransitionSystem &ts, int numLabels) {
    StateLabels s;
    s.selfLoop.resize(ts.get_size());
    s.applicable.resize(ts.get_size());
    for (int l = 0; l < numLabels; l++) {
        if (ts.isIrrelevantLabel(l))
            continue;
        for (const Transition &t : ts.get_transitions_with_label(l)) {
            s.applicable[t.src].push_back(l);
            if (t.src == t.target)
                s.selfLoop[t.src].push_back(l);
            else
                s.moveLabels[{t.src, t.target}].push_back(l);
        }
    }
    for (auto &v : s.applicable) {
        sort(v.begin(), v.end());
        v.erase(unique(v.begin(), v.end()), v.end());
    }
    for (auto &v : s.selfLoop)
        sort(v.begin(), v.end());
    return s;
}

// labels in a but not in b (both sorted)
vector<int> minus_sorted(const vector<int> &a, const vector<int> &b) {
    vector<int> r;
    set_difference(a.begin(), a.end(), b.begin(), b.end(), back_inserter(r));
    return r;
}

vector<int> intersect_sorted(const vector<int> &a, const vector<int> &b) {
    vector<int> r;
    set_intersection(a.begin(), a.end(), b.begin(), b.end(), back_inserter(r));
    return r;
}

/*
  Conflict order: labels 0..L-1 plus one hub per move and kind (kind 1: B1 -> hub -> movers,
  kind 2: movers -> hub -> B2). Tarjan SCCs, then the condensation in topological order, ready
  components by the smallest base position of their labels; labels inside a component by base
  position.
*/
vector<int> conflict_order(const FTSTask &fts, const vector<StateLabels> &perFactor,
                           ExistsSemantics semantics, const vector<int> &basePosition,
                           int &sccMax, int &sccLabels) {
    const int L = fts.get_num_labels();
    vector<vector<int>> adj(L);
    for (size_t f = 0; f < perFactor.size(); f++) {
        const StateLabels &sl = perFactor[f];
        for (const auto &[move, M] : sl.moveLabels) {
            auto [s, t] = move;
            vector<int> B1 = minus_sorted(sl.selfLoop[s], sl.selfLoop[t]);
            vector<int> B2 = minus_sorted(sl.selfLoop[t], sl.selfLoop[s]);
            if (semantics == ExistsSemantics::STRICT)
                B2 = intersect_sorted(B2, sl.applicable[s]);
            if (!B1.empty()) {
                int h = adj.size();
                adj.emplace_back(M.begin(), M.end());
                for (int b : B1)
                    adj[b].push_back(h);
            }
            if (!B2.empty()) {
                int h = adj.size();
                adj.emplace_back(B2.begin(), B2.end());
                for (int a : M)
                    adj[a].push_back(h);
            }
        }
    }
    const int n = adj.size();
    // iterative Tarjan
    vector<int> index(n, -1), low(n, 0), comp(n, -1);
    vector<char> onStack(n, 0);
    vector<int> stack;
    int counter = 0, numComp = 0;
    for (int root = 0; root < n; root++) {
        if (index[root] != -1)
            continue;
        vector<pair<int, size_t>> work{{root, 0}};
        index[root] = low[root] = counter++;
        stack.push_back(root);
        onStack[root] = 1;
        while (!work.empty()) {
            auto &[v, i] = work.back();
            if (i < adj[v].size()) {
                int w = adj[v][i++];
                if (index[w] == -1) {
                    index[w] = low[w] = counter++;
                    stack.push_back(w);
                    onStack[w] = 1;
                    work.push_back({w, 0});
                } else if (onStack[w]) {
                    low[v] = min(low[v], index[w]);
                }
            } else {
                int vv = v;
                work.pop_back();
                if (!work.empty())
                    low[work.back().first] = min(low[work.back().first], low[vv]);
                if (low[vv] == index[vv]) {
                    while (true) {
                        int w = stack.back();
                        stack.pop_back();
                        onStack[w] = 0;
                        comp[w] = numComp;
                        if (w == vv)
                            break;
                    }
                    numComp++;
                }
            }
        }
    }
    vector<vector<int>> compLabels(numComp);
    for (int l = 0; l < L; l++)
        compLabels[comp[l]].push_back(l);
    sccMax = 1;
    sccLabels = 0;
    for (auto &c : compLabels) {
        sort(c.begin(), c.end(), [&](int a, int b) {return basePosition[a] < basePosition[b];});
        if (c.size() >= 2) {
            sccMax = max<int>(sccMax, c.size());
            sccLabels += c.size();
        }
    }
    // condensation edges and in-degrees
    vector<vector<int>> cadj(numComp);
    vector<int> indeg(numComp, 0);
    for (int v = 0; v < n; v++)
        for (int w : adj[v])
            if (comp[v] != comp[w]) {
                cadj[comp[v]].push_back(comp[w]);
                indeg[comp[w]]++;
            }
    // Kahn; hub-only components first (key -1), others by smallest base position
    auto key = [&](int c) {return compLabels[c].empty() ? -1 : basePosition[compLabels[c][0]];};
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<>> ready;
    for (int c = 0; c < numComp; c++)
        if (indeg[c] == 0)
            ready.push({key(c), c});
    vector<int> order;
    order.reserve(L);
    while (!ready.empty()) {
        int c = ready.top().second;
        ready.pop();
        for (int l : compLabels[c])
            order.push_back(l);
        for (int d : cadj[c])
            if (--indeg[d] == 0)
                ready.push({key(d), d});
    }
    assert((int)order.size() == L);
    return order;
}
}

void ExistsEncodingFactory::initialize() {
    utils::Timer timer;
    cout << "Initialising" << fts << endl;
    cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;
    const int L = fts->get_num_labels();
    const int F = fts->get_size();

    vector<StateLabels> perFactor;
    perFactor.reserve(F);
    for (int f = 0; f < F; f++)
        perFactor.push_back(collect(fts->get_ts(f), L));

    vector<int> base = labelOrderFinder->find_order(*fts);
    assert((int)base.size() == L);
    vector<int> basePosition(L);
    for (int k = 0; k < L; k++)
        basePosition[base[k]] = k;

    auto result = make_shared<ExistsData>();
    result->semantics = semantics;
    result->exploitOrder = exploitOrder;
    result->impliedClauses = impliedClauses;
    result->breakMoverSymmetry = breakMoverSymmetry;
    if (breakMoverSymmetry && semantics != ExistsSemantics::FORALL) {
        cerr << "exists_sat: break_mover_symmetry is only exact for semantics=FORALL" << endl;
        utils::exit_with(utils::ExitCode::INPUT_ERROR);
    }
    long long impliedEntries = 0, symmetryGroups = 0;
    int sccMax = 0, sccLabels = 0;
    if (conflictOrder)
        result->order = conflict_order(*fts, perFactor, semantics, basePosition, sccMax, sccLabels);
    else
        result->order = base;
    vector<int> position(L);
    for (int k = 0; k < L; k++)
        position[result->order[k]] = k;

    long long relevantEntries = 0, moverEntries = 0, guardedEntries = 0;
    result->factors.resize(F);
    for (int f = 0; f < F; f++) {
        const TransitionSystem &ts = fts->get_ts(f);
        const StateLabels &sl = perFactor[f];
        ExistsFactorData &fd = result->factors[f];
        for (int l = 0; l < L; l++)
            if (!ts.isIrrelevantLabel(l))
                fd.labels.push_back(l);
        sort(fd.labels.begin(), fd.labels.end(), [&](int a, int b) {return position[a] < position[b];});
        const int n = fd.labels.size();
        vector<int> entry(L, -1);
        for (int k = 0; k < n; k++)
            entry[fd.labels[k]] = k;
        fd.selfLoopStates.resize(n);
        fd.sourceStates.resize(n);
        fd.moves.resize(n);
        fd.moverIndex.assign(n, -1);
        fd.guarded.assign(n, !exploitOrder);
        for (int k = 0; k < n; k++) {
            map<int, vector<int>> bySource;
            set<int> sources;
            for (const Transition &t : ts.get_transitions_with_label(fd.labels[k])) {
                sources.insert(t.src);
                if (t.src == t.target)
                    fd.selfLoopStates[k].push_back(t.src);
                else
                    bySource[t.src].push_back(t.target);
            }
            fd.sourceStates[k].assign(sources.begin(), sources.end());
            sort(fd.selfLoopStates[k].begin(), fd.selfLoopStates[k].end());
            for (auto &[s, targets] : bySource) {
                sort(targets.begin(), targets.end());
                targets.erase(unique(targets.begin(), targets.end()), targets.end());
                fd.moves[k].push_back({s, targets});
            }
            if (!fd.moves[k].empty()) {
                fd.moverIndex[k] = fd.movers.size();
                fd.movers.push_back(k);
            }
        }
        if (exploitOrder) {
            // a label needs the guard if one of its conflicts is not resolved by the order
            for (const auto &[move, M] : sl.moveLabels) {
                auto [s, t] = move;
                // two smallest and two largest positions among the movers (to exclude l itself)
                int min1 = INT_MAX, min2 = INT_MAX, max1 = INT_MIN, max2 = INT_MIN;
                int argMin1 = -1, argMax1 = -1;
                for (int a : M) {
                    int p = position[a];
                    if (p < min1) {min2 = min1; min1 = p; argMin1 = a;} else if (p < min2) min2 = p;
                    if (p > max1) {max2 = max1; max1 = p; argMax1 = a;} else if (p > max2) max2 = p;
                }
                for (int b : minus_sorted(sl.selfLoop[s], sl.selfLoop[t])) {        // kind 1
                    int earliest = (b == argMin1) ? min2 : min1;
                    if (earliest < position[b])
                        fd.guarded[entry[b]] = true;
                }
                vector<int> B2 = minus_sorted(sl.selfLoop[t], sl.selfLoop[s]);      // kind 2
                if (semantics == ExistsSemantics::STRICT)
                    B2 = intersect_sorted(B2, sl.applicable[s]);
                for (int b : B2) {
                    int latest = (b == argMax1) ? max2 : max1;
                    if (latest > position[b])
                        fd.guarded[entry[b]] = true;
                }
            }
        }
        if (impliedClauses) {
            // states one move away from s in this factor (over all relevant labels)
            vector<set<int>> oneMove(ts.get_size());
            for (int k = 0; k < n; k++)
                for (const auto &[src, targets] : fd.moves[k])
                    oneMove[src].insert(targets.begin(), targets.end());
            fd.implied.resize(n);
            for (int k = 0; k < n; k++) {
                const vector<int> &loops = fd.selfLoopStates[k];
                auto loopsAt = [&](int x) {return binary_search(loops.begin(), loops.end(), x);};
                map<int, const vector<int> *> targetsFrom;
                for (const auto &[src, targets] : fd.moves[k])
                    targetsFrom[src] = &targets;
                for (int s = 0; s < ts.get_size(); s++) {
                    set<int> possible = oneMove[s];          // end states of the factor at all
                    possible.insert(s);
                    set<int> allowed;
                    bool applicable = binary_search(fd.sourceStates[k].begin(), fd.sourceStates[k].end(), s);
                    if (targetsFrom.count(s))                 // as the mover
                        allowed.insert(targetsFrom[s]->begin(), targetsFrom[s]->end());
                    if (semantics == ExistsSemantics::FORALL) {
                        if (loopsAt(s))
                            for (int t : possible)
                                if (loopsAt(t))
                                    allowed.insert(t);
                    } else if (semantics == ExistsSemantics::STRICT && !applicable) {
                        allowed.clear();                      // cannot fire at all
                    } else {
                        if (loopsAt(s))                       // before the mover
                            allowed.insert(possible.begin(), possible.end());
                        for (int t : possible)                // after the mover
                            if (loopsAt(t))
                                allowed.insert(t);
                    }
                    if (allowed.size() < possible.size()) {
                        fd.implied[k].push_back({s, vector<int>(allowed.begin(), allowed.end())});
                        impliedEntries++;
                    }
                }
            }
        }
        if (breakMoverSymmetry) {
            map<vector<pair<int, int>>, vector<int>> byTransitions;
            for (int k : fd.movers) {
                vector<pair<int, int>> tr;
                for (const Transition &t : ts.get_transitions_with_label(fd.labels[k]))
                    tr.push_back({t.src, t.target});
                sort(tr.begin(), tr.end());
                byTransitions[tr].push_back(k);
            }
            for (auto &[tr, entries] : byTransitions)
                if (entries.size() >= 2) {
                    fd.moverGroups.push_back(entries);
                    symmetryGroups++;
                }
        }
        relevantEntries += n;
        moverEntries += fd.movers.size();
        for (bool g : fd.guarded)
            guardedEntries += g;
    }
    data = result;
    staticConstraints = static_constraints::compute(*fts, staticOptions);
    cout << "EXISTSSTAT semantics " << (semantics == ExistsSemantics::STRICT ? "strict" :
                                        semantics == ExistsSemantics::LAX ? "lax" : "forall")
         << " conflict_order " << conflictOrder << " exploit_order " << exploitOrder
         << " relevant_entries " << relevantEntries << " mover_entries " << moverEntries
         << " guarded_entries " << guardedEntries << " scc_max " << sccMax
         << " scc_labels " << sccLabels << " implied_entries " << impliedEntries
         << " symmetry_groups " << symmetryGroups << " time " << timer() << endl;
    cout << "SAT init time: " << timer << endl;
}

unique_ptr<SATEncoding> ExistsEncodingFactory::createEncodingInstance(shared_ptr<sat_capsule> capsule) {
    auto encoding = make_unique<ExistsEncoding>(capsule, fts, forceAtLeastOneAction, data);
    encoding->setStaticConstraints(staticConstraints);
    return encoding;
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

ExistsEncoding::ExistsEncoding(shared_ptr<sat_capsule> capsule, const shared_ptr<FTSTask> &fts,
                               bool forceAtLeastOneAction, shared_ptr<const ExistsData> _data)
    : CommonEncoding(capsule, fts, forceAtLeastOneAction, false, true), data(_data) {
    // plan extraction executes the labels of a step in this order
    labelOrder = data->order;
}

void ExistsEncoding::generateAdditionalVariables(int /*fromTime*/) {
}

void ExistsEncoding::encode_frame_axioms(const vector<vector<int>> &, const vector<vector<int>> &, int) {
    // part of encode_factor (a factor without a mover keeps its state)
}

void ExistsEncoding::encode_transition(const vector<vector<int>> &previousStateVars,
                                       const vector<vector<int>> &nextStateVars, int fromTime) {
    const vector<int> &labelVars = allTimesLabelVars[fromTime];
    vector<vector<int>> &moverVars = allTimesMoverVars[fromTime];
    moverVars.resize(fts->get_size());
    for (int f = 0; f < fts->get_size(); f++)
        encode_factor(f, previousStateVars[f], nextStateVars[f], labelVars, moverVars[f]);
}

void ExistsEncoding::encode_factor(int f, const vector<int> &prev, const vector<int> &next,
                                   const vector<int> &labelVars, vector<int> &moverVars) {
    const ExistsFactorData &fd = data->factors[f];
    const int numStates = prev.size();
    const int n = fd.labels.size();
    moverVars.assign(n, 0);

    // movers and the prefix M_j <-> some of the first j+1 movers fired
    vector<int> prefix(fd.movers.size());
    for (size_t j = 0; j < fd.movers.size(); j++) {
        const int k = fd.movers[j];
        const int X = labelVars[fd.labels[k]];
        const int mv = sat->new_variable();
        DEBUG(sat->registerVariable(mv, "mover:" + to_string(f) + ":" + to_string(fd.labels[k])));
        moverVars[k] = mv;
        sat->implies(mv, X);
        vector<int> sources;
        for (const auto &[s, targets] : fd.moves[k]) {
            sources.push_back(prev[s]);
            vector<int> nextTargets;
            for (int t : targets)
                nextTargets.push_back(next[t]);
            sat->andImpliesOr(vector<int>{mv, prev[s]}, nextTargets);
        }
        sat->impliesOr(mv, sources);
        const int M = sat->new_variable();
        DEBUG(sat->registerVariable(M, "moved:" + to_string(f) + ":" + to_string(j)));
        prefix[j] = M;
        sat->implies(mv, M);
        if (j > 0) {
            sat->implies(prefix[j - 1], M);
            sat->implies(prefix[j - 1], -mv);          // at most one mover
            sat->impliesOr(M, vector<int>{prefix[j - 1], mv});
        } else {
            sat->implies(M, mv);
        }
    }

    // frame: without a mover the factor keeps its state
    for (int s = 0; s < numStates; s++) {
        if (fd.movers.empty())
            sat->implies(prev[s], next[s]);
        else
            sat->impliesOr(prev[s], vector<int>{next[s], prefix.back()});
    }

    // non-movers
    int lastMoverBefore = -1;      // index into prefix of the last mover before entry k
    for (int k = 0; k < n; k++) {
        const int X = labelVars[fd.labels[k]];
        vector<int> lhs{X};
        if (moverVars[k])
            lhs.push_back(-moverVars[k]);
        vector<int> loopPrev, loopNext;
        for (int s : fd.selfLoopStates[k]) {
            loopPrev.push_back(prev[s]);
            loopNext.push_back(next[s]);
        }
        if (data->semantics == ExistsSemantics::FORALL) {
            sat->andImpliesOr(lhs, loopPrev);
            sat->andImpliesOr(lhs, loopNext);
        } else if (fd.guarded[k]) {
            if (lastMoverBefore < 0) {
                sat->andImpliesOr(lhs, loopPrev);
            } else {
                const int g = prefix[lastMoverBefore];
                vector<int> before = lhs, after = lhs;
                before.push_back(-g);
                after.push_back(g);
                sat->andImpliesOr(before, loopPrev);
                sat->andImpliesOr(after, loopNext);
            }
        } else {
            vector<int> either = loopPrev;
            either.insert(either.end(), loopNext.begin(), loopNext.end());
            sat->andImpliesOr(lhs, either);
        }
        // STRICT: every firing label is applicable at the start of the step
        if (data->semantics == ExistsSemantics::STRICT && !fd.allSourcesEverywhere(k, numStates)) {
            vector<int> sources;
            for (int s : fd.sourceStates[k])
                sources.push_back(prev[s]);
            sat->impliesOr(X, sources);
        }
        if (fd.moverIndex[k] >= 0)
            lastMoverBefore = fd.moverIndex[k];
    }

    // implied: a firing label restricts the end state directly
    if (data->impliedClauses) {
        for (int k = 0; k < n; k++) {
            const int X = labelVars[fd.labels[k]];
            for (const auto &[s, allowed] : fd.implied[k]) {
                vector<int> ends;
                for (int t : allowed)
                    ends.push_back(next[t]);
                sat->andImpliesOr(vector<int>{X, prev[s]}, ends);
            }
        }
    }
    // symmetry breaking (FORALL): among labels with identical transitions in this factor, only
    // the first firing one may be the mover
    if (data->breakMoverSymmetry) {
        for (const vector<int> &group : fd.moverGroups) {
            int fired = 0;     // some earlier label of the group fires
            for (int k : group) {
                if (fired)
                    sat->implies(moverVars[k], -fired);
                const int F = sat->new_variable();
                sat->implies(labelVars[fd.labels[k]], F);
                if (fired)
                    sat->implies(fired, F);
                fired = F;
            }
        }
    }
}

vector<vector<int>> ExistsEncoding::extractIntermediateStates(vector<int> &selectedLabels,
                                                              vector<int> &currentLastState,
                                                              vector<int> &nextState,
                                                              int labelTimestep) {
    // selectedLabels are in label order; per factor the state is the start state up to the
    // mover and the end state from the mover on
    const vector<vector<int>> &moverVars = allTimesMoverVars[labelTimestep];
    vector<int> moverPosition(fts->get_size(), INT_MAX);
    for (int f = 0; f < fts->get_size(); f++) {
        const ExistsFactorData &fd = data->factors[f];
        for (int k : fd.movers) {
            if (ipasir_val(sat->solver, moverVars[f][k]) > 0) {
                auto it = find(selectedLabels.begin(), selectedLabels.end(), fd.labels[k]);
                assert(it != selectedLabels.end());
                moverPosition[f] = it - selectedLabels.begin();
                break;
            }
        }
    }
    vector<vector<int>> states;
    for (size_t i = 0; i + 1 < selectedLabels.size(); i++) {
        vector<int> state(fts->get_size());
        for (int f = 0; f < fts->get_size(); f++)
            state[f] = ((int)i >= moverPosition[f]) ? nextState[f] : currentLastState[f];
        states.push_back(state);
    }
    return states;
}

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------

static shared_ptr<SATEncodingFactory> _parse(options::OptionParser &parser) {
    parser.document_synopsis("Exists-step encoding over an FTS",
                             "One real move per factor and step; the other labels of the step "
                             "run as self-loops before or after it, in label order. See "
                             "sat/exists_encoding.h.");
    parser.add_enum_option("semantics", {"STRICT", "LAX", "FORALL"},
                           "STRICT: Rintanen's exists-step, every firing label is applicable at "
                           "the start of the step; LAX: a label after the mover only needs its "
                           "self-loop at the end state; FORALL: forall-step with the same variables (comparison)", "STRICT");
    parser.add_option<bool>("exploit_order",
                            "no before/after guard for labels whose conflicts the label order "
                            "resolves", "true");
    parser.add_option<bool>("conflict_order",
                            "reorder the labels by the SCCs of the conflict graph (topologically; "
                            "label_order breaks ties and orders the SCCs internally)", "true");
    parser.add_option<bool>("implied_clauses",
                            "add the implied clauses label & start state -> allowed end states",
                            "false");
    parser.add_option<bool>("break_mover_symmetry",
                            "only the first firing label among labels with identical transitions "
                            "in a factor may be its mover (exact only for semantics=FORALL)",
                            "false");
    parser.add_option<shared_ptr<label_order_finder::LabelOrderFinder>>(
        "label_order", "base label order", "label_order_tsort()");
    parser.add_option<bool>("force_at_least_one_action",
                            "force that every time step contains at least one action", "false");
    static_constraints::add_options(parser);
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<ExistsEncodingFactory>(opts);
}

static options::PluginShared<SATEncodingFactory> _plugin("exists_sat", _parse);
}
