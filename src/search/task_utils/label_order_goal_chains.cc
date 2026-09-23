#include "label_order_finder.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <climits>
#include <unordered_map>

#include "../sat/kissat.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/transition_system.h"
#include "../option_parser.h"
#include "../plugin.h"
#include "../utils/system.h"

using namespace std;
using task_representation::FTSTask;
using task_representation::LabelID;
using task_representation::TransitionSystem;

namespace label_order_finder {

    namespace {
        // --------------------------------------------------------------
        // Abstract plans: BFS in the explicit product of a few factors
        // --------------------------------------------------------------

        // Labels that move factor f somewhere (have a non-self-loop transition).
        vector<int> moving_labels(const TransitionSystem &ts, int num_labels) {
            vector<int> res;
            for (int l = 0; l < num_labels; l++) {
                // note: is_selfloop_everywhere only means "has a self-loop at
                // every state"; such a label may still move the factor
                if (!ts.is_relevant_label(LabelID(l))) continue;
                for (const auto &t : ts.get_transitions_with_label(l))
                    if (t.src != t.target) { res.push_back(l); break; }
            }
            return res;
        }

        enum class BFSResult { FOUND, NO_PLAN, TOO_BIG };

        /*
          Shortest label sequence in the product of `factors` from the initial
          state to a state where every goal-relevant included factor is in a
          goal state. Labels that move none of the factors are useless here and
          are not considered; labels that self-loop everywhere in a factor do
          not constrain it -- only labels that are nothing *but* self-loops
          (isIrrelevantLabel) leave a factor unconstrained.
        */
        // goal_ok[i][s]: state s of factors[i] is acceptable at the end; an
        // empty goal_ok[i] means factors[i] is unconstrained.
        BFSResult product_bfs(const FTSTask &task, const vector<int> &factors,
                              const vector<vector<int>> &moving, int max_states,
                              const vector<vector<char>> &goal_ok,
                              vector<int> &plan) {
            const int k = factors.size();
            vector<uint64_t> radix(k);
            uint64_t mult = 1;
            for (int i = 0; i < k; i++) {
                radix[i] = mult;
                const uint64_t sz = task.get_ts(factors[i]).get_size();
                if (mult > (uint64_t(1) << 62) / sz) return BFSResult::TOO_BIG;
                mult *= sz;
            }

            vector<int> labels;
            for (int f : factors) labels.insert(labels.end(), moving[f].begin(), moving[f].end());
            sort(labels.begin(), labels.end());
            labels.erase(unique(labels.begin(), labels.end()), labels.end());

            // succ[li][i][s] = targets of label labels[li] from state s of factor i;
            // unconstrained[li][i] if that label self-loops everywhere there
            const int L = labels.size();
            vector<vector<vector<vector<int>>>> succ(L, vector<vector<vector<int>>>(k));
            vector<vector<char>> unconstrained(L, vector<char>(k, 0));
            for (int li = 0; li < L; li++) {
                for (int i = 0; i < k; i++) {
                    const TransitionSystem &ts = task.get_ts(factors[i]);
                    if (ts.isIrrelevantLabel(labels[li])) { unconstrained[li][i] = 1; continue; }
                    succ[li][i].assign(ts.get_size(), {});
                    for (const auto &t : ts.get_transitions_with_label(labels[li]))
                        succ[li][i][t.src].push_back(t.target);
                }
            }

            auto is_goal = [&](const vector<int> &st) {
                for (int i = 0; i < k; i++)
                    if (!goal_ok[i].empty() && !goal_ok[i][st[i]]) return false;
                return true;
            };
            auto encode = [&](const vector<int> &st) {
                uint64_t c = 0;
                for (int i = 0; i < k; i++) c += radix[i] * st[i];
                return c;
            };
            auto decode = [&](uint64_t c, vector<int> &st) {
                for (int i = k - 1; i >= 0; i--) { st[i] = c / radix[i]; c %= radix[i]; }
            };

            vector<int> init(k);
            for (int i = 0; i < k; i++) init[i] = task.get_ts(factors[i]).get_init_state();
            plan.clear();
            if (is_goal(init)) return BFSResult::FOUND;

            unordered_map<uint64_t, pair<uint64_t, int>> parent;   // state -> (parent, label)
            deque<uint64_t> queue;
            const uint64_t c0 = encode(init);
            parent.emplace(c0, make_pair(c0, -1));
            queue.push_back(c0);
            vector<int> st(k), nxt(k);
            while (!queue.empty()) {
                const uint64_t c = queue.front(); queue.pop_front();
                decode(c, st);
                for (int li = 0; li < L; li++) {
                    bool applicable = true;
                    for (int i = 0; i < k && applicable; i++)
                        if (!unconstrained[li][i] && succ[li][i][st[i]].empty()) applicable = false;
                    if (!applicable) continue;
                    // enumerate the (usually single) combination of targets
                    vector<int> idx(k, 0);
                    while (true) {
                        for (int i = 0; i < k; i++)
                            nxt[i] = unconstrained[li][i] ? st[i] : succ[li][i][st[i]][idx[i]];
                        const uint64_t cn = encode(nxt);
                        if (!parent.count(cn)) {
                            parent.emplace(cn, make_pair(c, labels[li]));
                            if (is_goal(nxt)) {
                                for (uint64_t x = cn; x != c0; x = parent[x].first)
                                    plan.push_back(parent[x].second);
                                reverse(plan.begin(), plan.end());
                                return BFSResult::FOUND;
                            }
                            if ((int)parent.size() > max_states) return BFSResult::TOO_BIG;
                            queue.push_back(cn);
                        }
                        int i = 0;
                        for (; i < k; i++) {
                            if (unconstrained[li][i]) continue;
                            if (++idx[i] < (int)succ[li][i][st[i]].size()) break;
                            idx[i] = 0;
                        }
                        if (i == k) break;
                    }
                }
            }
            return BFSResult::NO_PLAN;
        }

