#include "merge_strategy_local_guard.h"

#include "factored_transition_system.h"

#include "../task_representation/label_equivalence_relation.h"
#include "../task_representation/labels.h"
#include "../task_representation/transition_system.h"
#include "../options/option_parser.h"
#include "../options/options.h"
#include "../options/plugin.h"
#include "../utils/memory.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <iostream>
#include <limits>

using namespace std;

namespace task_transformation {
MergeStrategyLocalGuard::MergeStrategyLocalGuard(const FactoredTransitionSystem &fts, int max_states,
                                                 int max_dependents)
    : MergeStrategy(fts), max_states(max_states), max_dependents(max_dependents) {
}

vector<pair<int, int>> MergeStrategyLocalGuard::scan(const vector<int> &allowed_indices) const {
    const int L = fts.get_labels().get_size();
    // moves[l]: number of factors in which l has a non-self-loop transition
    // own[f]: the labels that move f
    // guarded[l]: factors in which l is not a self-loop in every state
    vector<int> moves(L, 0);
    vector<vector<int>> own(fts.get_size()), guarded(L);
    for (int f = 0; f < fts.get_size(); ++f) {
        if (!fts.is_active(f))
            continue;
        const task_representation::TransitionSystem &ts = fts.get_ts(f);
        for (const task_representation::GroupAndTransitions &gat : ts) {
            bool moving = false;
            int loops = 0;
            for (const task_representation::Transition &t : gat.transitions) {
                if (t.src != t.target) moving = true;
                else ++loops;
            }
            const bool noop = !moving && loops == ts.get_size();
            for (int label : gat.label_group) {
                if (moving) { ++moves[label]; own[f].push_back(label); }
                if (!noop) guarded[label].push_back(f);
            }
        }
    }
    vector<char> allowed;
    if (!allowed_indices.empty()) {
        allowed.assign(fts.get_size(), 0);
        for (int i : allowed_indices) allowed[i] = 1;
    }

    vector<pair<long long, pair<int, int>>> candidates;
    vector<int> dependents(fts.get_size(), 0);   // local single-guard factors per guard
    for (int i = 0; i < fts.get_size(); ++i) {
        if (!fts.is_active(i) || own[i].empty() || (!allowed.empty() && !allowed[i]))
            continue;
        int guard = -1;
        bool ok = true;
        for (int label : own[i]) {
            if (moves[label] != 1) { ok = false; break; }
            for (int g : guarded[label]) {
                if (g == i) continue;
                if (guard == -1) guard = g;
                else if (guard != g) { ok = false; break; }
            }
            if (!ok) break;
        }
        if (!ok || guard == -1)
            continue;
        ++dependents[guard];
        if (!allowed.empty() && !allowed[guard])
            continue;
        const long long size = (long long)fts.get_ts(i).get_size() * fts.get_ts(guard).get_size();
        if (getenv("FTS_GUARD_DEBUG")) {
            cout << "GUARDDBG candidate " << i << " (" << fts.get_ts(i).get_size() << " states) guard "
                 << guard << " (" << fts.get_ts(guard).get_size() << " states) labels";
            for (int label : own[i]) cout << " " << label;
            cout << endl;
        }
        if (size <= max_states)
            candidates.emplace_back(size, make_pair(guard, i));
    }
    if (max_dependents >= 1) {
        vector<pair<long long, pair<int, int>>> kept;
        for (const auto &c : candidates)
            if (dependents[c.second.first] <= max_dependents)
                kept.push_back(c);
        candidates.swap(kept);
    }
    sort(candidates.begin(), candidates.end());
    vector<char> used(fts.get_size(), 0);
    vector<pair<int, int>> result;
    for (const auto &c : candidates) {
        const int g = c.second.first, i = c.second.second;
        if (used[g] || used[i]) continue;
        used[g] = used[i] = 1;
        result.push_back(c.second);
    }
    reverse(result.begin(), result.end());   // get_next pops from the back: smallest first
    return result;
}

pair<int, int> MergeStrategyLocalGuard::get_next(const vector<int> &allowed_indices) {
    // current index of every active transition system
    map<const task_representation::TransitionSystem *, int> index_of;
    for (int f = 0; f < fts.get_size(); ++f)
        if (fts.is_active(f))
            index_of[&fts.get_ts(f)] = f;
    for (int round = 0; round < 2; ++round) {
        while (!queue.empty()) {
            auto next = queue.back();
            queue.pop_back();
            auto g = index_of.find(next.first), i = index_of.find(next.second);
            if (g != index_of.end() && i != index_of.end())
                return make_pair(g->second, i->second);
        }
        if (round == 0)
            for (const auto &p : scan(allowed_indices))
                queue.emplace_back(&fts.get_ts(p.first), &fts.get_ts(p.second));
    }
    cout << "merge_local_guard: no local factor with a single guard within " << max_states
         << " states" << endl;
    return make_pair(-1, -1);
}

MergeStrategyFactoryLocalGuard::MergeStrategyFactoryLocalGuard(const options::Options &opts)
    : MergeStrategyFactory(), max_states(opts.get<int>("max_states")),
      max_dependents(opts.get<int>("max_dependents")) {
}

unique_ptr<MergeStrategy> MergeStrategyFactoryLocalGuard::compute_merge_strategy(
    const FTSTask &, const FactoredTransitionSystem &fts) {
    return utils::make_unique_ptr<MergeStrategyLocalGuard>(fts, max_states, max_dependents);
}

string MergeStrategyFactoryLocalGuard::name() const {
    return "local guard";
}

void MergeStrategyFactoryLocalGuard::dump_strategy_specific_options() const {
    cout << "Maximum product size: " << max_states << ", maximum dependents per guard: "
         << max_dependents << endl;
}

static shared_ptr<MergeStrategyFactory> _parse(options::OptionParser &parser) {
    parser.document_synopsis(
        "Local-guard merge strategy",
        "Merges a factor whose moving labels move only that factor and have preconditions in "
        "exactly one other factor (its guard) into that guard, so that the labels become tau "
        "labels of the product; stops when no such pair fits into max_states.");
    parser.add_option<int>("max_states", "maximum number of states of a product", "1000");
    parser.add_option<int>("max_dependents",
                           "only merge into a guard on which at most this many local factors depend; "
                           "-1 = no limit", "-1");
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<MergeStrategyFactoryLocalGuard>(opts);
}

static options::PluginShared<MergeStrategyFactory> _plugin("merge_local_guard", _parse);
}
