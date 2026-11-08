#include "label_group_finder.h"

#include "label_order_finder.h"

#include "../options/option_parser.h"
#include "../options/plugin.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/transition_system.h"
#include "../utils/timer.h"

#include "../sat/kissat.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>

using namespace std;
using namespace task_representation;
using options::OptionParser;

namespace label_group_finder {

static bool has(const vector<int> &v, int x) {
    return binary_search(v.begin(), v.end(), x);
}

ExistsGroupChecker::ExistsGroupChecker(const FTSTask &fts, int exactConflicts)
    : fts(fts), entries(fts.get_num_labels()), exactConflicts(exactConflicts) {
    for (int ts = 0; ts < fts.get_size(); ts++) {
        const TransitionSystem &tss = fts.get_ts(ts);
        for (int l = 0; l < fts.get_num_labels(); l++) {
            if (tss.isIrrelevantLabel(l))
                continue;
            Entry e{ts, {}, {}, {}};
            set<int> app, sl;
            set<pair<int, int>> real;
            for (const Transition &t : tss.get_transitions_with_label(l)) {
                app.insert(t.src);
                if (t.src == t.target) sl.insert(t.src);
                else real.insert({t.src, t.target});
            }
            e.app.assign(app.begin(), app.end());
            e.sl.assign(sl.begin(), sl.end());
            e.real.assign(real.begin(), real.end());
            entries[l].push_back(move(e));
        }
    }
}

/*
  In factor F: a runs before b if b moves s -> t there and a self-loops at s but not at t; a runs
  after b if b moves s -> t and a is applicable at s without a self-loop there but self-loops at
  t. (Symmetrically with a as the mover.)
*/
pair<bool, bool> ExistsGroupChecker::static_edges(int a, int b) const {
    bool ab = false, ba = false;
    const vector<Entry> &A = entries[a], &B = entries[b];
    size_t i = 0, j = 0;
    while (i < A.size() && j < B.size()) {
        if (A[i].ts < B[j].ts) { i++; continue; }
        if (B[j].ts < A[i].ts) { j++; continue; }
        const Entry &x = A[i], &y = B[j];
        for (auto [s, t] : y.real) {          // b the mover
            if (has(x.sl, s) && !has(x.sl, t)) ab = true;
            if (has(x.app, s) && !has(x.sl, s) && has(x.sl, t)) ba = true;
        }
        for (auto [s, t] : x.real) {          // a the mover
            if (has(y.sl, s) && !has(y.sl, t)) ba = true;
            if (has(y.app, s) && !has(y.sl, s) && has(y.sl, t)) ab = true;
        }
        i++; j++;
    }
    return {ab, ba};
}

bool ExistsGroupChecker::statically_safe(const vector<int> &group) const {
    const int n = group.size();
    vector<vector<int>> succ(n);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            auto [ij, ji] = static_edges(group[i], group[j]);
            if (ij) succ[i].push_back(j);
            if (ji) succ[j].push_back(i);
        }
    // cycle detection (DFS colours)
    vector<int> colour(n, 0);
    for (int r = 0; r < n; r++) {
        if (colour[r]) continue;
        vector<pair<int, size_t>> stack{{r, 0}};
        colour[r] = 1;
        while (!stack.empty()) {
            auto &[v, k] = stack.back();
            if (k < succ[v].size()) {
                int w = succ[v][k++];
                if (colour[w] == 1) return false;
                if (colour[w] == 0) { colour[w] = 1; stack.push_back({w, 0}); }
            } else {
                colour[v] = 2;
                stack.pop_back();
            }
        }
    }
    return true;
}

bool ExistsGroupChecker::keeps_order_executions(int a, int b) const {
    const vector<Entry> &A = entries[a], &B = entries[b];
    size_t i = 0, j = 0;
    while (i < A.size() && j < B.size()) {
        if (A[i].ts < B[j].ts) { i++; continue; }
        if (B[j].ts < A[i].ts) { j++; continue; }
        const Entry &x = A[i], &y = B[j];
        for (auto [s, t] : x.real) {
            if (!has(y.app, t)) continue;
            if (!has(y.sl, t) || !has(y.app, s)) return false;
            auto it = lower_bound(y.real.begin(), y.real.end(), make_pair(t, -1));
            if (it != y.real.end() && it->first == t) return false;   // b moves on from t
        }
        i++; j++;
    }
    return true;
}

