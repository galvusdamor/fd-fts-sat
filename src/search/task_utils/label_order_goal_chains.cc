#include "label_order_finder.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <climits>
#include <cmath>
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
        /*
          Deadline of the order computation (time_budget). The expensive
          steps poll it and give up -- a product search counts as too big, a
          bisimulation is discarded, the MaxSAT gets only the remaining time.
          Inactive without a time_budget, and suspended (DeadlineOff) for the
          unbudgeted depth-1 fallback, which must always complete.
        */
        struct Deadline {
            bool active = false;
            chrono::steady_clock::time_point end;
            bool passed() const { return active && chrono::steady_clock::now() > end; }
            double remaining() const {
                return chrono::duration<double>(end - chrono::steady_clock::now()).count();
            }
        } deadline;
        struct DeadlineOff {
            bool was = deadline.active;
            DeadlineOff() { deadline.active = false; }
            ~DeadlineOff() { deadline.active = was; }
        };

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
          Candidate labels per product state without testing every label: each
          label gets a pivot, the first included factor it is constrained in,
          and is listed under the pivot's states it has transitions from. The
          candidates of a state are the union over its components, sorted, so
          they are tried in label order exactly as a full scan would.
        */
        struct CandidateIndex {
            vector<vector<vector<int>>> at;   // at[i][s] = label positions with pivot i, source s
            vector<int> stamp;
            int tick = 0;
            vector<int> buf;
            void build(const vector<vector<vector<vector<int>>>> &succ,
                       const vector<vector<char>> &unconstrained, int k,
                       const vector<int> &sizes) {
                const int L = succ.size();
                at.assign(k, {});
                for (int i = 0; i < k; i++) at[i].assign(sizes[i], {});
                for (int li = 0; li < L; li++) {
                    int pivot = -1;
                    for (int i = 0; i < k && pivot < 0; i++) if (!unconstrained[li][i]) pivot = i;
                    if (pivot < 0) continue;   // moves nothing here: never useful
                    for (int s = 0; s < sizes[pivot]; s++)
                        if (!succ[li][pivot][s].empty()) at[pivot][s].push_back(li);
                }
                stamp.assign(L, -1);
            }
            const vector<int> &candidates(const vector<int> &st) {
                buf.clear(); tick++;
                for (size_t i = 0; i < at.size(); i++)
                    for (int li : at[i][st[i]]) if (stamp[li] != tick) { stamp[li] = tick; buf.push_back(li); }
                sort(buf.begin(), buf.end());
                return buf;
            }
        };

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
                              vector<int> &plan, long &explored, long &work) {
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

            vector<int> init(k), sizes(k);
            for (int i = 0; i < k; i++) {
                init[i] = task.get_ts(factors[i]).get_init_state();
                sizes[i] = task.get_ts(factors[i]).get_size();
            }
            plan.clear();
            if (is_goal(init)) return BFSResult::FOUND;
            CandidateIndex cand;
            cand.build(succ, unconstrained, k, sizes);

            unordered_map<uint64_t, pair<uint64_t, int>> parent;   // state -> (parent, label)
            deque<uint64_t> queue;
            const uint64_t c0 = encode(init);
            parent.emplace(c0, make_pair(c0, -1));
            queue.push_back(c0);
            vector<int> st(k), nxt(k);
            long polls = 0;
            while (!queue.empty()) {
                if ((++polls & 1023) == 0 && deadline.passed()) { explored += parent.size(); return BFSResult::TOO_BIG; }
                const uint64_t c = queue.front(); queue.pop_front();
                decode(c, st);
                const vector<int> &cands = cand.candidates(st);
                work += 1 + cands.size();
                for (int li : cands) {
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
                                explored += parent.size();
                                return BFSResult::FOUND;
                            }
                            if ((int)parent.size() > max_states) { explored += parent.size(); return BFSResult::TOO_BIG; }
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
            explored += parent.size();
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
                                vector<vector<int>> &plans, long &explored, long &work) {
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
            vector<int> init(k), sizes(k);
            for (int i = 0; i < k; i++) {
                init[i] = task.get_ts(factors[i]).get_init_state();
                sizes[i] = task.get_ts(factors[i]).get_size();
            }
            if (is_goal(init)) { plans.push_back({}); return BFSResult::FOUND; }
            CandidateIndex cand;
            cand.build(succ, unconstrained, k, sizes);

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
                if ((qi & 1023) == 1023 && deadline.passed()) { truncated = true; bound = INT_MAX; break; }
                if (depth[qi] >= bound) break;
                if (goal[qi]) continue;          // paths end at the first goal state
                decode(code[qi], st);
                const vector<int> &cands = cand.candidates(st);
                work += 1 + cands.size();
                for (int li : cands) {
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
            explored += code.size();
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
                for (int pass = 0; pass < 20 && !deadline.passed(); pass++) {
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
          leftover_support(opts.get<bool>("leftover_support")),
          leftover_layer(opts.get<bool>("leftover_layer")),
          goal_pairs(opts.get<int>("goal_pairs")),
          state_budget(opts.get<int>("state_budget")),
          all_plans(opts.get<bool>("all_plans")),
          deep_giveup(opts.get<int>("deep_giveup")),
          work_budget(opts.get<int>("work_budget")),
          merge_strategy(opts.get_enum("merge_strategy")),
          merge_other_goals(opts.get<bool>("merge_other_goals")),
          merge_until_covered(opts.get<bool>("merge_until_covered")),
          max_merges(opts.get<int>("max_merges")),
          merge_skip_too_big(opts.get<bool>("merge_skip_too_big")),
          merge_build_factor(opts.get<double>("merge_build_factor")),
          shrink(opts.get_enum("shrink")),
          prune_slack(opts.get<int>("prune_slack")),
          time_budget(opts.get<double>("time_budget")),
          exact_max_weight(opts.get<int>("exact_max_weight")),
          pair_source(opts.get_enum("pair_source")),
          pair_slack(opts.get<int>("pair_slack")),
          pair_weight(opts.get<int>("pair_weight")),
          chain_weight(opts.get<int>("chain_weight")),
          pair_scope(opts.get_enum("pair_scope")),
          pair_uncovered(opts.get<bool>("pair_uncovered")) {
        if (pair_source != 0 && opts.get_enum("merge_strategy") == 0) {
            cerr << "label_order_goal_chains: pair_source needs a merge_strategy (explicit products)" << endl;
            utils::exit_with(utils::ExitCode::INPUT_ERROR);
        }
        if (leftover_support && leftover_layer) {
            cerr << "label_order_goal_chains: leftover_support and leftover_layer exclude each other" << endl;
            utils::exit_with(utils::ExitCode::INPUT_ERROR);
        }
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
                                 MergeStats &ms, int exact_max_weight = -1, int chain_weight = 1,
                                 const Weights &extra = Weights(),
                                 const vector<long> &extra_first = vector<long>()) {
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
                    if (i + 1 < c.size() && c[i] != c[i + 1]) w[{c[i], c[i + 1]}] += chain_weight;
                }
            }
            // pairs induced by the products' states (pair_source); their labels
            // that no chain mentions come after all chain labels, by depth
            for (const auto &[e, wt] : extra) w[e] += wt;
            if (!extra_first.empty())
                for (int l = 0; l < num_labels; l++)
                    if (first_pos[l] < 0 && extra_first[l] >= 0)
                        first_pos[l] = long(chains.size()) * stride + extra_first[l];

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
                long comp_weight = 0;
                for (const auto &[e, wt] : wc) comp_weight += wt;
                // the totalizer grows with the total weight, and building it is not time-limited
                if ((int)comp.size() <= exact_max_size && (exact_max_weight < 0 || comp_weight <= exact_max_weight)) {
                    ms.exact_comps++;
                    double tl = exact_time_limit;
                    if (deadline.active) tl = min(tl, deadline.remaining());
                    if (tl > 0 && exact_order(comp, wc, co, exact_conflicts, tl)) ms.proven++;
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

        // --------------------------------------------------------------
        // Incremental products (merge_strategy != NONE)
        // --------------------------------------------------------------

        /*
          Per base factor, per state: every (label, target) of a label that
          constrains the factor, sorted by label. Self-loops included: a
          constraining label that loops at s is applicable there.
        */
        struct FactorTable {
            vector<vector<pair<int, int>>> out;
            bool has(int s, int l) const {
                const auto &o = out[s];
                auto it = lower_bound(o.begin(), o.end(), make_pair(l, INT_MIN));
                return it != o.end() && it->first == l;
            }
        };

        /*
          The reachable, goal-reaching part of the product of `factors`.
          Every state stores all its transitions by labels that constrain some
          included factor, sorted by label -- self-loops included, also of
          labels that move nothing here but have a precondition here. So a
          label that constrains the product is applicable in s iff s has an
          edge for it, and every other label is applicable everywhere. That is
          all a later merge needs; the states need not remember which factor
          states they consist of, and can therefore be shrunk.

          Pruning after every merge is exact for all later merges: a path of
          the product with one more factor projects onto a path of this one,
          so an unreachable or dead-end state here only ever has unreachable
          or dead-end refinements.
        */
        struct Product {
            vector<int> factors;
            vector<vector<pair<int, int>>> out;
            vector<char> goal;
            vector<int> dist;          // distance to the product's goal
            int init = 0;
            vector<char> moves;        // per label: moves some included factor
            vector<char> cons;         // per label: constrains some included factor
            bool approximate = false;  // D2 removed (pruning) or added (shrinking) paths
            int size() const { return out.size(); }
        };

        enum class MergeResult { OK, NO_PLAN, TOO_BIG };

        /*
          Keep the states in `keep` that can reach a goal state through kept
          states, renumber, compute goal distances. All states are reachable
          by construction; keeping only goal-reaching ones keeps that true.
        */
        MergeResult prune_to_goal(Product &p, const vector<char> *keep = nullptr) {
            const int n = p.size();
            vector<vector<int>> in(n);
            for (int s = 0; s < n; s++) {
                if (keep && !(*keep)[s]) continue;
                for (auto [l, t] : p.out[s]) if (t != s && (!keep || (*keep)[t])) in[t].push_back(s);
            }
            vector<int> dist(n, INT_MAX);
            deque<int> q;
            for (int s = 0; s < n; s++) if (p.goal[s] && (!keep || (*keep)[s])) { dist[s] = 0; q.push_back(s); }
            while (!q.empty()) {
                const int s = q.front(); q.pop_front();
                for (int u : in[s]) if (dist[u] == INT_MAX) { dist[u] = dist[s] + 1; q.push_back(u); }
            }
            if (dist[p.init] == INT_MAX) return MergeResult::NO_PLAN;
            vector<int> index(n, -1);
            int m = 0;
            for (int s = 0; s < n; s++) if (dist[s] != INT_MAX) index[s] = m++;
            vector<vector<pair<int, int>>> out(m);
            vector<char> goal(m);
            vector<int> d(m);
            for (int s = 0; s < n; s++) {
                if (index[s] < 0) continue;
                const int ns = index[s];
                goal[ns] = p.goal[s]; d[ns] = dist[s];
                for (auto [l, t] : p.out[s]) if (index[t] >= 0) out[ns].push_back({l, index[t]});
            }
            p.out = move(out); p.goal = move(goal); p.dist = move(d);
            p.init = index[p.init];
            return MergeResult::OK;
        }

        MergeResult root_product(const FTSTask &task, int root, const FactorTable &ft,
                                 const vector<vector<int>> &moving, int num_labels,
                                 Product &p, long &explored) {
            const TransitionSystem &ts = task.get_ts(root);
            p = Product();
            p.factors = {root};
            p.moves.assign(num_labels, 0);
            for (int l : moving[root]) p.moves[l] = 1;
            p.cons.assign(num_labels, 0);
            for (int l = 0; l < num_labels; l++) p.cons[l] = !ts.isIrrelevantLabel(l);
            vector<int> index(ts.get_size(), -1);
            deque<int> q;
            index[ts.get_init_state()] = 0; q.push_back(ts.get_init_state());
            vector<int> order{ts.get_init_state()};
            while (!q.empty()) {
                const int s = q.front(); q.pop_front();
                for (auto [l, t] : ft.out[s])
                    if (index[t] < 0) { index[t] = order.size(); order.push_back(t); q.push_back(t); }
            }
            p.out.resize(order.size()); p.goal.resize(order.size());
            for (size_t i = 0; i < order.size(); i++) {
                p.goal[i] = ts.is_goal_state(order[i]);
                for (auto [l, t] : ft.out[order[i]]) p.out[i].push_back({l, index[t]});
            }
            p.init = 0;
            explored += order.size();
            return prune_to_goal(p);
        }

        /*
          The product of p and base factor f over the alive states of p. A
          label is applicable in (s, q) iff it is applicable in s (an edge of
          s, or it does not constrain p) and in q (a transition from q, or it
          does not constrain f). Construction stops at build_cap states.
        */
        MergeResult merge_into(const FTSTask &task, const Product &p, int f, const FactorTable &F,
                               const vector<vector<int>> &moving, int build_cap, bool other_goals,
                               Product &r, long &explored) {
            const TransitionSystem &tf = task.get_ts(f);
            const int L = p.cons.size();
            vector<char> fcons(L, 0);
            for (int l = 0; l < L; l++) fcons[l] = !tf.isIrrelevantLabel(l);
            r = Product();
            r.factors = p.factors; r.factors.push_back(f);
            r.moves = p.moves; for (int l : moving[f]) r.moves[l] = 1;
            r.cons = p.cons; for (int l = 0; l < L; l++) r.cons[l] |= fcons[l];
            r.approximate = p.approximate;
            const uint64_t fs = tf.get_size();
            unordered_map<uint64_t, int> id;
            vector<pair<int, int>> st;   // (state of p, state of f)
            auto intern = [&](int s, int q) {
                const uint64_t c = uint64_t(s) * fs + q;
                auto it = id.find(c);
                if (it != id.end()) return it->second;
                const int n = st.size();
                id.emplace(c, n); st.push_back({s, q});
                return n;
            };
            intern(p.init, tf.get_init_state());
            vector<vector<pair<int, int>>> out;
            for (size_t x = 0; x < st.size(); x++) {
                if ((int)st.size() > build_cap || ((x & 1023) == 1023 && deadline.passed())) {
                    explored += st.size(); return MergeResult::TOO_BIG;
                }
                const auto [s, q] = st[x];
                vector<pair<int, int>> edges;
                const auto &ps = p.out[s];
                const auto &fq = F.out[q];
                size_t a = 0, b = 0;
                while (a < ps.size() || b < fq.size()) {
                    const int la = a < ps.size() ? ps[a].first : INT_MAX;
                    const int lb = b < fq.size() ? fq[b].first : INT_MAX;
                    const int l = min(la, lb);
                    size_t a2 = a, b2 = b;
                    while (a2 < ps.size() && ps[a2].first == l) a2++;
                    while (b2 < fq.size() && fq[b2].first == l) b2++;
                    if (la == l && lb == l) {
                        for (size_t i = a; i < a2; i++)
                            for (size_t j = b; j < b2; j++)
                                edges.push_back({l, intern(ps[i].second, fq[j].second)});
                    } else if (la == l) {
                        if (!fcons[l])
                            for (size_t i = a; i < a2; i++) edges.push_back({l, intern(ps[i].second, q)});
                    } else if (!p.cons[l]) {
                        for (size_t j = b; j < b2; j++) edges.push_back({l, intern(s, fq[j].second)});
                    }
                    a = a2; b = b2;
                }
                out.push_back(move(edges));
            }
            explored += st.size();
            r.out = move(out);
            r.goal.resize(st.size());
            for (size_t x = 0; x < st.size(); x++)
                r.goal[x] = p.goal[st[x].first] && (!other_goals || tf.is_goal_state(st[x].second));
            r.init = 0;
            return prune_to_goal(r);
        }

        // ---- shrinking (Variant D) ----

        /*
          Replace every state by its block. Edges are unioned (duplicates
          removed); a block is a goal if any of its states is. Exact for a
          bisimulation, an over-approximation otherwise.
        */
        void quotient(Product &p, const vector<int> &block, int nblocks) {
            vector<vector<pair<int, int>>> out(nblocks);
            vector<char> goal(nblocks, 0);
            for (int s = 0; s < p.size(); s++) {
                goal[block[s]] |= p.goal[s];
                for (auto [l, t] : p.out[s]) out[block[s]].push_back({l, block[t]});
            }
            for (auto &o : out) { sort(o.begin(), o.end()); o.erase(unique(o.begin(), o.end()), o.end()); }
            p.out = move(out); p.goal = move(goal);
            p.init = block[p.init];
            prune_to_goal(p);   // recomputes distances; every block stays reachable and alive
        }

        /*
          Coarsest partition refining `block` such that states in one block
          have the same (label, target block) sets, over the edges accepted by
          `use_edge`. Signature refinement until the number of blocks is
          stable. Returns the number of blocks.
        */
        int refine(const Product &p, vector<int> &block, const function<bool(int, int)> &use_edge) {
            int n_blocks = *max_element(block.begin(), block.end()) + 1;
            vector<pair<int, int>> sig;
            while (true) {
                if (deadline.passed()) return -1;   // unfinished refinement: not a bisimulation
                map<pair<int, vector<pair<int, int>>>, int> ids;
                vector<int> nb(p.size());
                for (int s = 0; s < p.size(); s++) {
                    sig.clear();
                    for (auto [l, t] : p.out[s]) if (use_edge(s, t)) sig.push_back({l, block[t]});
                    sort(sig.begin(), sig.end());
                    sig.erase(unique(sig.begin(), sig.end()), sig.end());
                    auto it = ids.emplace(make_pair(block[s], sig), (int)ids.size()).first;
                    nb[s] = it->second;
                }
                const int k = ids.size();
                block.swap(nb);
                if (k == n_blocks) return k;
                n_blocks = k;
            }
        }

        // D1: strong bisimulation w.r.t. all labels and the product's goal. Exact.
        void shrink_exact(Product &p) {
            vector<int> block(p.size());
            for (int s = 0; s < p.size(); s++) block[s] = p.goal[s] ? 1 : 0;
            if (*max_element(block.begin(), block.end()) == 0) for (int &b : block) b = 0;
            const int k = refine(p, block, [](int, int) { return true; });
            if (k >= 0 && k < p.size()) quotient(p, block, k);
        }

        // D2a: keep the states on plans of length <= h*(init) + slack. Removes paths.
        bool shrink_prune(Product &p, int slack, int target) {
            vector<int> g(p.size(), INT_MAX);
            deque<int> q;
            g[p.init] = 0; q.push_back(p.init);
            while (!q.empty()) {
                const int s = q.front(); q.pop_front();
                for (auto [l, t] : p.out[s]) if (g[t] == INT_MAX) { g[t] = g[s] + 1; q.push_back(t); }
            }
            const long bound = long(p.dist[p.init]) + slack;
            vector<char> keep(p.size(), 0);
            for (int s = 0; s < p.size(); s++) keep[s] = g[s] != INT_MAX && long(g[s]) + p.dist[s] <= bound;
            prune_to_goal(p, &keep);
            p.approximate = true;
            return p.size() <= target;
        }

        /*
          D2c: greedy bisimulation by goal distance, as in FD: initial
          partition by goal distance, refinement only over edges that lower
          it by one. If that is still too big, one state per goal distance.
          Adds paths.
        */
        bool shrink_greedy(Product &p, int target) {
            vector<int> block = p.dist;
            {   // renumber distances densely
                vector<int> vals(block.begin(), block.end());
                sort(vals.begin(), vals.end()); vals.erase(unique(vals.begin(), vals.end()), vals.end());
                for (int &b : block) b = lower_bound(vals.begin(), vals.end(), b) - vals.begin();
            }
            const vector<int> by_h = block;
            const int nh = *max_element(by_h.begin(), by_h.end()) + 1;
            const vector<int> dist = p.dist;
            int k = refine(p, block, [&](int s, int t) { return dist[t] == dist[s] - 1; });
            if (k < 0) return false;
            p.approximate = true;
            if (k <= target) { quotient(p, block, k); return true; }
            quotient(p, by_h, nh);
            return p.size() <= target;
        }

        /*
          D2b: merge the states with the same goal flag and the same sets of
          incoming and outgoing labels. That adds paths but no consecutive
          label pair that did not exist before. If not enough, D2c.
        */
        bool shrink_pairs(Product &p, int target) {
            vector<vector<int>> in_l(p.size()), out_l(p.size());
            for (int s = 0; s < p.size(); s++)
                for (auto [l, t] : p.out[s]) { out_l[s].push_back(l); in_l[t].push_back(l); }
            map<tuple<char, vector<int>, vector<int>>, int> ids;
            vector<int> block(p.size());
            for (int s = 0; s < p.size(); s++) {
                for (auto *v : {&in_l[s], &out_l[s]}) { sort(v->begin(), v->end()); v->erase(unique(v->begin(), v->end()), v->end()); }
                block[s] = ids.emplace(make_tuple(p.goal[s], in_l[s], out_l[s]), (int)ids.size()).first->second;
            }
            const int k = ids.size();
            if (k < p.size()) { quotient(p, block, k); p.approximate = true; }
            if (p.size() <= target) return true;
            return shrink_greedy(p, target);
        }

        // shrink: 0 NONE, 1 EXACT (D1), 2 PRUNE (D1+D2a), 3 PAIRS (D1+D2b), 4 GREEDY (D1+D2c)
        bool shrink_to(Product &p, int method, int slack, int target) {
            if (method >= 1) shrink_exact(p);
            if (p.size() <= target) return true;
            if (method == 2) return shrink_prune(p, slack, target);
            if (method == 3) return shrink_pairs(p, target);
            if (method == 4) return shrink_greedy(p, target);
            return false;
        }

        // A shortest plan: from the initial state always to a successor one
        // step closer to the goal, smallest label first.
        vector<int> product_plan(const Product &p) {
            vector<int> plan;
            int s = p.init;
            while (p.dist[s] > 0) {
                int best_l = INT_MAX, best_t = -1;
                for (auto [l, t] : p.out[s])
                    if (p.dist[t] == p.dist[s] - 1 && (l < best_l || (l == best_l && t < best_t))) { best_l = l; best_t = t; }
                assert(best_t >= 0);
                plan.push_back(best_l);
                s = best_t;
            }
            return plan;
        }

        /*
          Replay `plan` on base factor f as a set of states. Returns the
          position of the first label f cannot follow, or -1 if it follows
          all of them. Only applicability is checked: the transform leaves no
          dead ends in the base factors, so an applicable label always ends
          in a state that can still reach f's goal.
        */
        int first_flaw(const TransitionSystem &tf, const FactorTable &F, const vector<int> &plan) {
            vector<int> cur{tf.get_init_state()}, nxt;
            vector<char> in(tf.get_size(), 0);
            for (size_t i = 0; i < plan.size(); i++) {
                const int l = plan[i];
                if (tf.isIrrelevantLabel(l)) continue;
                nxt.clear();
                for (int s : cur) {
                    const auto &o = F.out[s];
                    for (auto it = lower_bound(o.begin(), o.end(), make_pair(l, INT_MIN));
                         it != o.end() && it->first == l; ++it)
                        if (!in[it->second]) { in[it->second] = 1; nxt.push_back(it->second); }
                }
                for (int t : nxt) in[t] = 0;
                if (nxt.empty()) return i;
                swap(cur, nxt);
            }
            return -1;
        }

        /*
          pair_source: the consecutive label pairs of *all* qualifying paths
          of a product, instead of those of one shortest plan. For every state
          t, each qualifying edge into t (label a) and out of t (label b)
          gives the pair (a, b); a pair's count is the number of states it
          occurs at, scaled to 1..weight_scale per product. Self-loops never
          qualify. Modes: 1 OPTIMAL (edges on shortest plans from the initial
          state: g(s) + h(s) = h* and h(t) = h(s) - 1), 2 SLACK (edges on
          plans of length <= h* + slack: g(s) + 1 + h(t) <= h* + slack),
          3 ALL (every edge between alive states).
          first[l] gets the smallest g of an edge with label l, for ordering
          labels no chain mentions.
        */
        void state_pairs(const Product &p, int mode, int slack, int weight_scale,
                         map<pair<int, int>, int> &extra, vector<long> &first, long offset, long &distinct) {
            const int n = p.size();
            vector<int> g(n, INT_MAX);
            deque<int> q;
            g[p.init] = 0; q.push_back(p.init);
            while (!q.empty()) {
                const int s = q.front(); q.pop_front();
                for (auto [l, t] : p.out[s]) if (g[t] == INT_MAX) { g[t] = g[s] + 1; q.push_back(t); }
            }
            const long hstar = p.dist[p.init];
            auto ok = [&](int s, int t) {
                if (s == t || g[s] == INT_MAX) return false;
                if (mode == 1) return long(g[s]) + p.dist[s] == hstar && p.dist[t] == p.dist[s] - 1;
                if (mode == 2) return long(g[s]) + 1 + p.dist[t] <= hstar + slack;
                return true;
            };
            vector<vector<int>> in_l(n), out_l(n);
            for (int s = 0; s < n; s++)
                for (auto [l, t] : p.out[s])
                    if (ok(s, t)) {
                        out_l[s].push_back(l); in_l[t].push_back(l);
                        if (first[l] < 0 || offset + g[s] < first[l]) first[l] = offset + g[s];
                    }
            map<pair<int, int>, int> count;
            for (int t = 0; t < n; t++) {
                if ((t & 1023) == 1023 && deadline.passed()) break;
                auto &a = in_l[t], &b = out_l[t];
                sort(a.begin(), a.end()); a.erase(unique(a.begin(), a.end()), a.end());
                sort(b.begin(), b.end()); b.erase(unique(b.begin(), b.end()), b.end());
                for (int x : a) for (int y : b) if (x != y) count[{x, y}]++;
            }
            int mx = 1;
            for (const auto &[e, c] : count) mx = max(mx, c);
            for (const auto &[e, c] : count)
                extra[e] += max(1, int(lround(double(weight_scale) * c / mx)));
            distinct += count.size();
        }

        /*
          pair_scope=LEFTOVER: keep the order of the chain labels and move each
          movable (leftover) label to the position where the fewest weighted
          pairs involving it are backwards. Only pairs with at least one
          movable label count. Insertion local search, as in heuristic_order,
          starting from the given order (relaxed-layer insertion).
        */
        vector<int> place_leftovers(vector<int> order, const vector<char> &movable,
                                    const map<pair<int, int>, int> &pairs, int passes, long &moves) {
            const int n = order.size();
            const int L = movable.size();
            vector<vector<pair<int, int>>> out(L), in(L);
            for (const auto &[e, wt] : pairs) {
                if (!movable[e.first] && !movable[e.second]) continue;
                out[e.first].push_back({e.second, wt});
                in[e.second].push_back({e.first, wt});
            }
            vector<int> pos(L, -1);
            for (int pass = 0; pass < passes && !deadline.passed(); pass++) {
                bool improved = false;
                for (int idx = 0; idx < n; idx++) {
                    const int v = order[idx];
                    if (!movable[v] || (out[v].empty() && in[v].empty())) continue;
                    for (int i = 0; i < n; i++) pos[order[i]] = i;
                    const int p = pos[v];
                    vector<long> delta_at(n + 1, 0);
                    long base = 0;
                    auto add_range = [&](int lo, int hi, long val) {
                        if (lo >= hi) return;
                        delta_at[lo] += val; delta_at[hi] -= val;
                    };
                    for (auto [u, wt] : out[v]) {   // v->u backwards iff v after u
                        const int q = pos[u], qq = q < p ? q : q - 1;
                        add_range(qq + 1, n, wt);
                        if (q < p) base += wt;
                    }
                    for (auto [u, wt] : in[v]) {    // u->v backwards iff v before u
                        const int q = pos[u], qq = q < p ? q : q - 1;
                        add_range(0, qq + 1, wt);
                        if (q > p) base += wt;
                    }
                    long run = 0, bestc = base; int bestpos = -1;
                    for (int i = 0; i < n; i++) {
                        run += delta_at[i];
                        if (run < bestc) { bestc = run; bestpos = i; }
                    }
                    if (bestpos >= 0 && bestpos != p) {
                        order.erase(order.begin() + p);
                        order.insert(order.begin() + bestpos, v);
                        improved = true; moves++;
                    }
                }
                if (!improved) break;
            }
            return order;
        }

        // Goal distances in a base factor (all states are goals if it has
        // no goal of its own).
        vector<int> factor_goal_distances(const TransitionSystem &tf, const FactorTable &F) {
            const int n = tf.get_size();
            vector<vector<int>> in(n);
            for (int s = 0; s < n; s++) for (auto [l, t] : F.out[s]) if (t != s) in[t].push_back(s);
            vector<int> dist(n, INT_MAX);
            deque<int> q;
            for (int s = 0; s < n; s++) if (tf.is_goal_state(s)) { dist[s] = 0; q.push_back(s); }
            while (!q.empty()) {
                const int s = q.front(); q.pop_front();
                for (int u : in[s]) if (dist[u] == INT_MAX) { dist[u] = dist[s] + 1; q.push_back(u); }
            }
            return dist;
        }
    }

    vector<int> LabelOrderFinderGoalChains::find_order(const FTSTask &task) {
        const auto start = chrono::steady_clock::now();
        deadline.active = time_budget >= 0;
        deadline.end = start + chrono::duration_cast<chrono::steady_clock::duration>(
            chrono::duration<double>(max(0.0, time_budget)));
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
        auto ancestors_of = [&](const vector<int> &roots, int depth) {
            if (depth < 0) depth = INT_MAX;   // all transitive ancestors
            vector<int> anc;
            vector<char> in(num_factors, 0);
            for (int r : roots) in[r] = 1;
            vector<int> frontier = roots;
            for (int d = 0; d < depth && !frontier.empty(); d++) {
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
        long explored_states = 0, work_done = 0;
        int skipped_budget = 0, deep_fallbacks = 0, consecutive_fallbacks = 0;
        auto solve = [&](const vector<int> &roots, const vector<char> &root_ok, bool other_goals,
                         vector<vector<int>> &plans, vector<int> &used, int depth, bool budgeted) {
            plans.clear(); used.clear();
            unique_ptr<DeadlineOff> off;
            if (!budgeted) off = make_unique<DeadlineOff>();
            if (budgeted && deadline.passed()) { skipped_budget++; return false; }
            if (budgeted && ((state_budget >= 0 && explored_states >= state_budget) ||
                             (work_budget >= 0 && work_done >= work_budget))) { skipped_budget++; return false; }
            const vector<int> anc = ancestors_of(roots, depth);
            const int nr = roots.size();
            auto goal_sets = [&](const vector<int> &factors) {
                vector<vector<char>> gs(factors.size());
                for (size_t i = 0; i < factors.size(); i++) {
                    const TransitionSystem &ts = task.get_ts(factors[i]);
                    if (i == 0 && !root_ok.empty()) { gs[i] = root_ok; continue; }
                    if ((int)i < nr || other_goals) {
                        gs[i].assign(ts.get_size(), 0);
                        for (int s = 0; s < ts.get_size(); s++) gs[i][s] = ts.is_goal_state(s);
                    }
                }
                return gs;
            };
            auto attempt = [&](int n, vector<vector<int>> &out_plans) {
                vector<int> factors = roots;
                factors.insert(factors.end(), anc.begin(), anc.begin() + n);
                const vector<vector<char>> gs = goal_sets(factors);
                if (multi)
                    return product_plans(task, factors, moving, max_states, gs, plans_per_goal, plan_slack,
                                         out_plans, explored_states, work_done);
                vector<int> plan;
                const BFSResult r = product_bfs(task, factors, moving, max_states, gs, plan, explored_states, work_done);
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
                used = roots;
                used.insert(used.end(), anc.begin(), anc.begin() + best_n);
                product_factors += best_n + nr;
                for (int f : used) covered[f] = 1;
                return true;
            }
            if (r == BFSResult::NO_PLAN) no_plan++; else too_big_alone++;
            return false;
        };

        // ---- incremental products (merge_strategy != NONE) ----
        vector<unique_ptr<FactorTable>> owned_tables(num_factors);
        vector<const FactorTable *> tables(num_factors, nullptr);
        auto table = [&](int f) -> const FactorTable & {
            if (!tables[f]) {
                const TransitionSystem &ts = task.get_ts(f);
                owned_tables[f] = make_unique<FactorTable>();
                FactorTable &t = *owned_tables[f];
                t.out.assign(ts.get_size(), {});
                for (int l = 0; l < num_labels; l++) {
                    if (ts.isIrrelevantLabel(l)) continue;
                    for (const auto &tr : ts.get_transitions_with_label(l)) t.out[tr.src].push_back({l, tr.target});
                }
                for (auto &o : t.out) sort(o.begin(), o.end());
                tables[f] = &t;
            }
            return *tables[f];
        };
        vector<vector<int>> constrained_in;   // label -> factors it constrains
        if (merge_strategy != 0) {
            constrained_in.assign(num_labels, {});
            for (int f = 0; f < num_factors; f++)
                for (int l = 0; l < num_labels; l++)
                    if (!task.get_ts(f).isIrrelevantLabel(l)) constrained_in[l].push_back(f);
        }
        vector<vector<int>> own_rank(num_factors);   // DFP: per label, min goal distance of a target
        auto factor_rank = [&](int f) -> const vector<int> & {
            if (own_rank[f].empty()) {
                const FactorTable &F = table(f);
                const vector<int> d = factor_goal_distances(task.get_ts(f), F);
                own_rank[f].assign(num_labels, INT_MAX);
                for (const auto &o : F.out) for (auto [l, t] : o) own_rank[f][l] = min(own_rank[f][l], d[t]);
            }
            return own_rank[f];
        };
        int merges = 0, merge_too_big = 0, merge_no_plan = 0, cegar_real = 0;
        int pre_shrinks = 0, spurious_plans = 0, spurious_final = 0, approx_products = 0, time_budget_hits = 0;
        map<pair<int, int>, int> extra_pairs;
        vector<long> extra_first(num_labels, -1);
        long extra_distinct = 0;
        int pair_products = 0;
        auto over_time = [&]() {
            const bool over = deadline.passed();
            if (over) time_budget_hits++;
            return over;
        };
        long cegar_flaws = 0;
        double merge_time = 0;
        auto merged_goal_chain = [&](int g, vector<int> &plan, vector<int> &used) {
            const auto merge_start = chrono::steady_clock::now();
            struct AddTime { double &t; chrono::steady_clock::time_point s;
                ~AddTime() { t += chrono::duration<double>(chrono::steady_clock::now() - s).count(); } }
                add_time{merge_time, merge_start};
            Product p;
            if (root_product(task, g, table(g), moving, num_labels, p, explored_states) != MergeResult::OK)
                return false;
            if (shrink != 0 && !shrink_to(p, shrink, prune_slack, max_states)) return false;
            if (shrink == 0 && p.size() > max_states) return false;
            vector<char> in_p(num_factors, 0);
            in_p[g] = 1;
            int n = 0;
            auto may_merge = [&]() {
                return (max_merges < 0 || n < max_merges) && !over_time() &&
                       !(state_budget >= 0 && explored_states >= state_budget);
            };
            // with shrinking, the product may over- or under-approximate: is
            // pi refuted by one of its own factors?
            auto spurious = [&](const vector<int> &pi) {
                for (int h : p.factors) if (first_flaw(task.get_ts(h), table(h), pi) >= 0) return true;
                return false;
            };
            auto try_merge = [&](int f) {
                const FactorTable &F = table(f);
                const int build_cap = (int)min<double>(INT_MAX, max_states * max(1.0, merge_build_factor));
                // p is already minimal under D1 (applied after every merge), so
                // try the merge as it is: the reachable product is often far
                // smaller than |p| * |f|. Only if it does not fit, and only with
                // an approximating method, shrink p first (as in M&S) and retry.
                Product r;
                MergeResult res = merge_into(task, p, f, F, moving, build_cap, merge_other_goals, r, explored_states);
                if (res == MergeResult::TOO_BIG && shrink >= 2) {
                    Product shrunk = p;
                    const long fs = task.get_ts(f).get_size();
                    if (shrink_to(shrunk, shrink, prune_slack, max<long>(1, build_cap / fs))) {
                        pre_shrinks++;
                        res = merge_into(task, shrunk, f, F, moving, build_cap, merge_other_goals, r, explored_states);
                    }
                }
                if (res == MergeResult::OK) {
                    if (shrink != 0) { if (!shrink_to(r, shrink, prune_slack, max_states)) res = MergeResult::TOO_BIG; }
                    else if (r.size() > max_states) res = MergeResult::TOO_BIG;
                }
                if (res == MergeResult::OK) { p = move(r); in_p[f] = 1; merges++; n++; return true; }
                if (res == MergeResult::TOO_BIG) merge_too_big++; else merge_no_plan++;
                return false;
            };
            if (merge_strategy == 1) {                           // A: ranked
                const vector<int> anc = ancestors_of({g}, ancestor_depth);
                bool stopped = false;
                for (int f : anc) {
                    if (!may_merge()) { stopped = true; break; }
                    if (!try_merge(f) && !merge_skip_too_big) { stopped = true; break; }
                }
                if (merge_until_covered && !stopped && ancestor_depth >= 0)
                    for (int f : ancestors_of({g}, -1)) {
                        if (in_p[f] || covered[f]) continue;
                        if (!may_merge()) break;
                        if (!try_merge(f) && !merge_skip_too_big) break;
                    }
            } else if (merge_strategy == 2) {                    // B: CEGAR
                vector<char> excluded(num_factors, 0);           // too big to merge (skip mode)
                while (true) {
                    const vector<int> pi = product_plan(p);
                    if (p.approximate && spurious(pi)) { spurious_plans++; break; }   // cannot be repaired by merging
                    vector<char> cand(num_factors, 0);
                    vector<int> strength(num_factors, 0);
                    for (int l : pi) for (int f : constrained_in[l]) if (!in_p[f] && !excluded[f]) { cand[f] = 1; strength[f]++; }
                    int best = -1, best_pos = INT_MAX;
                    for (int f = 0; f < num_factors; f++) {
                        if (!cand[f]) continue;
                        const int pos = first_flaw(task.get_ts(f), table(f), pi);
                        if (pos < 0) continue;
                        if (pos < best_pos || (pos == best_pos && strength[f] > strength[best])) { best = f; best_pos = pos; }
                    }
                    if (best < 0) {                              // no mergeable factor refutes pi
                        bool any_excluded = false;
                        for (int f = 0; f < num_factors; f++) any_excluded |= excluded[f];
                        if (!any_excluded) cegar_real++;         // pi is a real plan for g
                        break;
                    }
                    cegar_flaws++;
                    if (!may_merge()) break;
                    if (!try_merge(best)) { if (!merge_skip_too_big) break; excluded[best] = 1; }
                }
            } else {                                             // C: DFP-like
                const vector<int> pool = ancestors_of({g}, ancestor_depth);
                vector<char> failed(num_factors, 0);
                while (may_merge()) {
                    vector<int> rank_p(num_labels, INT_MAX);
                    vector<int> ranked_labels;
                    for (size_t s = 0; s < p.out.size(); s++)
                        for (auto [l, t] : p.out[s]) {
                            if (!p.moves[l]) continue;
                            if (rank_p[l] == INT_MAX) ranked_labels.push_back(l);
                            rank_p[l] = min(rank_p[l], p.dist[t]);
                        }
                    int best = -1, best_score = INT_MAX;
                    for (int f : pool) {                         // pool order breaks ties
                        if (in_p[f] || failed[f]) continue;
                        const TransitionSystem &tf = task.get_ts(f);
                        int score = INT_MAX;
                        for (int l : ranked_labels) {
                            if (tf.isIrrelevantLabel(l)) continue;
                            const int sc = merge_strategy == 3 ? rank_p[l] : max(rank_p[l], factor_rank(f)[l]);
                            score = min(score, sc);
                        }
                        if (score < best_score) { best_score = score; best = f; }
                    }
                    if (best < 0) break;
                    if (!try_merge(best)) failed[best] = 1;      // too big: try the next one
                }
            }
            plan = product_plan(p);
            if (p.approximate) { approx_products++; if (spurious(plan)) spurious_final++; }
            if (pair_source != 0 && !deadline.passed()) {
                state_pairs(p, pair_source, pair_slack, pair_weight, extra_pairs, extra_first,
                            long(pair_products++) * (long(p.dist[p.init]) + pair_slack + 2), extra_distinct);
            }
            used = p.factors;
            product_factors += used.size();
            for (int f : used) covered[f] = 1;
            return true;
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
            if (depth >= subgoal_depth || over_time()) return;
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
                    if (!solve({f}, ok, false, sp, su, ancestor_depth, true) || sp[0].empty()) continue;
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
            /*
              Deeper ancestors only within the state budget; if that is used
              up (or the deeper product fails), fall back to the unbudgeted
              depth-1 computation of the default, so running out of budget can
              never leave a goal without the chain the default would give it.
            */
            bool ok = false;
            if (merge_strategy != 0) {
                // incremental product; falls back to the depth-1 default like v2
                vector<int> plan;
                if (!over_time() && !(state_budget >= 0 && explored_states >= state_budget) && merged_goal_chain(g, plan, used))
                    { plans = {plan}; ok = true; }
                else { deep_fallbacks++; }
            } else if (ancestor_depth > 1 && !over_time() && !(deep_giveup > 0 && consecutive_fallbacks >= deep_giveup)) {
                ok = solve({g}, {}, true, plans, used, ancestor_depth, true);
                if (!ok) { deep_fallbacks++; consecutive_fallbacks++; }
                else consecutive_fallbacks = 0;
            }
            if (!ok) ok = solve({g}, {}, true, plans, used, 1, false);
            if (!ok) {
                if (verbose)
                    cout << "GOALCHAINS no abstract plan for factor " << g << " (" << tg.get_size()
                         << " states, " << tg.get_goal_states().size() << " goal states)" << endl;
                continue;
            }
            if (plans[0].empty()) continue;
            add_subgoals(plans[0], used, 0);
            chains.push_back({plans, 0});
        }
        /*
          Pair chains: a joint abstract plan for two goal factors shows how
          their chains interleave (a shared lift serialises two passengers, a
          pipeline of machines interleaves two sheets), which single-goal
          chains cannot tell. Each goal factor is paired with the goal_pairs
          others sharing most ancestors with it.
        */
        int pair_chains = 0;
        const auto pair_start = chrono::steady_clock::now();
        if (goal_pairs > 0) {
            vector<int> goals;
            for (int g = 0; g < num_factors; g++) if (task.get_ts(g).is_goal_relevant()) goals.push_back(g);
            vector<vector<char>> anc_set(goals.size(), vector<char>(num_factors, 0));
            for (size_t i = 0; i < goals.size(); i++) for (int f : ancestors_of({goals[i]}, ancestor_depth)) anc_set[i][f] = 1;
            set<pair<int, int>> done;
            for (size_t i = 0; i < goals.size(); i++) {
                vector<pair<int, int>> partners;   // (-shared, j)
                for (size_t j = 0; j < goals.size(); j++) {
                    if (j == i) continue;
                    int shared = 0;
                    for (int f = 0; f < num_factors; f++) shared += anc_set[i][f] && anc_set[j][f];
                    if (shared > 0) partners.push_back({-shared, j});
                }
                sort(partners.begin(), partners.end());
                for (int n = 0; n < goal_pairs && n < (int)partners.size() && !over_time(); n++) {
                    const int j = partners[n].second;
                    if (!done.insert({min<int>(i, j), max<int>(i, j)}).second) continue;
                    vector<vector<int>> plans; vector<int> used;
                    if (!solve({goals[min<int>(i, j)], goals[max<int>(i, j)]}, {}, true, plans, used, ancestor_depth, true) || plans[0].empty())
                        continue;
                    chains.push_back({plans, 0});
                    pair_chains++;
                }
            }
        }
        const double chain_time = chrono::duration<double>(chrono::steady_clock::now() - start).count();
        const double pair_time = chrono::duration<double>(chrono::steady_clock::now() - pair_start).count();

        // ---- merge, then choose among alternative plans ----
        auto selected_chains = [&]() {
            vector<vector<int>> sel;
            for (const auto &c : chains) {
                if (all_plans) {
                    // every candidate votes: its consecutive pairs all count
                    for (const auto &a : c.alternatives) if (!a.empty()) sel.push_back(a);
                } else {
                    sel.push_back(c.alternatives[c.selected]);
                }
            }
            return sel;
        };
        vector<int> leftover_order = leftover->find_order(task);
        const vector<int> relaxed = leftover_layer ? relaxed_layers(task) : vector<int>();
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
            if (leftover_layer) {
                /*
                  Insert each leftover label before the first chain label of a
                  later relaxed layer; the chain labels keep their order. Never
                  applicable labels go last.
                */
                const int INF = INT_MAX;
                auto lay = [&](int l) { return relaxed[l] < 0 ? INF : relaxed[l]; };
                map<int, vector<int>> pending;   // layer -> leftover labels in leftover order
                for (int l : leftover_order) if (!placed[l]) pending[lay(l)].push_back(l);
                vector<int> result;
                for (int c : order) {
                    while (!pending.empty() && pending.begin()->first < lay(c) && pending.begin()->first != INF) {
                        for (int l : pending.begin()->second) result.push_back(l);
                        pending.erase(pending.begin());
                    }
                    result.push_back(c);
                }
                for (auto &[d, ls] : pending) for (int l : ls) result.push_back(l);
                return result;
            }
            for (int l : leftover_order) if (!placed[l]) order.push_back(l);
            return order;
        };

        MergeStats ms;
        const map<pair<int, int>, int> no_pairs;
        const vector<long> no_first;
        const bool pairs_in_maxsat = pair_scope == 0;
        vector<int> merged = merge_chains(selected_chains(), num_labels, exact_max_size,
                                          exact_conflicts, exact_time_limit, ms, exact_max_weight, chain_weight,
                                          pairs_in_maxsat ? extra_pairs : no_pairs,
                                          pairs_in_maxsat ? extra_first : no_first);
        vector<int> order = complete(merged);
        int rounds = 0, switches = 0;
        long alternatives = 0;
        for (const auto &c : chains) alternatives += c.alternatives.size();
        if (multi && !all_plans) {
            for (; rounds < selection_rounds && !deadline.passed(); rounds++) {
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
                                      exact_conflicts, exact_time_limit, ms, exact_max_weight, chain_weight,
                                      pairs_in_maxsat ? extra_pairs : no_pairs,
                                      pairs_in_maxsat ? extra_first : no_first);
                order = complete(merged);
            }
        }
        long leftover_moves = 0;
        int uncovered_pair_factors = 0;
        if (pair_scope == 1 && pair_uncovered) {
            /*
              Factors in no goal product: their labels would get no pairs at
              all. Take the pairs of all paths through the factor itself
              (ALL: such factors usually have no goal, so every state is one).
            */
            DeadlineOff off;   // atomic factors are small, and without this step their labels get no pairs
            for (int f = 0; f < num_factors; f++) {
                if (covered[f] || moving[f].empty()) continue;
                Product fp;
                if (root_product(task, f, table(f), moving, num_labels, fp, explored_states) != MergeResult::OK)
                    continue;
                state_pairs(fp, 3, 0, pair_weight, extra_pairs, extra_first,
                            long(pair_products++) * 2, extra_distinct);
                uncovered_pair_factors++;
            }
        }
        if (pair_scope == 1 && !extra_pairs.empty()) {
            vector<char> movable(num_labels, 1);
            for (const auto &c : chains) for (int l : c.alternatives[c.selected]) movable[l] = 0;
            DeadlineOff off;   // cheap and bounded by its passes; the order must be complete
            order = place_leftovers(order, movable, extra_pairs, 5, leftover_moves);
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
        deadline.active = false;
        cout << "GOALCHAINS goal_factors " << goal_factors << " chains " << chains.size()
             << " chain_labels " << ms.chain_labels << " distinct " << merged.size()
             << " mean_product_factors " << (chains.empty() ? 0.0 : double(product_factors) / chains.size())
             << " bfs_retries " << too_big_drops << " unsolved_abstractions " << no_plan
             << " too_big_alone " << too_big_alone
             << " pairs_weight " << ms.total_weight << " violated " << ms.violated
             << " sccs " << ms.sccs << " largest_scc " << ms.largest
             << " exact_sccs " << ms.exact_comps << " proven " << ms.proven << " heuristic_sccs " << ms.heuristic_comps
             << " leftover " << (num_labels - (int)merged.size())
             << " subgoal_chains " << subgoal_chains << " pair_chains " << pair_chains
             << " alternatives " << alternatives
             << " selection_rounds " << rounds << " switches " << switches
             << " explored_states " << explored_states << " skipped_budget " << skipped_budget
             << " deep_fallbacks " << deep_fallbacks << " work " << work_done
             << " factors_covered " << factors_covered << " factors_with_moves " << factors_with_moves
             << " merge_strategy " << merge_strategy << " merges " << merges
             << " merge_too_big " << merge_too_big << " merge_no_plan " << merge_no_plan
             << " cegar_real " << cegar_real << " cegar_flaws " << cegar_flaws
             << " merge_time " << merge_time << " pair_time " << pair_time
             << " shrink " << shrink << " pre_shrinks " << pre_shrinks << " approx_products " << approx_products
             << " spurious_plans " << spurious_plans << " spurious_final " << spurious_final
             << " time_budget_hits " << time_budget_hits
             << " pair_source " << pair_source << " pair_products " << pair_products
             << " state_pairs " << extra_distinct << " leftover_moves " << leftover_moves
             << " uncovered_pair_factors " << uncovered_pair_factors
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
        parser.add_option<bool>("all_plans",
            "with plans_per_goal/plan_slack: merge all candidate plans as separate chains "
            "(each votes with its consecutive pairs) instead of selecting one per chain", "false");
        parser.add_option<int>("selection_rounds",
            "rounds of choosing each chain's best candidate under the current order and re-merging", "5");
        parser.add_option<bool>("leftover_layer",
            "insert leftover labels between the chain labels by relaxed-reachability layer "
            "instead of appending them", "false");
        parser.add_option<int>("state_budget",
            "product states the searches beyond the default may explore: goal chains with "
            "ancestor_depth > 1, subgoal chains and pair chains, in that order of generation. "
            "Once used up, goal chains fall back to the default depth-1 computation (which "
            "is never budgeted) and no further subgoal or pair chains are computed; "
            "-1 = unlimited", "-1");
        parser.add_option<int>("work_budget",
            "like state_budget, but counted in expanded states plus candidate labels tested, "
            "which tracks the actual cost where states have many applicable labels; "
            "-1 = unlimited", "-1");
        parser.add_option<int>("deep_giveup",
            "with ancestor_depth > 1: stop trying deep ancestors for the remaining goals after "
            "this many consecutive goals fell back to depth 1 (where deep products are too big, "
            "every attempt is wasted budget); 0 = never", "0");
        parser.add_option<int>("goal_pairs",
            "also plan jointly for each goal factor and up to this many other goal factors "
            "sharing most ancestors with it, adding those chains; 0 = off", "0");
        vector<string> strategies {"NONE", "RANKED", "CEGAR", "DFP_ROOT", "DFP"};
        vector<string> strategies_doc {
            "one explicit product per goal factor over its ancestors up to ancestor_depth (v2)",
            "A: merge the ranked ancestors (causal-graph level, then strength) one at a time, "
            "up to ancestor_depth (-1 = all transitive ancestors)",
            "B: CEGAR -- merge the factor in which the current abstract plan becomes "
            "inapplicable first; stop when no factor refutes it",
            "C1: DFP-like -- merge the ancestor interacting with the product on a label "
            "closest to the root's goal (product goal distances only)",
            "C2: DFP -- min over shared labels of max(rank in product, rank in the factor "
            "by its own goal)"};
        parser.add_enum_option("merge_strategy", strategies,
            "how a goal factor's product is built. Anything but NONE merges one factor "
            "at a time and prunes unreachable and dead-end states after every merge "
            "(exact); a merge exceeding max_states stops merging for that goal", "NONE",
            strategies_doc);
        parser.add_option<bool>("merge_other_goals",
            "with merge_strategy: the product's goal also requires every merged factor's "
            "own goal (as NONE does); default: the root's goal only", "false");
        parser.add_option<bool>("merge_until_covered",
            "with merge_strategy=RANKED: after the ancestors up to ancestor_depth, also merge "
            "deeper ancestors that no earlier goal's product contains yet", "false");
        parser.add_option<bool>("merge_skip_too_big",
            "with merge_strategy RANKED or CEGAR: a merge exceeding max_states skips that factor "
            "and goes on with the next one instead of ending the goal's merging (DFP always skips)",
            "false");
        parser.add_option<double>("merge_build_factor",
            "with merge_strategy: max_states applies to the product after pruning; its "
            "construction may reach merge_build_factor * max_states states", "1");
        vector<string> shrinks {"NONE", "EXACT", "PRUNE", "PAIRS", "GREEDY"};
        vector<string> shrinks_doc {
            "no shrinking: a merge beyond max_states fails",
            "D1: strong bisimulation (all labels, the product's goal) after every merge and "
            "before a merge that could not fit; exact",
            "D1, then D2a beyond max_states: keep only states on plans of length <= "
            "h*(init) + prune_slack (removes paths)",
            "D1, then D2b beyond max_states: merge states with equal goal flag and equal sets "
            "of incoming and outgoing labels (adds paths, but no new consecutive label "
            "pairs); if not enough, GREEDY",
            "D1, then D2c beyond max_states: greedy bisimulation by goal distance as in FD; "
            "if not enough, one state per goal distance (adds paths)"};
        parser.add_enum_option("shrink", shrinks,
            "with merge_strategy: how products are shrunk (Variant D)", "NONE", shrinks_doc);
        parser.add_option<int>("prune_slack",
            "with shrink=PRUNE: how much longer than the shortest plan kept plans may be", "0");
        parser.add_option<double>("time_budget",
            "seconds for the whole order computation; once used up, remaining goals get the "
            "cheap depth-1 chain, and no more merges, subgoal or pair chains are computed; "
            "-1 = unlimited", "-1");
        vector<string> pair_sources {"PLAN", "OPTIMAL", "SLACK", "ALL"};
        vector<string> pair_sources_doc {
            "the consecutive pairs of the chains only",
            "also the pairs of all shortest plans of every goal product",
            "also the pairs of all plans of length <= h* + pair_slack",
            "also the pairs of all paths between alive states"};
        parser.add_enum_option("pair_source", pair_sources,
            "which consecutive label pairs of the goal products the MaxSAT sees; needs a "
            "merge_strategy", "PLAN", pair_sources_doc);
        parser.add_option<int>("pair_slack", "with pair_source=SLACK", "1");
        vector<string> pair_scopes {"ALL", "LEFTOVER"};
        vector<string> pair_scopes_doc {
            "the state-induced pairs go into the MaxSAT with the chain pairs",
            "the MaxSAT sees the chains only; the state-induced pairs then move only the labels "
            "no chain mentions (starting from the relaxed-layer insertion)"};
        parser.add_enum_option("pair_scope", pair_scopes,
            "with pair_source: where the state-induced pairs are used", "ALL", pair_scopes_doc);
        parser.add_option<bool>("pair_uncovered",
            "with pair_scope=LEFTOVER: also take the pairs of all paths of every factor that no "
            "goal product contains", "false");
        parser.add_option<int>("chain_weight",
            "weight of one occurrence of a chain pair (state-induced pairs: 1..pair_weight); "
            "large values keep the chains' order and let the state pairs place the rest", "1");
        parser.add_option<int>("exact_max_weight",
            "components whose pairs weigh more than this in total get only the heuristic order "
            "(the MaxSAT encoding grows with the weight); -1 = no limit", "-1");
        parser.add_option<int>("pair_weight",
            "state-induced pairs get weight 1..pair_weight per product, by the number of "
            "states they occur at (a chain pair counts 1 per occurrence)", "4");
        parser.add_option<int>("max_merges",
            "with merge_strategy: at most this many merges per goal factor; -1 = unlimited", "-1");
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
