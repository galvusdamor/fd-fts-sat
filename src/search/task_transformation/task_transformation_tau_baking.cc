#include "task_transformation_tau_baking.h"

#include "label_transformation_utils.h"

#include "../task_representation/fts_task.h"
#include "../task_representation/labels.h"
#include "../options/plugin.h"
#include "../plugin.h"
#include "../utils/system.h"
#include "../utils/timer.h"

#include <cassert>
#include <deque>
#include <iostream>

using namespace std;
using namespace task_representation;

namespace task_transformation {
TauBakingTransformation::TauBakingTransformation(const options::Options &opts)
    : max_rounds(opts.get<int>("max_rounds")) {
}

// states reachable from `start` (a set) in the graph given as adjacency lists
static vector<bool> reachable(const vector<vector<int>> &graph, const vector<bool> &start) {
    vector<bool> seen(start);
    deque<int> queue;
    for (size_t s = 0; s < start.size(); ++s)
        if (start[s]) queue.push_back(s);
    while (!queue.empty()) {
        const int x = queue.front();
        queue.pop_front();
        for (int t : graph[x])
            if (!seen[t]) { seen[t] = true; queue.push_back(t); }
    }
    return seen;
}

/*
  One round. Returns null if there is nothing to do (no tau label, no label to remove).
*/
static pair<shared_ptr<FTSTask>, shared_ptr<PlanReconstructionTauBaking>>
bake_round(const FTSTask &task, int round) {
    utils::Timer timer;
    const int F = task.get_size();
    const int L = task.get_num_labels();
    vector<vector<vector<Transition>>> trans(F);
    vector<vector<char>> noop(F, vector<char>(L));
    for (int f = 0; f < F; ++f) {
        trans[f] = transitions_by_label(task.get_ts(f), L);
        for (int l = 0; l < L; ++l)
            noop[f][l] = is_noop_label(task.get_ts(f), l);
    }

    // tau_of[l]: the factor l is a tau label of, -1 if none, -2 if l is a no-op everywhere
    vector<int> tau_of(L, -1);
    vector<char> keep(L, 1);
    int num_tau = 0, num_noop = 0;
    for (int l = 0; l < L; ++l) {
        int moving = -2;
        for (int f = 0; f < F; ++f) {
            if (!noop[f][l]) {
                if (moving == -2) moving = f;
                else { moving = -1; break; }
            }
        }
        tau_of[l] = moving;
        if (moving != -1) {
            keep[l] = 0;
            if (moving == -2) ++num_noop; else ++num_tau;
        }
    }
    if (num_tau == 0 && num_noop == 0)
        return make_pair(nullptr, nullptr);

    auto reconstruction = make_shared<PlanReconstructionTauBaking>();
    reconstruction->baked.resize(F);
    reconstruction->old_initial_state.resize(F);
    vector<vector<bool>> goal(F);
    long long transitions_before = 0, transitions_after = 0;
    int baked_factors = 0, goals_added = 0;
    for (int f = 0; f < F; ++f) {
        const TransitionSystem &ts = task.get_ts(f);
        const int n = ts.get_size();
        reconstruction->old_initial_state[f] = ts.get_init_state();
        goal[f] = ts.get_is_goal();
        vector<vector<pair<int, int>>> tau_succ(n);
        vector<vector<int>> tau_pred(n);
        bool any = false;
        for (int l = 0; l < L; ++l) {
            if (tau_of[l] != f) continue;
            for (const Transition &t : trans[f][l]) {
                if (t.src == t.target) continue;
                tau_succ[t.src].emplace_back(l, t.target);
                tau_pred[t.target].push_back(t.src);
                any = true;
            }
        }
        if (!any) continue;
        ++baked_factors;

        // pre[s]: the states from which s is reachable with tau labels (s included)
        vector<vector<int>> pre(n);
        for (int s = 0; s < n; ++s) {
            vector<bool> start(n, false);
            start[s] = true;
            vector<bool> r = reachable(tau_pred, start);
            for (int x = 0; x < n; ++x)
                if (r[x]) pre[s].push_back(x);
        }
        auto baked = utils::make_unique_ptr<PlanReconstructionTauBaking::BakedFactor>();
        baked->tau_successors = move(tau_succ);
        baked->moves.assign(L, 0);
        baked->transitions.resize(L);
        baked->goal = goal[f];
        for (int l = 0; l < L; ++l) {
            if (!keep[l] || noop[f][l]) continue;
            baked->moves[l] = 1;
            baked->transitions[l] = trans[f][l];
            vector<Transition> result;
            for (const Transition &t : trans[f][l])
                for (int x : pre[t.src])
                    result.emplace_back(x, t.target);
            sort_unique(result);
            transitions_before += trans[f][l].size();
            transitions_after += result.size();
            trans[f][l] = move(result);
        }
        const vector<bool> new_goal = reachable(tau_pred, goal[f]);
        for (int s = 0; s < n; ++s)
            if (new_goal[s] && !goal[f][s]) ++goals_added;
        goal[f] = new_goal;
        reconstruction->baked[f] = move(baked);
    }

    // prune unreachable and dead states, remove labels left without transitions
    vector<vector<bool>> alive(F);
    for (int f = 0; f < F; ++f)
        alive[f].assign(task.get_ts(f).get_size(), true);
    int dead_labels = 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (int f = 0; f < F; ++f) {
            const int n = alive[f].size();
            vector<vector<int>> succ(n), pred(n);
            for (int l = 0; l < L; ++l) {
                if (!keep[l]) continue;
                for (const Transition &t : trans[f][l]) {
                    if (!alive[f][t.src] || !alive[f][t.target]) continue;
                    succ[t.src].push_back(t.target);
                    pred[t.target].push_back(t.src);
                }
            }
            vector<bool> init(n, false);
            init[task.get_ts(f).get_init_state()] = true;
            vector<bool> goals(n);
            for (int s = 0; s < n; ++s) goals[s] = goal[f][s] && alive[f][s];
            const vector<bool> fw = reachable(succ, init), bw = reachable(pred, goals);
            for (int s = 0; s < n; ++s) {
                if (alive[f][s] && !(fw[s] && bw[s])) {
                    alive[f][s] = false;
                    changed = true;
                }
            }
            if (!alive[f][task.get_ts(f).get_init_state()]) {
                cerr << "tau_baking: factor " << f << " has no path to its goal" << endl;
                utils::exit_with(utils::ExitCode::CRITICAL_ERROR);
            }
        }
        for (int l = 0; l < L; ++l) {
            if (!keep[l]) continue;
            for (int f = 0; f < F; ++f) {
                bool any = false;
                for (const Transition &t : trans[f][l])
                    if (alive[f][t.src] && alive[f][t.target]) { any = true; break; }
                if (!any) {
                    keep[l] = 0;
                    ++dead_labels;
                    changed = true;
                    break;
                }
            }
        }
    }