/*
  Exact check: is there a choice of factor states, fired labels and movers whose precedences
  contain a cycle? Only factors where at least two group labels are relevant matter (elsewhere a
  label's precondition can be met independently). Variables per such factor: start state x and
  end state y (one-hot over the states the group's labels mention), mover m_l; per label fired
  f_l and "in the cycle" c_l; per factor and ordered pair e1 (a before the mover b) and e2 (b
  after the mover a). A non-empty set of fired labels in which every label has a predecessor in
  the set contains a cycle, and vice versa.
*/
bool ExistsGroupChecker::exactly_safe(const vector<int> &group) {
    exactCalls++;
    int numVars = 0;
    auto var = [&]() { return ++numVars; };
    vector<vector<int>> clauses;
    auto clause = [&](vector<int> c) { clauses.push_back(move(c)); };
    auto exactlyOne = [&](const vector<int> &xs) {
        clause(xs);
        if (xs.size() <= 6) {
            for (size_t i = 0; i < xs.size(); i++)
                for (size_t j = i + 1; j < xs.size(); j++)
                    clause({-xs[i], -xs[j]});
            return;
        }
        int prev = 0;
        for (size_t i = 0; i + 1 < xs.size(); i++) {
            int r = var();
            clause({-xs[i], r});
            if (prev) { clause({-prev, r}); clause({-prev, -xs[i]}); }
            prev = r;
        }
        clause({-prev, -xs.back()});
    };
    const int n = group.size();
    vector<int> f(n), c(n);
    for (int i = 0; i < n; i++) { f[i] = var(); c[i] = var(); clause({-c[i], f[i]}); }
    clause(c);   // a non-empty cycle set
    // factors with at least two group labels
    map<int, vector<pair<int, const Entry *>>> byFactor;   // ts -> (group index, entry)
    for (int i = 0; i < n; i++)
        for (const Entry &e : entries[group[i]])
            byFactor[e.ts].push_back({i, &e});
    // in-edges of every label: (predecessor index, edge variable)
    vector<vector<pair<int, int>>> inEdges(n);
    for (auto &[ts, members] : byFactor) {
        if (members.size() < 2) continue;
        set<int> domainSet;
        for (auto &[i, e] : members) {
            domainSet.insert(e->app.begin(), e->app.end());
            for (auto [s, t] : e->real) domainSet.insert(t);
        }
        vector<int> domain(domainSet.begin(), domainSet.end());
        map<int, int> x, y;
        vector<int> xs, ys;
        for (int s : domain) { x[s] = var(); y[s] = var(); xs.push_back(x[s]); ys.push_back(y[s]); }
        exactlyOne(xs);
        exactlyOne(ys);
        vector<int> mover(members.size(), 0), movers;
        for (size_t k = 0; k < members.size(); k++) {
            auto [i, e] = members[k];
            // fired -> applicable at the start state
            vector<int> app{-f[i]};
            for (int s : e->app) app.push_back(x[s]);
            clause(app);
            if (!e->real.empty()) {
                mover[k] = var();
                movers.push_back(mover[k]);
                clause({-mover[k], f[i]});
                // the mover takes a real transition
                for (int s : domain) {
                    vector<int> c2{-mover[k], -x[s]};
                    for (auto [u, t] : e->real) if (u == s) c2.push_back(y[t]);
                    clause(c2);
                }
            }
            // a fired non-mover self-loops at the start or the end state
            for (int s : domain) {
                if (has(e->sl, s)) continue;
                vector<int> c3{-f[i], -x[s]};
                if (mover[k]) c3.push_back(mover[k]);
                for (int t : e->sl) c3.push_back(y[t]);
                clause(c3);
            }
        }
        for (size_t a = 0; a < movers.size(); a++)
            for (size_t b = a + 1; b < movers.size(); b++)
                clause({-movers[a], -movers[b]});
        // a change of state needs a mover with that transition
        for (int s : domain)
            for (int t : domain) {
                if (s == t) continue;
                vector<int> c4{-x[s], -y[t]};
                for (size_t k = 0; k < members.size(); k++)
                    if (mover[k] && binary_search(members[k].second->real.begin(), members[k].second->real.end(), make_pair(s, t)))
                        c4.push_back(mover[k]);
                clause(c4);
            }
        // precedence edges
        for (size_t ka = 0; ka < members.size(); ka++)
            for (size_t kb = 0; kb < members.size(); kb++) {
                if (ka == kb) continue;
                auto [ia, ea] = members[ka];
                auto [ib, eb] = members[kb];
                // e1: a (non-mover) self-loops at the start but not at the end, b is the mover
                if (mover[kb] && !ea->sl.empty()) {
                    int e1 = var();
                    clause({-e1, mover[kb]});
                    clause({-e1, f[ia]});
                    if (mover[ka]) clause({-e1, -mover[ka]});
                    vector<int> at{-e1};
                    for (int s : ea->sl) at.push_back(x[s]);
                    clause(at);
                    for (int t : ea->sl) clause({-e1, -y[t]});
                    inEdges[ib].push_back({ia, e1});
                }
                // e2: a is the mover, b (non-mover) self-loops at the end but not at the start
                if (mover[ka] && !eb->sl.empty()) {
                    int e2 = var();
                    clause({-e2, mover[ka]});
                    clause({-e2, f[ib]});
                    if (mover[kb]) clause({-e2, -mover[kb]});
                    vector<int> at{-e2};
                    for (int t : eb->sl) at.push_back(y[t]);
                    clause(at);
                    for (int s : eb->sl) clause({-e2, -x[s]});
                    inEdges[ib].push_back({ia, e2});
                }
            }
    }
    // every label of the cycle set has a predecessor in it
    for (int b = 0; b < n; b++) {
        vector<int> pred{-c[b]};
        for (auto [a, e] : inEdges[b]) {
            int z = var();
            clause({-z, c[a]});
            clause({-z, e});
            pred.push_back(z);
        }
        clause(pred);
    }
    kissat *solver = kissat_init();
    kissat_set_option(solver, "quiet", 1);
    kissat_set_conflict_limit(solver, exactConflicts);
    for (const auto &cl : clauses) {
        for (int l : cl) kissat_add(solver, l);
        kissat_add(solver, 0);
    }
    const int res = kissat_solve(solver);
    kissat_release(solver);
    if (res == 20) { exactSafe++; return true; }
    if (res == 10) exactUnsafe++;
    else exactUnknown++;
    return false;
}