        /*
          Up to `max_plans` label sequences of length <= shortest + slack in
          the same product and with the same goal as product_bfs. Explores the
          product breadth-first to that depth (or until max_states), computes
          goal distances backwards over the explored edges and enumerates
          paths depth-first, never revisiting a state on the current path.
          The first plan returned is a shortest one, but not necessarily the
          one product_bfs finds.
        */
        BFSResult product_plans(const FTSTask &task, const vector<int> &factors,
                                const vector<vector<int>> &moving, int max_states,
                                const vector<vector<char>> &goal_ok, int max_plans, int slack,
                                vector<vector<int>> &plans) {
            plans.clear();
            const int k = factors.size();
            vector<uint64_t> radix(k);
            uint64_t mult = 1;
            for (int i = 0; i < k; i++) {
                radix[i] = mult;
                const uint64_t sz = task.get_ts(factors[i]).get_size();
                if (mult > (uint64_t(1) << 62) / sz) return BFSResult::TOO_BIG;
                mult *= sz;
            }
            vector<int> labels;
            for (int f : factors) labels.insert(labels.end(), moving[f].begin(), moving[f].end());
            sort(labels.begin(), labels.end());
            labels.erase(unique(labels.begin(), labels.end()), labels.end());
            const int L = labels.size();
            vector<vector<vector<vector<int>>>> succ(L, vector<vector<vector<int>>>(k));
            vector<vector<char>> unconstrained(L, vector<char>(k, 0));
            for (int li = 0; li < L; li++)
                for (int i = 0; i < k; i++) {
                    const TransitionSystem &ts = task.get_ts(factors[i]);
                    if (ts.isIrrelevantLabel(labels[li])) { unconstrained[li][i] = 1; continue; }
                    succ[li][i].assign(ts.get_size(), {});
                    for (const auto &t : ts.get_transitions_with_label(labels[li]))
                        succ[li][i][t.src].push_back(t.target);
                }
            auto is_goal = [&](const vector<int> &st) {
                for (int i = 0; i < k; i++)
                    if (!goal_ok[i].empty() && !goal_ok[i][st[i]]) return false;
                return true;
            };
            auto encode = [&](const vector<int> &st) {
                uint64_t c = 0;
                for (int i = 0; i < k; i++) c += radix[i] * st[i];
                return c;
            };
            auto decode = [&](uint64_t c, vector<int> &st) {
                for (int i = k - 1; i >= 0; i--) { st[i] = c / radix[i]; c %= radix[i]; }
            };
            vector<int> init(k);
            for (int i = 0; i < k; i++) init[i] = task.get_ts(factors[i]).get_init_state();
            if (is_goal(init)) { plans.push_back({}); return BFSResult::FOUND; }

            // forward BFS, keeping all edges between explored states
            unordered_map<uint64_t, int> id;              // state -> index
            vector<uint64_t> code;
            vector<int> depth;
            vector<char> goal;
            vector<vector<pair<int, int>>> out;           // (label, target index)
            auto intern = [&](uint64_t c, int d, bool g) {
                auto it = id.find(c);
                if (it != id.end()) return it->second;
                const int n = code.size();
                id.emplace(c, n); code.push_back(c); depth.push_back(d); goal.push_back(g); out.emplace_back();
                return n;
            };
            intern(encode(init), 0, false);
            int bound = INT_MAX;
            vector<int> st(k), nxt(k);
            bool truncated = false;
            for (size_t qi = 0; qi < code.size(); qi++) {
                if (depth[qi] >= bound) break;
                if (goal[qi]) continue;          // paths end at the first goal state
                decode(code[qi], st);
                for (int li = 0; li < L; li++) {
                    bool applicable = true;
                    for (int i = 0; i < k && applicable; i++)
                        if (!unconstrained[li][i] && succ[li][i][st[i]].empty()) applicable = false;
                    if (!applicable) continue;
                    vector<int> idx(k, 0);
                    while (true) {
                        for (int i = 0; i < k; i++)
                            nxt[i] = unconstrained[li][i] ? st[i] : succ[li][i][st[i]][idx[i]];
                        const uint64_t cn = encode(nxt);
                        if (cn != code[qi]) {
                            const bool g = is_goal(nxt);
                            const int t = intern(cn, depth[qi] + 1, g);
                            out[qi].push_back({labels[li], t});
                            if (g && bound == INT_MAX) bound = depth[qi] + 1 + slack;
                        }
                        int i = 0;
                        for (; i < k; i++) {
                            if (unconstrained[li][i]) continue;
                            if (++idx[i] < (int)succ[li][i][st[i]].size()) break;
                            idx[i] = 0;
                        }
                        if (i == k) break;
                    }
                }
                if ((int)code.size() > max_states) { truncated = true; break; }
            }
            if (bound == INT_MAX) return truncated ? BFSResult::TOO_BIG : BFSResult::NO_PLAN;

            // backward goal distances over the explored edges
            const int N = code.size();
            vector<vector<int>> in(N);
            for (int v = 0; v < N; v++) for (auto [l, t] : out[v]) in[t].push_back(v);
            vector<int> dist(N, INT_MAX);
            deque<int> q;
            for (int v = 0; v < N; v++) if (goal[v]) { dist[v] = 0; q.push_back(v); }
            while (!q.empty()) {
                const int v = q.front(); q.pop_front();
                for (int u : in[v]) if (dist[u] == INT_MAX) { dist[u] = dist[v] + 1; q.push_back(u); }
            }
            // depth-first enumeration of paths of length <= bound, shortest-first per branch
            vector<int> path;
            vector<char> on_path(N, 0);
            long budget = 200000;   // edge visits
            function<void(int)> dfs = [&](int v) {
                if ((int)plans.size() >= max_plans || --budget < 0) return;
                if (goal[v]) { plans.push_back(path); return; }
                vector<pair<int, pair<int, int>>> cand;   // (dist, (label, target))
                for (auto [l, t] : out[v])
                    if (!on_path[t] && dist[t] != INT_MAX && (int)path.size() + 1 + dist[t] <= bound)
                        cand.push_back({dist[t], {l, t}});
                stable_sort(cand.begin(), cand.end(),
                            [](const auto &a, const auto &b) { return a.first < b.first; });
                for (auto &c : cand) {
                    on_path[c.second.second] = 1; path.push_back(c.second.first);
                    dfs(c.second.second);
                    path.pop_back(); on_path[c.second.second] = 0;
                    if ((int)plans.size() >= max_plans || budget < 0) return;
                }
            };
            on_path[0] = 1;
            dfs(0);
            return plans.empty() ? BFSResult::NO_PLAN : BFSResult::FOUND;
        }

