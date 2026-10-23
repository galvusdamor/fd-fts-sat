#include "merge_strategy_label_unify.h"

#include "factored_transition_system.h"

#include "../task_representation/label_equivalence_relation.h"
#include "../task_representation/labels.h"
#include "../task_representation/transition_system.h"
#include "../options/option_parser.h"
#include "../options/options.h"
#include "../options/plugin.h"
#include "../utils/memory.h"
#include "../utils/rng.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <unordered_set>

using namespace std;

namespace task_transformation {
MergeStrategyLabelUnify::MergeStrategyLabelUnify(const FactoredTransitionSystem &fts, int max_states,
                                                 int min_saved, int max_bucket, bool require_exclusive)
    : MergeStrategy(fts), max_states(max_states), min_saved(min_saved), max_bucket(max_bucket),
      require_exclusive(require_exclusive) {
}

pair<int, int> MergeStrategyLabelUnify::get_next(const vector<int> &allowed_indices) {
    const int F = fts.get_size();
    const int L = fts.get_labels().get_size();
    vector<char> allowed(F, allowed_indices.empty() ? 1 : 0);
    for (int i : allowed_indices) allowed[i] = 1;

    // group[f][l]: l's label group in factor f (labels of one group have the same transitions
    // there), -1 if l is not a current label; moving[l]: factors l has a non-self-loop in
    vector<vector<int>> group(F);
    vector<vector<int>> moving(L);
    // require_exclusive: per factor and label group, the targets of its moving transitions and
    // the sources of all its transitions
    vector<map<int, pair<vector<int>, vector<int>>>> ends(F);
    vector<char> current(L, 0);
    for (int f = 0; f < F; ++f) {
        if (!fts.is_active(f)) continue;
        group[f].assign(L, -1);
        const task_representation::TransitionSystem &ts = fts.get_ts(f);
        for (const task_representation::GroupAndTransitions &gat : ts) {
            bool mv = false;
            for (const task_representation::Transition &t : gat.transitions)
                if (t.src != t.target) { mv = true; break; }
            if (require_exclusive && mv) {
                auto &e = ends[f][gat.group_id];
                for (const task_representation::Transition &t : gat.transitions) {
                    if (t.src != t.target) e.first.push_back(t.target);
                    e.second.push_back(t.src);
                }
                sort(e.first.begin(), e.first.end());
                e.first.erase(unique(e.first.begin(), e.first.end()), e.first.end());
                sort(e.second.begin(), e.second.end());
                e.second.erase(unique(e.second.begin(), e.second.end()), e.second.end());
            }
            for (int label : gat.label_group) {
                group[f][label] = gat.group_id;
                current[label] = 1;
                if (mv) moving[label].push_back(f);
            }
        }
    }
    // a random 64-bit key per (factor, group); a label's signature is the sum over factors
    std::mt19937_64 rng(4711);
    vector<map<int, unsigned long long>> key(F);
    auto k = [&](int f, int g) -> unsigned long long {
        auto it = key[f].find(g);
        if (it != key[f].end()) return it->second;
        return key[f][g] = rng();
    };
    vector<unsigned long long> sig(L, 0);
    for (int l = 0; l < L; ++l) {
        if (!current[l]) continue;
        for (int f = 0; f < F; ++f)
            if (fts.is_active(f)) sig[l] += k(f, group[f][l]);
    }

    // factor pairs that some two labels sharing a moving factor differ in exactly
    vector<vector<int>> bucket(F);
    for (int l = 0; l < L; ++l)
        if (current[l])
            for (int f : moving[l]) bucket[f].push_back(l);
    map<pair<int, int>, long long> candidates;
    for (int f = 0; f < F; ++f) {
        const vector<int> &b = bucket[f];
        if ((int)b.size() > max_bucket) continue;
        for (size_t i = 0; i < b.size(); ++i) {
            for (size_t j = i + 1; j < b.size(); ++j) {
                const int l1 = b[i], l2 = b[j];
                // only count a pair once: at the smallest moving factor both share
                bool first = true;
                for (int g : moving[l1])
                    if (g < f && find(moving[l2].begin(), moving[l2].end(), g) != moving[l2].end()) {
                        first = false; break;
                    }
                if (!first) continue;
                int d1 = -1, d2 = -1, n = 0;
                for (int g = 0; g < F && n <= 2; ++g) {
                    if (!fts.is_active(g) || group[g][l1] == group[g][l2]) continue;
                    if (n == 0) d1 = g; else if (n == 1) d2 = g;
                    ++n;
                }
                if (n == 2 && allowed[d1] && allowed[d2]) {
                    if (require_exclusive) {
                        // a shared moving factor where neither can follow the other
                        auto blocks = [&](int g, int x, int y) {
                            // no transition of y starts where x's moving transitions end
                            const auto &tx = ends[g][group[g][x]].first;
                            const auto &sy = ends[g][group[g][y]].second;
                            for (int s : tx)
                                if (binary_search(sy.begin(), sy.end(), s)) return false;
                            return true;
                        };
                        bool exclusive = false;
                        for (int g : moving[l1])
                            if (find(moving[l2].begin(), moving[l2].end(), g) != moving[l2].end() &&
                                blocks(g, l1, l2) && blocks(g, l2, l1)) { exclusive = true; break; }
                        if (!exclusive) continue;
                    }
                    ++candidates[make_pair(d1, d2)];
                }
            }
        }
    }

    // labels saved by merging (a, b): current labels minus distinct signatures outside {a, b}
    int L_current = 0;
    for (int l = 0; l < L; ++l) L_current += current[l];
    long long best_saved = 0, best_size = 0;
    pair<int, int> best(-1, -1);
    for (const auto &c : candidates) {
        const int a = c.first.first, b = c.first.second;
        const long long size = (long long)fts.get_ts(a).get_size() * fts.get_ts(b).get_size();
        if (size > max_states) continue;
        unordered_set<unsigned long long> rest;
        rest.reserve(L_current);
        for (int l = 0; l < L; ++l)
            if (current[l]) rest.insert(sig[l] - k(a, group[a][l]) - k(b, group[b][l]));
        const long long saved = L_current - (long long)rest.size();
        if (saved > best_saved || (saved == best_saved && saved > 0 && size < best_size)) {
            best_saved = saved; best_size = size; best = c.first;
        }
    }
    if (best_saved < min_saved) {
        cout << "LABELUNIFY merges " << merges << " saved " << saved_total << " candidates "
             << candidates.size() << " (no pair saves " << min_saved << " labels within "
             << max_states << " states)" << endl;
        return make_pair(-1, -1);
    }
    ++merges;
    saved_total += best_saved;
    cout << "LABELUNIFY merge " << best.first << " x " << best.second << " size " << best_size
         << " saves " << best_saved << " of " << L_current << " labels" << endl;
    return best;
}

MergeStrategyFactoryLabelUnify::MergeStrategyFactoryLabelUnify(const options::Options &opts)
    : MergeStrategyFactory(), max_states(opts.get<int>("max_states")),
      min_saved(opts.get<int>("min_saved")), max_bucket(opts.get<int>("max_bucket")),
      require_exclusive(opts.get<bool>("require_exclusive")) {
}

unique_ptr<MergeStrategy> MergeStrategyFactoryLabelUnify::compute_merge_strategy(
    const FTSTask &, const FactoredTransitionSystem &fts) {
    return utils::make_unique_ptr<MergeStrategyLabelUnify>(fts, max_states, min_saved, max_bucket,
                                                           require_exclusive);
}

string MergeStrategyFactoryLabelUnify::name() const {
    return "label unify";
}

void MergeStrategyFactoryLabelUnify::dump_strategy_specific_options() const {
    cout << "Maximum product size: " << max_states << ", minimum labels saved: " << min_saved
         << ", maximum bucket: " << max_bucket << endl;
}

static shared_ptr<MergeStrategyFactory> _parse(options::OptionParser &parser) {
    parser.document_synopsis(
        "Label-unifying merge strategy",
        "Merges the factor pair whose merge lets exact label reduction unify the most labels "
        "(labels differing in exactly those two factors); stops when no pair within max_states "
        "saves at least min_saved labels. Needs label reduction after merging "
        "(label_reduction=exact(before_shrinking=true)).");
    parser.add_option<int>("max_states", "maximum number of states of a product", "1000");
    parser.add_option<int>("min_saved", "minimum number of labels a merge must save", "1");
    parser.add_option<int>("max_bucket",
                           "factors moved by more labels than this are not used to find label pairs",
                           "3000");
    parser.add_option<bool>("require_exclusive",
                            "only unify labels that cannot fire in the same time step anyway "
                            "(see the header)", "false");
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<MergeStrategyFactoryLabelUnify>(opts);
}

static options::PluginShared<MergeStrategyFactory> _plugin("merge_label_unify", _parse);
}