// ---------------------------------------------------------------------------
// Finders
// ---------------------------------------------------------------------------

class LabelGroupsNone : public LabelGroupFinder {
public:
    vector<vector<int>> find_groups(const FTSTask &) override { return {}; }
};

class LabelGroupsFixed : public LabelGroupFinder {
    shared_ptr<label_order_finder::LabelOrderFinder> order;
    int size;
public:
    LabelGroupsFixed(shared_ptr<label_order_finder::LabelOrderFinder> order, int size) : order(order), size(size) {}
    vector<vector<int>> find_groups(const FTSTask &fts) override {
        vector<vector<int>> groups;
        for (int l : order->find_order(fts)) {
            if (groups.empty() || (size > 0 && (int) groups.back().size() >= size)) groups.emplace_back();
            groups.back().push_back(l);
        }
        cout << "LABELGROUPS groups " << groups.size() << " labels " << fts.get_num_labels() << endl;
        return groups;
    }
};

/*
  Exists-step groups, greedily along a label order: a label joins the current group if the group
  stays exists-safe (statically, else by the exact check if enabled) and, with keep_order, if no
  earlier label of the group could feed it in a way the group's semantics cannot reproduce
  (ExistsGroupChecker::keeps_order_executions) -- then every execution of the intermediate-state
  encoding in the label order is an execution of the group, so horizons do not grow.
*/
class LabelGroupsExists : public LabelGroupFinder {
    shared_ptr<label_order_finder::LabelOrderFinder> order;
    bool keepOrder, exact;
    int maxSize, maxExactSize, exactConflicts;
public:
    LabelGroupsExists(const options::Options &opts)
        : order(opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("order")),
          keepOrder(opts.get<bool>("keep_order")), exact(opts.get<bool>("exact")),
          maxSize(opts.get<int>("max_size")), maxExactSize(opts.get<int>("max_exact_size")),
          exactConflicts(opts.get<int>("exact_conflicts")) {}
    vector<vector<int>> find_groups(const FTSTask &fts) override {
        utils::Timer timer;
        ExistsGroupChecker checker(fts, exactConflicts);
        vector<vector<int>> groups;
        vector<int> cur;
        bool curStaticSafe = true;
        long orderRejects = 0, staticRejects = 0, exactAccepts = 0, exactRejects = 0;
        for (int b : order->find_order(fts)) {
            bool join = !cur.empty() && (maxSize <= 0 || (int) cur.size() < maxSize);
            if (join && keepOrder)
                for (int a : cur)
                    if (!checker.keeps_order_executions(a, b)) { join = false; orderRejects++; break; }
            if (join) {
                vector<int> cand = cur;
                cand.push_back(b);
                bool safe = curStaticSafe && checker.statically_safe(cand);
                if (safe) {
                    // stays statically safe
                } else if (exact && (int) cand.size() <= maxExactSize) {
                    safe = checker.exactly_safe(cand);
                    if (safe) { exactAccepts++; curStaticSafe = false; }
                    else exactRejects++;
                } else {
                    staticRejects++;
                }
                join = safe;
            }
            if (!join) {
                if (!cur.empty()) groups.push_back(cur);
                cur.clear();
                curStaticSafe = true;
            }
            cur.push_back(b);
        }
        if (!cur.empty()) groups.push_back(cur);
        size_t largest = 0, inMulti = 0;
        for (const auto &g : groups) {
            largest = max(largest, g.size());
            if (g.size() > 1) inMulti += g.size();
        }
        cout << "LABELGROUPS groups " << groups.size() << " labels " << fts.get_num_labels()
             << " largest " << largest << " labels_in_multi " << inMulti
             << " order_rejects " << orderRejects << " static_rejects " << staticRejects
             << " exact_calls " << checker.exactCalls << " exact_accepts " << exactAccepts
             << " exact_rejects " << exactRejects << " exact_unknown " << checker.exactUnknown
             << " time " << timer() << endl;
        return groups;
    }
};