        // --------------------------------------------------------------
        // Merging chains: weighted linear ordering / feedback arc set
        // --------------------------------------------------------------

        using Weights = map<pair<int, int>, int>;   // (a,b) -> times b directly follows a

        // cost of an order: total weight of pairs it puts backwards
        long cost_of(const vector<int> &order, const Weights &w) {
            unordered_map<int, int> pos;
            for (size_t i = 0; i < order.size(); i++) pos[order[i]] = i;
            long c = 0;
            for (const auto &[e, wt] : w) if (pos[e.first] > pos[e.second]) c += wt;
            return c;
        }

        // Eades-Lin-Smyth on the vertex set `vs` (weights restricted to it),
        // then insertion local search.
        vector<int> heuristic_order(const vector<int> &vs, const Weights &w) {
            const int n = vs.size();
            unordered_map<int, int> id;
            for (int i = 0; i < n; i++) id[vs[i]] = i;
            vector<vector<pair<int, int>>> out(n), in(n);
            for (const auto &[e, wt] : w) {
                auto a = id.find(e.first), b = id.find(e.second);
                if (a == id.end() || b == id.end()) continue;
                out[a->second].push_back({b->second, wt});
                in[b->second].push_back({a->second, wt});
            }
            vector<long> wout(n, 0), win(n, 0);
            for (int v = 0; v < n; v++) {
                for (auto [u, wt] : out[v]) wout[v] += wt;
                for (auto [u, wt] : in[v]) win[v] += wt;
            }
            vector<char> gone(n, 0);
            vector<int> left, right;
            int remaining = n;
            auto remove = [&](int v) {
                gone[v] = 1; remaining--;
                for (auto [u, wt] : out[v]) if (!gone[u]) win[u] -= wt;
                for (auto [u, wt] : in[v]) if (!gone[u]) wout[u] -= wt;
            };
            while (remaining > 0) {
                bool changed = true;
                while (changed) {
                    changed = false;
                    for (int v = 0; v < n; v++) if (!gone[v] && wout[v] == 0) { right.push_back(v); remove(v); changed = true; }
                    for (int v = 0; v < n; v++) if (!gone[v] && win[v] == 0) { left.push_back(v); remove(v); changed = true; }
                }
                if (remaining == 0) break;
                int best = -1;
                for (int v = 0; v < n; v++)
                    if (!gone[v] && (best < 0 || wout[v] - win[v] > wout[best] - win[best])) best = v;
                left.push_back(best); remove(best);
            }
            vector<int> ord = left;
            ord.insert(ord.end(), right.rbegin(), right.rend());

            // insertion local search: move one vertex to its best position
            if (n <= 5000) {
                vector<vector<int>> wmat;   // only neighbours matter; use maps
                for (int pass = 0; pass < 20; pass++) {
                    bool improved = false;
                    for (int v = 0; v < n; v++) {
                        vector<int> pos(n);
                        for (int i = 0; i < n; i++) pos[ord[i]] = i;
                        const int p = pos[v];
                        // delta[i] = cost change of moving v to position i
                        // contribution of neighbour u: edge v->u violated iff pos(u) < pos(v)
                        vector<long> delta_at(n + 1, 0);   // difference array over target positions
                        long base = 0;
                        auto add_range = [&](int lo, int hi, long val) {   // positions [lo,hi)
                            if (lo >= hi) return;
                            delta_at[lo] += val; delta_at[hi] -= val;
                        };
                        for (auto [u, wt] : out[v]) {
                            const int q = pos[u];
                            // v placed at i (in the order without v): violated iff q' < i
                            const int qq = q < p ? q : q - 1;
                            add_range(qq + 1, n, wt);
                            if (q < p) base += wt;
                        }
                        for (auto [u, wt] : in[v]) {
                            const int q = pos[u];
                            const int qq = q < p ? q : q - 1;
                            add_range(0, qq + 1, wt);
                            if (q > p) base += wt;
                        }
                        long run = 0, bestc = base; int bestpos = -1;
                        for (int i = 0; i < n; i++) {
                            run += delta_at[i];
                            if (run < bestc) { bestc = run; bestpos = i; }
                        }
                        if (bestpos >= 0) {
                            ord.erase(ord.begin() + p);
                            ord.insert(ord.begin() + bestpos, v);
                            improved = true;
                        }
                    }
                    if (!improved) break;
                }
            }
            vector<int> res;
            for (int v : ord) res.push_back(vs[v]);
            return res;
        }