    vector<int> new_label(L, -1);
    for (int l = 0; l < L; ++l) {
        if (keep[l]) {
            new_label[l] = reconstruction->old_label.size();
            reconstruction->old_label.push_back(l);
        }
    }
    auto labels = select_labels(task.get_labels(), reconstruction->old_label);
    vector<unique_ptr<TransitionSystem>> systems;
    reconstruction->old_state.resize(F);
    int pruned_states = 0;
    for (int f = 0; f < F; ++f) {
        const TransitionSystem &ts = task.get_ts(f);
        const int n = ts.get_size();
        vector<int> new_state(n, -1);
        for (int s = 0; s < n; ++s) {
            if (alive[f][s]) {
                new_state[s] = reconstruction->old_state[f].size();
                reconstruction->old_state[f].push_back(s);
            } else {
                ++pruned_states;
            }
        }
        const int m = reconstruction->old_state[f].size();
        vector<vector<Transition>> transitions(reconstruction->old_label.size());
        for (size_t k = 0; k < reconstruction->old_label.size(); ++k) {
            for (const Transition &t : trans[f][reconstruction->old_label[k]])
                if (alive[f][t.src] && alive[f][t.target])
                    transitions[k].emplace_back(new_state[t.src], new_state[t.target]);
            sort_unique(transitions[k]);
        }
        vector<bool> goals(m);
        for (int s = 0; s < m; ++s)
            goals[s] = goal[f][reconstruction->old_state[f][s]];
        systems.push_back(build_transition_system(ts, *labels, move(transitions), m, move(goals),
                                                  new_state[ts.get_init_state()]));
    }
    cout << "TAUBAKING round " << round << " labels " << L << " tau_labels " << num_tau
         << " noop_labels " << num_noop << " dead_labels " << dead_labels
         << " kept_labels " << reconstruction->old_label.size() << " baked_factors " << baked_factors
         << " transitions_before " << transitions_before << " transitions_after " << transitions_after
         << " goals_added " << goals_added << " pruned_states " << pruned_states
         << " time " << timer() << endl;
    return make_pair(make_shared<FTSTask>(move(systems), move(labels)), reconstruction);
}

pair<shared_ptr<FTSTask>, shared_ptr<PlanReconstruction>>
TauBakingTransformation::transform_task(const shared_ptr<FTSTask> &fts_task) {
    shared_ptr<FTSTask> task = fts_task;
    vector<shared_ptr<PlanReconstruction>> rounds;
    for (int round = 1; max_rounds < 0 || round <= max_rounds; ++round) {
        auto result = bake_round(*task, round);
        if (!result.first)
            break;
        task = result.first;
        rounds.push_back(result.second);   // in the order applied, see PlanReconstructionSequence
        if (task->trivially_solved())
            break;
    }
    cout << "TAUBAKING rounds " << rounds.size() << " result " << *task << endl;
    return make_pair(task, make_shared<PlanReconstructionSequence>(move(rounds)));
}