static options::PluginTypePlugin<LabelGroupFinder> _type_plugin(
    "LabelGroupFinder",
    "Ordered label sets, one per micro-step of a time step (see label_group_finder.h).");

static shared_ptr<LabelGroupFinder> _parse_none(OptionParser &parser) {
    parser.document_synopsis("no groups", "the encoder's own grouping options apply");
    if (parser.dry_run()) return nullptr;
    return make_shared<LabelGroupsNone>();
}

static shared_ptr<LabelGroupFinder> _parse_fixed(OptionParser &parser) {
    parser.document_synopsis("fixed-size groups",
                             "consecutive labels of order; size 1 = one label per micro-step, "
                             "0 = one group with every label");
    parser.add_option<shared_ptr<label_order_finder::LabelOrderFinder>>("order", "label order", "label_order_linear()");
    parser.add_option<int>("size", "labels per group (0: all)", "1");
    options::Options opts = parser.parse();
    if (parser.dry_run()) return nullptr;
    return make_shared<LabelGroupsFixed>(opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("order"),
                                         opts.get<int>("size"));
}

static shared_ptr<LabelGroupFinder> _parse_exists(OptionParser &parser) {
    parser.document_synopsis("exists-step groups", "see LabelGroupsExists in label_group_finder.cc");
    parser.add_option<shared_ptr<label_order_finder::LabelOrderFinder>>("order", "base label order", "label_order_linear()");
    parser.add_option<bool>("keep_order",
                            "only group labels so that every execution of the label order inside the "
                            "group remains possible (horizons do not grow)", "true");
    parser.add_option<bool>("exact",
                            "if the static precedence graph has a cycle, decide exists-safety exactly "
                            "with a SAT call", "true");
    parser.add_option<int>("max_size", "at most this many labels per group (0: no limit)", "64");
    parser.add_option<int>("max_exact_size", "exact checks only for groups up to this size", "32");
    parser.add_option<int>("exact_conflicts", "conflict limit of an exact check (unknown = not safe)", "10000");
    options::Options opts = parser.parse();
    if (parser.dry_run()) return nullptr;
    return make_shared<LabelGroupsExists>(opts);
}

static options::PluginShared<LabelGroupFinder> _plugin_none("label_groups_none", _parse_none);
static options::PluginShared<LabelGroupFinder> _plugin_fixed("label_groups_fixed", _parse_fixed);
static options::PluginShared<LabelGroupFinder> _plugin_exists("label_groups_exists", _parse_exists);

}