        struct CNF {
            int nvars = 0;
            vector<vector<int>> clauses;
            int var() { return ++nvars; }
            void add(vector<int> c) { clauses.push_back(move(c)); }
            // outputs o[0..min(n,k+1)-1]: o[j] true iff at least j+1 inputs true
            vector<int> totalizer(const vector<int> &lits, int k) {
                if (lits.size() == 1) return {lits[0]};
                const size_t mid = lits.size() / 2;
                vector<int> a = totalizer(vector<int>(lits.begin(), lits.begin() + mid), k);
                vector<int> b = totalizer(vector<int>(lits.begin() + mid, lits.end()), k);
                const int m = min<int>(lits.size(), k + 1);
                vector<int> o(m);
                for (int j = 0; j < m; j++) o[j] = var();
                for (int i = 0; i <= (int)a.size(); i++)
                    for (int j = 0; j <= (int)b.size(); j++) {
                        if (i + j == 0) continue;
                        const int t = min(i + j, m);
                        vector<int> c = {o[t - 1]};
                        if (i) c.push_back(-a[i - 1]);
                        if (j) c.push_back(-b[j - 1]);
                        add(c);
                    }
                return o;
            }
        };

        /*
          Exact weighted linear ordering on `vs`, starting from `order` (a
          feasible solution whose cost is the first upper bound). Returns true
          if the result is proven optimal.
        */
        bool exact_order(const vector<int> &vs, const Weights &w, vector<int> &order,
                         int conflicts, double time_limit) {
            const auto start = chrono::steady_clock::now();
            const int n = vs.size();
            unordered_map<int, int> id;
            for (int i = 0; i < n; i++) id[vs[i]] = i;
            long ub = cost_of(order, w);
            if (ub == 0) return true;

            CNF base;
            vector<vector<int>> var(n, vector<int>(n, 0));
            for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) var[i][j] = base.var();
            auto bef = [&](int i, int j) { return i < j ? var[i][j] : -var[j][i]; };
            for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) for (int k = j + 1; k < n; k++) {
                base.add({-var[i][j], -var[j][k], var[i][k]});
                base.add({var[i][j], var[j][k], -var[i][k]});
            }
            vector<int> breaks;
            for (const auto &[e, wt] : w) {
                auto a = id.find(e.first), b = id.find(e.second);
                if (a == id.end() || b == id.end()) continue;
                for (int r = 0; r < wt; r++) breaks.push_back(-bef(a->second, b->second));
            }
            const vector<int> outputs = base.totalizer(breaks, ub);