pair<shared_ptr<FTSTask>, Mapping>
TauBakingTransformation::transform_task_lossy(const shared_ptr<FTSTask> &) {
    cerr << "tau_baking() does not support lossy transformations" << endl;
    utils::exit_with(utils::ExitCode::UNSUPPORTED);
}

void PlanReconstructionTauBaking::tau_path(int factor, vector<int> &state, const vector<bool> &target,
                                           vector<int> &labels, vector<PlanState> &states) const {
    const BakedFactor &b = *baked[factor];
    const int from = state[factor];
    if (target[from])
        return;
    vector<pair<int, int>> parent(b.tau_successors.size(), make_pair(-2, -1));   // (state, label)
    parent[from] = make_pair(-1, -1);
    deque<int> queue {from};
    int found = -1;
    while (!queue.empty() && found == -1) {
        const int x = queue.front();
        queue.pop_front();
        for (const auto &e : b.tau_successors[x]) {
            if (parent[e.second].first != -2) continue;
            parent[e.second] = make_pair(x, e.first);
            if (target[e.second]) { found = e.second; break; }
            queue.push_back(e.second);
        }
    }
    if (found == -1) {
        cerr << "tau_baking: no tau path in factor " << factor << " from state " << from << endl;
        utils::exit_with(utils::ExitCode::CRITICAL_ERROR);
    }
    vector<pair<int, int>> path;   // (label, state)
    for (int x = found; x != from; x = parent[x].first)
        path.emplace_back(parent[x].second, x);
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        state[factor] = it->second;
        labels.push_back(it->first);
        states.emplace_back(vector<int>(state));
    }
}

void PlanReconstructionTauBaking::reconstruct_plan(Plan &plan) const {
    const vector<int> &labels = plan.get_labels();
    const vector<PlanState> &states = plan.get_traversed_states();
    const int F = old_state.size();
    auto to_old = [&](const PlanState &s) {
        vector<int> values(F);
        for (int f = 0; f < F; ++f)
            values[f] = old_state[f][s[f]];
        return values;
    };
    vector<int> current = states.empty() ? old_initial_state : to_old(states[0]);
    vector<int> new_labels;
    vector<PlanState> new_states;
    new_states.emplace_back(vector<int>(current));
    for (size_t step = 0; step < labels.size(); ++step) {
        const int label = old_label[labels[step]];
        vector<int> next = to_old(states[step + 1]);
        for (int f = 0; f < F; ++f) {
            const BakedFactor *b = baked[f].get();
            if (!b || !b->moves[label]) continue;
            vector<bool> target(b->tau_successors.size(), false);
            for (const Transition &t : b->transitions[label])
                if (t.target == next[f]) target[t.src] = true;
            tau_path(f, current, target, new_labels, new_states);
        }
        new_labels.push_back(label);
        new_states.emplace_back(vector<int>(next));
        current = move(next);
    }
    for (int f = 0; f < F; ++f)
        if (baked[f])
            tau_path(f, current, baked[f]->goal, new_labels, new_states);
    cout << "TauBaking reconstruction: " << labels.size() << " -> " << new_labels.size()
         << " labels" << endl;
    plan.set_plan(move(new_states), move(new_labels));
}

bool PlanReconstructionTauBaking::map_state_sets(StateSets &sets, string &why) const {
    if (old_state.size() != sets.tuples.size()) { why = "tau baking factor count"; return false; }
    for (size_t f = 0; f < sets.tuples.size(); ++f) {
        const auto &old = old_state[f];
        const BakedFactor *b = baked[f].get();
        vector<vector<int>> successors(sets.tuples[f].size());
        if (b) {
            if (b->tau_successors.size() != sets.tuples[f].size()) { why = "tau baking state count"; return false; }
            for (size_t x = 0; x < successors.size(); ++x)
                for (const auto &e : b->tau_successors[x]) successors[x].push_back(e.second);
        }
        vector<vector<StateSets::Tuple>> renumbered(old.size());
        for (size_t s2 = 0; s2 < old.size(); ++s2) {
            if (old[s2] < 0 || old[s2] >= (int)sets.tuples[f].size()) { why = "tau baking state map"; return false; }
            renumbered[s2] = b ? closure_tuples(sets.tuples[f], successors, old[s2]) : sets.tuples[f][old[s2]];
        }
        sets.tuples[f].swap(renumbered);
    }
    return true;
}

static shared_ptr<TaskTransformation> _parse(options::OptionParser &parser) {
    parser.document_synopsis(
        "Tau baking",
        "Removes tau labels (labels that move one factor and are self-loops everywhere in all "
        "others) by baking their paths into the following labels and into the goal.");
    parser.add_option<int>("max_rounds", "repeat while labels are removed, at most this often; -1 = no limit",
                           "-1");
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<TauBakingTransformation>(opts);
}

static options::PluginShared<TaskTransformation> _plugin("tau_baking", _parse);
}