            while (ub > 0) {
                const double elapsed = chrono::duration<double>(chrono::steady_clock::now() - start).count();
                if (elapsed > time_limit) return false;
                kissat *solver = kissat_init();
                kissat_set_option(solver, "quiet", 1);
                kissat_set_conflict_limit(solver, conflicts);
                for (const auto &c : base.clauses) { for (int l : c) kissat_add(solver, l); kissat_add(solver, 0); }
                kissat_add(solver, -outputs[ub - 1]); kissat_add(solver, 0);   // cost <= ub-1
                const int res = kissat_solve(solver);
                if (res == 20) { kissat_release(solver); return true; }
                if (res != 10) { kissat_release(solver); return false; }
                vector<int> before(n, 0);   // number of labels before label i
                for (int i = 0; i < n; i++) for (int j = 0; j < n; j++)
                    if (i != j && kissat_value(solver, abs(bef(j, i))) * (bef(j, i) > 0 ? 1 : -1) > 0) before[i]++;
                kissat_release(solver);
                vector<int> idx(n);
                iota(idx.begin(), idx.end(), 0);
                sort(idx.begin(), idx.end(), [&](int a, int b) { return before[a] < before[b]; });
                order.clear();
                for (int i : idx) order.push_back(vs[i]);
                const long c = cost_of(order, w);
                assert(c <= ub - 1);
                ub = c;
            }
            return true;
        }

        // Tarjan SCCs of the precedence graph, returned in topological order.
        vector<vector<int>> sccs_topological(const vector<int> &vs, const Weights &w) {
            const int n = vs.size();
            unordered_map<int, int> id;
            for (int i = 0; i < n; i++) id[vs[i]] = i;
            vector<vector<int>> adj(n);
            for (const auto &[e, wt] : w) adj[id[e.first]].push_back(id[e.second]);
            vector<int> index(n, -1), low(n, 0), stack;
            vector<char> on(n, 0);
            vector<vector<int>> comps;
            int counter = 0;
            // iterative Tarjan
            for (int s = 0; s < n; s++) {
                if (index[s] >= 0) continue;
                vector<pair<int, size_t>> call = {{s, 0}};
                index[s] = low[s] = counter++; stack.push_back(s); on[s] = 1;
                while (!call.empty()) {
                    auto &[v, it] = call.back();
                    if (it < adj[v].size()) {
                        const int u = adj[v][it++];
                        if (index[u] < 0) {
                            index[u] = low[u] = counter++; stack.push_back(u); on[u] = 1;
                            call.push_back({u, 0});
                        } else if (on[u]) low[v] = min(low[v], index[u]);
                    } else {
                        if (low[v] == index[v]) {
                            vector<int> comp;
                            int u;
                            do { u = stack.back(); stack.pop_back(); on[u] = 0; comp.push_back(vs[u]); } while (u != v);
                            comps.push_back(comp);
                        }
                        const int done = v;
                        call.pop_back();
                        if (!call.empty()) low[call.back().first] = min(low[call.back().first], low[done]);
                    }
                }
            }
            reverse(comps.begin(), comps.end());   // Tarjan emits sinks first
            return comps;
        }
    }

    LabelOrderFinderGoalChains::LabelOrderFinderGoalChains(const options::Options &opts)
        : max_states(opts.get<int>("max_states")),
          exact_max_size(opts.get<int>("exact_max_size")),
          exact_conflicts(opts.get<int>("exact_conflicts")),
          exact_time_limit(opts.get<double>("exact_time_limit")),
          leftover(opts.get<shared_ptr<LabelOrderFinder>>("leftover")),
          verbose(opts.get<bool>("verbose")),
          ancestor_depth(opts.get<int>("ancestor_depth")),
          subgoal_depth(opts.get<int>("subgoal_depth")),
          plans_per_goal(opts.get<int>("plans_per_goal")),
          plan_slack(opts.get<int>("plan_slack")),
          selection_rounds(opts.get<int>("selection_rounds")),
          leftover_support(opts.get<bool>("leftover_support")) {
        if (plans_per_goal < 1) {
            cerr << "label_order_goal_chains: plans_per_goal must be >= 1" << endl;
            utils::exit_with(utils::ExitCode::INPUT_ERROR);
        }
    }

    namespace {
        struct MergeStats {
            long chain_labels = 0, violated = 0, total_weight = 0;
            int sccs = 0, largest = 0, exact_comps = 0, proven = 0, heuristic_comps = 0;
        };

        /*
          Merge label chains into one order of the labels they mention:
          minimise backwards consecutive pairs. Components of the precedence
          graph in Kahn order, ties by (chain index, position) so chains stay
          contiguous -- by position alone every chain's first label would come
          first, then every second label, which is layering again.
        */
        vector<int> merge_chains(const vector<vector<int>> &chains, int num_labels,
                                 int exact_max_size, int exact_conflicts, double exact_time_limit,
                                 MergeStats &ms) {
            Weights w;
            vector<long> first_pos(num_labels, -1);
            long stride = 1;
            for (const auto &c : chains) stride = max<long>(stride, c.size());
            for (size_t ci = 0; ci < chains.size(); ci++) {
                const auto &c = chains[ci];
                ms.chain_labels += c.size();
                for (size_t i = 0; i < c.size(); i++) {
                    const long key = ci * stride + i;
                    if (first_pos[c[i]] < 0) first_pos[c[i]] = key;
                    if (i + 1 < c.size() && c[i] != c[i + 1]) w[{c[i], c[i + 1]}]++;
                }
            }
            vector<int> vs;
            for (int l = 0; l < num_labels; l++) if (first_pos[l] >= 0) vs.push_back(l);

            vector<vector<int>> comps = sccs_topological(vs, w);
            {
                vector<int> comp_of(num_labels, -1);
                for (size_t c = 0; c < comps.size(); c++) for (int l : comps[c]) comp_of[l] = c;
                const int C = comps.size();
                vector<vector<int>> succ(C);
                vector<int> indeg(C, 0);
                vector<long> prio(C, LONG_MAX);
                for (int c = 0; c < C; c++) for (int l : comps[c]) prio[c] = min(prio[c], first_pos[l]);
                set<pair<int, int>> seen;
                for (const auto &[e, wt] : w) {
                    const int a = comp_of[e.first], b = comp_of[e.second];
                    if (a != b && seen.insert({a, b}).second) { succ[a].push_back(b); indeg[b]++; }
                }
                set<pair<long, int>> ready;
                for (int c = 0; c < C; c++) if (!indeg[c]) ready.insert({prio[c], c});
                vector<vector<int>> sorted;
                while (!ready.empty()) {
                    const int c = ready.begin()->second; ready.erase(ready.begin());
                    sorted.push_back(comps[c]);
                    for (int d : succ[c]) if (--indeg[d] == 0) ready.insert({prio[d], d});
                }
                assert(sorted.size() == comps.size());
                comps.swap(sorted);
            }
            vector<int> order;
            for (auto &comp : comps) {
                ms.largest = max<int>(ms.largest, comp.size());
                if (comp.size() == 1) { order.push_back(comp[0]); continue; }
                Weights wc;
                {
                    vector<char> in(num_labels, 0);
                    for (int l : comp) in[l] = 1;
                    for (const auto &[e, wt] : w) if (in[e.first] && in[e.second]) wc[e] = wt;
                }
                vector<int> co = heuristic_order(comp, wc);
                if ((int)comp.size() <= exact_max_size) {
                    ms.exact_comps++;
                    if (exact_order(comp, wc, co, exact_conflicts, exact_time_limit)) ms.proven++;
                } else {
                    ms.heuristic_comps++;
                }
                order.insert(order.end(), co.begin(), co.end());
            }
            ms.sccs = comps.size();
            ms.violated = cost_of(order, w);
            for (const auto &[e, wt] : w) ms.total_weight += wt;
            return order;
        }

        long breaks_under(const vector<int> &chain, const vector<int> &pos) {
            long b = 0;
            for (size_t i = 0; i + 1 < chain.size(); i++)
                if (chain[i] == chain[i + 1] || pos[chain[i]] >= pos[chain[i + 1]]) b++;
            return b;
        }
    }

    vector<int> LabelOrderFinderGoalChains::find_order(const FTSTask &task) {
        const auto start = chrono::steady_clock::now();
        const int num_labels = task.get_num_labels();
        const int num_factors = task.get_size();

        vector<vector<int>> moving(num_factors);
        for (int f = 0; f < num_factors; f++) moving[f] = moving_labels(task.get_ts(f), num_labels);
        // factors a label has a precondition on, ascending
        vector<vector<int>> pre_factors(num_labels);
        for (int f = 0; f < num_factors; f++)
            for (int l = 0; l < num_labels; l++)
                if (task.get_ts(f).has_precondition_on(LabelID(l))) pre_factors[l].push_back(f);

        const bool multi = plans_per_goal > 1 || plan_slack > 0;

        // Ancestors of `root`: level 1 are the factors a label moving root has
        // a precondition on, strongest (most such labels) first; level d+1 the
        // same for the labels moving any level-d factor. Up to ancestor_depth.
        auto ancestors_of = [&](int root) {
            vector<int> anc;
            vector<char> in(num_factors, 0);
            in[root] = 1;
            vector<int> frontier = {root};
            for (int d = 0; d < ancestor_depth && !frontier.empty(); d++) {
                map<int, int> strength;
                for (int a : frontier)
                    for (int l : moving[a])
                        for (int f : pre_factors[l])
                            if (!in[f]) strength[f]++;
                vector<int> level;
                for (auto [f, st] : strength) level.push_back(f);
                stable_sort(level.begin(), level.end(), [&](int a, int b) { return strength[a] > strength[b]; });
                for (int f : level) { in[f] = 1; anc.push_back(f); }
                frontier = level;
            }
            return anc;
        };

        int too_big_drops = 0, no_plan = 0, too_big_alone = 0;
        long product_factors = 0;
        vector<char> covered(num_factors, 0);

        /*
          Solve one abstraction: `root` must end in a state accepted by
          root_ok (empty = its goal states); the other included factors must
          end in a goal state when `other_goals`, else are unconstrained.
          Returns the candidate plans (empty vector = nothing found) and the
          factors of the product actually used.
        */
        auto solve = [&](int root, const vector<char> &root_ok, bool other_goals,
                         vector<vector<int>> &plans, vector<int> &used) {
            const vector<int> anc = ancestors_of(root);
            auto goal_sets = [&](const vector<int> &factors) {
                vector<vector<char>> gs(factors.size());
                for (size_t i = 0; i < factors.size(); i++) {
                    const TransitionSystem &ts = task.get_ts(factors[i]);
                    if (i == 0 && !root_ok.empty()) { gs[i] = root_ok; continue; }
                    if (i == 0 || other_goals) {
                        gs[i].assign(ts.get_size(), 0);
                        for (int s = 0; s < ts.get_size(); s++) gs[i][s] = ts.is_goal_state(s);
                    }
                }
                return gs;
            };
            auto attempt = [&](int n, vector<vector<int>> &out_plans) {
                vector<int> factors = {root};
                factors.insert(factors.end(), anc.begin(), anc.begin() + n);
                const vector<vector<char>> gs = goal_sets(factors);
                if (multi)
                    return product_plans(task, factors, moving, max_states, gs, plans_per_goal, plan_slack, out_plans);
                vector<int> plan;
                const BFSResult r = product_bfs(task, factors, moving, max_states, gs, plan);
                out_plans.clear();
                if (r == BFSResult::FOUND) out_plans.push_back(plan);
                return r;
            };
            vector<vector<int>> cur, best;
            int best_n = -1;
            BFSResult r = attempt(anc.size(), cur);
            if (r == BFSResult::FOUND) { best_n = anc.size(); best = cur; }
            else if (r == BFSResult::TOO_BIG) {
                int lo = 0, hi = anc.size() - 1;
                while (lo <= hi) {
                    const int mid = (lo + hi) / 2;
                    const BFSResult rm = attempt(mid, cur);
                    too_big_drops++;
                    if (rm == BFSResult::FOUND) { best_n = mid; best = cur; lo = mid + 1; }
                    else if (rm == BFSResult::TOO_BIG) hi = mid - 1;
                    else { r = rm; break; }
                }
            }
            plans.clear(); used.clear();
            if (best_n >= 0) {
                plans = best;
                used = {root};
                used.insert(used.end(), anc.begin(), anc.begin() + best_n);
                product_factors += best_n + 1;
                for (int f : used) covered[f] = 1;
                return true;
            }
            if (r == BFSResult::NO_PLAN) no_plan++; else too_big_alone++;
            return false;
        };

        // ---- chains: goal chains, each preceded by its subgoal chains ----
        struct Chain { vector<vector<int>> alternatives; int selected = 0; };
        vector<Chain> chains;
        int goal_factors = 0, subgoal_chains = 0;
        set<pair<int, vector<int>>> subgoals_seen;
        // chains for the subgoals of `plan` (computed on factor set `used`),
        // recursively, appended before the caller appends its own chain
        function<void(const vector<int> &, const vector<int> &, int)> add_subgoals =
            [&](const vector<int> &plan, const vector<int> &used, int depth) {
            if (depth >= subgoal_depth) return;
            vector<char> in(num_factors, 0);
            for (int f : used) in[f] = 1;
            for (int l : plan)
                for (int f : pre_factors[l]) {
                    if (in[f]) continue;
                    const TransitionSystem &ts = task.get_ts(f);
                    const vector<int> &pre = ts.get_label_precondition(LabelID(l));
                    if (!subgoals_seen.insert({f, pre}).second) continue;
                    vector<char> ok(ts.get_size(), 0);
                    for (int s : pre) ok[s] = 1;
                    if (ok[ts.get_init_state()]) continue;
                    vector<vector<int>> sp; vector<int> su;
                    if (!solve(f, ok, false, sp, su) || sp[0].empty()) continue;
                    add_subgoals(sp[0], su, depth + 1);
                    chains.push_back({sp, 0});
                    subgoal_chains++;
                }
        };
        for (int g = 0; g < num_factors; g++) {
            const TransitionSystem &tg = task.get_ts(g);
            if (!tg.is_goal_relevant()) continue;
            goal_factors++;
            vector<vector<int>> plans; vector<int> used;
            if (!solve(g, {}, true, plans, used)) {
                if (verbose)
                    cout << "GOALCHAINS no abstract plan for factor " << g << " (" << tg.get_size()
                         << " states, " << tg.get_goal_states().size() << " goal states)" << endl;
                continue;
            }
            if (plans[0].empty()) continue;
            add_subgoals(plans[0], used, 0);
            chains.push_back({plans, 0});
        }
        const double chain_time = chrono::duration<double>(chrono::steady_clock::now() - start).count();

        // ---- merge, then choose among alternative plans ----
        auto selected_chains = [&]() {
            vector<vector<int>> sel;
            for (const auto &c : chains) sel.push_back(c.alternatives[c.selected]);
            return sel;
        };
        vector<int> leftover_order = leftover->find_order(task);
        auto complete = [&](vector<int> order) {
            vector<char> placed(num_labels, 0);
            for (int l : order) placed[l] = 1;
            if (leftover_support) {
                /*
                  Balyo-style: walk the chain order and, before each chain
                  label, emit (depth-first, post-order) the leftover labels
                  that support it -- produce a state it has a precondition on
                  -- and their own leftover supporters.
                */
                vector<vector<int>> producers_of;   // per (factor, state), flattened
                vector<int> offset(num_factors + 1, 0);
                for (int f = 0; f < num_factors; f++) offset[f + 1] = offset[f] + task.get_ts(f).get_size();
                producers_of.assign(offset[num_factors], {});
                for (int f = 0; f < num_factors; f++)
                    for (int l : moving[f])
                        if (!placed[l])
                            for (const auto &t : task.get_ts(f).get_transitions_with_label(l))
                                if (t.src != t.target) producers_of[offset[f] + t.target].push_back(l);
                for (auto &v : producers_of) { sort(v.begin(), v.end()); v.erase(unique(v.begin(), v.end()), v.end()); }
                vector<char> emitted(num_labels, 0);
                vector<int> result;
                function<void(int)> visit = [&](int l) {
                    for (int f : pre_factors[l])
                        for (int s : task.get_ts(f).get_label_precondition(LabelID(l)))
                            for (int p : producers_of[offset[f] + s])
                                if (!emitted[p]) { emitted[p] = 1; visit(p); result.push_back(p); }
                };
                for (int l : order) { emitted[l] = 1; }
                for (int l : order) { visit(l); result.push_back(l); }
                for (int l : result) placed[l] = 1;
                order = result;
            }
            for (int l : leftover_order) if (!placed[l]) order.push_back(l);
            return order;
        };

        MergeStats ms;
        vector<int> merged = merge_chains(selected_chains(), num_labels, exact_max_size,
                                          exact_conflicts, exact_time_limit, ms);
        vector<int> order = complete(merged);
        int rounds = 0, switches = 0;
        long alternatives = 0;
        for (const auto &c : chains) alternatives += c.alternatives.size();
        if (multi) {
            for (; rounds < selection_rounds; rounds++) {
                vector<int> pos(num_labels);
                for (size_t i = 0; i < order.size(); i++) pos[order[i]] = i;
                bool changed = false;
                for (auto &c : chains) {
                    int best = c.selected;
                    long bestb = breaks_under(c.alternatives[best], pos);
                    for (size_t a = 0; a < c.alternatives.size(); a++) {
                        const long b = breaks_under(c.alternatives[a], pos);
                        if (b < bestb) { bestb = b; best = a; }
                    }
                    if (best != c.selected) { c.selected = best; changed = true; switches++; }
                }
                if (!changed) break;
                ms = MergeStats();
                merged = merge_chains(selected_chains(), num_labels, exact_max_size,
                                      exact_conflicts, exact_time_limit, ms);
                order = complete(merged);
            }
        }
        assert((int)order.size() == num_labels);
        if (verbose)
            for (const auto &c : chains) {
                cout << "GOALCHAINS chain";
                for (int l : c.alternatives[c.selected]) cout << " " << l;
                cout << endl;
            }

        int factors_with_moves = 0, factors_covered = 0;
        for (int f = 0; f < num_factors; f++)
            if (!moving[f].empty()) { factors_with_moves++; if (covered[f]) factors_covered++; }
        const double total_time = chrono::duration<double>(chrono::steady_clock::now() - start).count();
        cout << "GOALCHAINS goal_factors " << goal_factors << " chains " << chains.size()
             << " chain_labels " << ms.chain_labels << " distinct " << merged.size()
             << " mean_product_factors " << (chains.empty() ? 0.0 : double(product_factors) / chains.size())
             << " bfs_retries " << too_big_drops << " unsolved_abstractions " << no_plan
             << " too_big_alone " << too_big_alone
             << " pairs_weight " << ms.total_weight << " violated " << ms.violated
             << " sccs " << ms.sccs << " largest_scc " << ms.largest
             << " exact_sccs " << ms.exact_comps << " proven " << ms.proven << " heuristic_sccs " << ms.heuristic_comps
             << " leftover " << (num_labels - (int)merged.size())
             << " subgoal_chains " << subgoal_chains << " alternatives " << alternatives
             << " selection_rounds " << rounds << " switches " << switches
             << " factors_covered " << factors_covered << " factors_with_moves " << factors_with_moves
             << " chain_time " << chain_time << " total_time " << total_time << endl;
        return order;
    }

    static shared_ptr<LabelOrderFinder> _parse_goal_chains(OptionParser &parser) {
        parser.document_synopsis("goal chains", "");
        parser.add_option<int>("max_states",
            "reachable-state cap per product; ancestors are dropped weakest first above it", "50000");
        parser.add_option<int>("exact_max_size",
            "components with at most this many labels are ordered by exact MaxSAT", "150");
        parser.add_option<int>("exact_conflicts", "kissat conflict limit per MaxSAT call", "200000");
        parser.add_option<double>("exact_time_limit", "seconds per component for the MaxSAT", "10");
        parser.add_option<shared_ptr<LabelOrderFinder>>("leftover",
            "order for the labels no chain mentions", "label_order_relaxed()");
        parser.add_option<bool>("verbose", "report per-goal-factor details", "false");
        parser.add_option<int>("ancestor_depth",
            "causal-graph distance up to which ancestors join a goal factor's product "
            "(nearest and strongest first; the state cap drops the rest)", "1");
        parser.add_option<int>("subgoal_depth",
            "recursion depth of subgoal chains: a chain label with a precondition on a "
            "factor outside its product gets a chain for reaching that precondition, "
            "placed before it; 0 = off", "0");
        parser.add_option<int>("plans_per_goal",
            "candidate abstract plans per chain; >1 (or plan_slack>0) enumerates plans "
            "and selects one per chain jointly", "1");
        parser.add_option<int>("plan_slack",
            "candidate plans may be this much longer than the shortest", "0");
        parser.add_option<int>("selection_rounds",
            "rounds of choosing each chain's best candidate under the current order and re-merging", "5");
        parser.add_option<bool>("leftover_support",
            "place leftover labels depth-first before the chain labels they support "
            "(Balyo's topological ranking seeded with the chain order) instead of at the end", "false");
        Options opts = parser.parse();
        if (parser.help_mode() || parser.dry_run())
            return nullptr;
        return make_shared<LabelOrderFinderGoalChains>(opts);
    }

    static PluginShared<LabelOrderFinder> _plugin_goal_chains("label_order_goal_chains", _parse_goal_chains);
}
