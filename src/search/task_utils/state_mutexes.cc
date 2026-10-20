#include "state_mutexes.h"

#include "h2_information.h"

#include "../globals.h"
#include "../task_representation/fact.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/labels.h"
#include "../task_representation/sas_task.h"
#include "../task_representation/transition_system.h"
#include "../task_transformation/plan_reconstruction.h"
#include "../utils/timer.h"

#include <cstdlib>
#include <functional>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace std;
using namespace task_representation;
using task_transformation::StateSets;

namespace state_mutexes {
StateMutexes::StateMutexes(const FTSTask &fts, bool all_compatible) {
    const int F = fts.get_size();
    fact_offset.assign(F + 1, 0);
    for (int f = 0; f < F; ++f)
        fact_offset[f + 1] = fact_offset[f] + fts.get_ts(f).get_size();
    const int N = fact_offset[F];
    compat.assign(N, vector<uint64_t>((N + 63) / 64, all_compatible ? ~uint64_t(0) : 0));
}

void StateMutexes::set_compatible(int i, int j) {
    compat[i][j >> 6] |= uint64_t(1) << (j & 63);
    compat[j][i >> 6] |= uint64_t(1) << (i & 63);
}

void StateMutexes::set_incompatible(int i, int j) {
    compat[i][j >> 6] &= ~(uint64_t(1) << (j & 63));
    compat[j][i >> 6] &= ~(uint64_t(1) << (i & 63));
}

static unique_ptr<StateMutexes> skipped(const string &why) {
    cout << "STATEMUTEXES skipped: " << why << endl;
    return nullptr;
}

/*
  Mutexes of the finite-domain task -- the translator's mutex groups, or the h2
  preprocessor's if the task was preprocessed -- handed down to the states of the transformed
  FTS by the plan-reconstruction steps (PlanReconstruction::map_state_sets). Two states of
  different factors are mutex iff every pair of their tuples contains a mutex pair of facts.
*/
static unique_ptr<StateMutexes> from_sas(const FTSTask &fts,
                                         const function<bool(const FactPair &, const FactPair &)> &facts_mutex,
                                         const function<bool(const FactPair &)> &fact_unreachable,
                                         const string &name) {
    utils::Timer t;
    auto sas = g_sas_task();
    StateSets sets;
    sets.vars.resize(sas->get_num_variables());
    sets.tuples.resize(sas->get_num_variables());
    for (int v = 0; v < sas->get_num_variables(); ++v) {
        sets.vars[v] = {v};
        sets.tuples[v].resize(sas->get_variable_domain_size(v));
        for (int x = 0; x < (int)sets.tuples[v].size(); ++x)
            sets.tuples[v][x] = {StateSets::Tuple{x}};
    }
    string why;
    if (g_plan_reconstruction && !g_plan_reconstruction->map_state_sets(sets, why))
        return skipped(why);
    const int F = fts.get_size();
    if ((int)sets.tuples.size() != F)
        return skipped(to_string(sets.tuples.size()) + " mapped factors for " + to_string(F) + " factors");
    for (int f = 0; f < F; ++f) {
        if ((int)sets.tuples[f].size() != fts.get_ts(f).get_size() || sets.vars[f].empty())
            return skipped("factor " + to_string(f) + " state count");
        for (auto &st : sets.tuples[f])
            task_transformation::normalize(st);
    }

    auto m = make_unique<StateMutexes>(fts, false);
    // a tuple is impossible if one of its facts is unreachable; a state is unreachable if all
    // its tuples are impossible
    auto tuple_impossible = [&](int f, const StateSets::Tuple &x) {
        for (size_t i = 0; i < x.size(); ++i)
            if (fact_unreachable(FactPair(sets.vars[f][i], x[i]))) return true;
        return false;
    };
    for (int f = 0; f < F; ++f)
        for (int a = 0; a < fts.get_ts(f).get_size(); ++a) {
            bool unreachable = !sets.tuples[f][a].empty();
            for (const auto &x : sets.tuples[f][a])
                if (!tuple_impossible(f, x)) { unreachable = false; break; }
            if (!unreachable) m->set_compatible(m->fact(f, a), m->fact(f, a));
        }
    // two tuples are incompatible if some fact of one is mutex with some fact of the other
    auto tuples_mutex = [&](int f, const StateSets::Tuple &x, int g, const StateSets::Tuple &y) {
        if (tuple_impossible(f, x) || tuple_impossible(g, y)) return true;
        for (size_t i = 0; i < x.size(); ++i)
            for (size_t j = 0; j < y.size(); ++j)
                if (facts_mutex(FactPair(sets.vars[f][i], x[i]), FactPair(sets.vars[g][j], y[j])))
                    return true;
        return false;
    };
    for (int f = 0; f < F; ++f)
        for (int g = f + 1; g < F; ++g)
            for (int a = 0; a < fts.get_ts(f).get_size(); ++a)
                for (int b = 0; b < fts.get_ts(g).get_size(); ++b) {
                    // no preimage at all: no evidence, claim nothing
                    bool mutex = !sets.tuples[f][a].empty() && !sets.tuples[g][b].empty();
                    for (const auto &x : sets.tuples[f][a]) {
                        if (!mutex) break;
                        for (const auto &y : sets.tuples[g][b])
                            if (!tuples_mutex(f, x, g, y)) { mutex = false; break; }
                    }
                    if (!mutex)
                        m->set_compatible(m->fact(f, a), m->fact(g, b));
                }
    // FTS_MAPPING_DUMP=file: per factor state its SAS variables and tuples (diagnostics)
    if (const char *dump = getenv("FTS_MAPPING_DUMP")) {
        ofstream out(dump);
        for (int f = 0; f < F; ++f)
            for (size_t a = 0; a < sets.tuples[f].size(); ++a) {
                out << f << " " << a << " vars";
                for (int v : sets.vars[f]) out << " " << v;
                out << " :";
                for (const auto &tuple : sets.tuples[f][a]) {
                    out << " (";
                    for (size_t i = 0; i < tuple.size(); ++i) out << (i ? "," : "") << tuple[i];
                    out << ")";
                }
                out << "\n";
            }
    }
    // safeguard: the initial states of all factors hold together, so a mutex between two of
    // them means a step mapped the states wrongly -- then no mutexes are used at all
    for (int f = 0; f < F; ++f)
        for (int g = f + 1; g < F; ++g)
            if (!m->compatible(m->fact(f, fts.get_ts(f).get_init_state()), m->fact(g, fts.get_ts(g).get_init_state())))
                return skipped("initial states of factors " + to_string(f) + " and " + to_string(g)
                               + " are mutex: inconsistent mapping");
    m->set_source(name, t());
    return m;
}

static unique_ptr<StateMutexes> from_file(const FTSTask &fts, const string &file) {
    utils::Timer t;
    ifstream in(file);
    if (!in)
        return skipped("cannot read " + file);
    auto m = make_unique<StateMutexes>(fts, true);
    const int N = m->num_facts();
    string line;
    while (getline(in, line)) {
        istringstream ls(line);
        vector<int> v;
        int x;
        while (ls >> x) v.push_back(x);
        if (v.size() == 4) {
            m->set_incompatible(m->fact(v[0], v[1]), m->fact(v[2], v[3]));
        } else if (v.size() == 2) {
            const int i = m->fact(v[0], v[1]);
            for (int j = 0; j < N; ++j) m->set_incompatible(i, j);
        }
    }
    m->set_source("file", t());
    return m;
}

/*
  An h^2-style pairwise reachability fixpoint on the FTS. Facts are (factor, state); R
  over-approximates the pairs of facts that can hold together in a reachable state: it starts
  with the pairs of initial states, and a label l adds the pairs of its transitions' targets
  (for two factors it is relevant to, or one and a factor it does not touch) from pairs of
  their sources, provided every other factor l is relevant to has an l-source state
  compatible with both. Pairs outside R are mutex, facts outside R unreachable. A fixpoint
  that did not converge within the budget is discarded (an unfinished R would
  under-approximate and claim false mutexes).
*/
static unique_ptr<StateMutexes> fts_fixpoint(const FTSTask &fts, double time_limit) {
    utils::Timer t;
    const int F = fts.get_size();
    const int L = fts.get_num_labels();
    auto m = make_unique<StateMutexes>(fts, false);
    const int N = m->num_facts();
    vector<int> factor_of(N);
    for (int f = 0; f < F; ++f)
        for (int s = 0; s < m->factor_size(f); ++s) factor_of[m->fact(f, s)] = f;
    const int W = (N + 63) / 64;
    bool changed = false;
    auto add = [&](int i, int j) {
        if (m->compatible(i, j)) return;
        m->set_compatible(i, j);
        changed = true;
    };
    for (int f = 0; f < F; ++f)
        for (int g = 0; g < F; ++g)
            add(m->fact(f, fts.get_ts(f).get_init_state()), m->fact(g, fts.get_ts(g).get_init_state()));

    // per label: relevant factors, their transitions (fact ids) and source masks
    struct LabelInfo {vector<int> rel; vector<vector<pair<int, int>>> trans; vector<vector<uint64_t>> pre;};
    vector<LabelInfo> info(L);
    for (int l = 0; l < L; ++l) {
        for (int f = 0; f < F; ++f) {
            const TransitionSystem &ts = fts.get_ts(f);
            const LabelID id(l);
            if (!ts.is_relevant_label(id) && ts.is_selfloop_everywhere(id)) continue;
            info[l].rel.push_back(f);
            vector<pair<int, int>> tr;
            vector<uint64_t> pre(W, 0);
            for (const auto &x : ts.get_transitions_with_label(l)) {
                const int s = m->fact(f, x.src), s2 = m->fact(f, x.target);
                tr.emplace_back(s, s2);
                pre[s >> 6] |= uint64_t(1) << (s & 63);
            }
            info[l].trans.push_back(move(tr));
            info[l].pre.push_back(move(pre));
        }
    }
    vector<char> rel_mark(F, 0);
    int passes = 0;
    bool timeout = false;
    do {
        changed = false;
        ++passes;
        for (int l = 0; l < L && !timeout; ++l) {
            const LabelInfo &li = info[l];
            const int k = li.rel.size();
            if (k == 0) continue;
            for (int f : li.rel) rel_mark[f] = 1;
            // every relevant factor other than fx, fy has an l-source compatible with x and y
            auto consistent = [&](int x, int y, int ix, int iy) {
                const auto &rx = m->compatible_row(x), &ry = m->compatible_row(y);
                for (int h = 0; h < k; ++h) {
                    if (h == ix || h == iy) continue;
                    const auto &p = li.pre[h];
                    bool any = false;
                    for (int w = 0; w < W && !any; ++w) any = (rx[w] & ry[w] & p[w]) != 0;
                    if (!any) return false;
                }
                return true;
            };
            for (int a = 0; a < k; ++a) {
                for (const auto &[s, s2] : li.trans[a]) {
                    if (!m->compatible(s, s)) continue;
                    if (!consistent(s, s, a, -1)) continue;
                    add(s2, s2);
                    for (int b = a + 1; b < k; ++b)
                        for (const auto &[u, u2] : li.trans[b])
                            if (m->compatible(s, u) && consistent(s, u, a, b)) add(s2, u2);
                    // facts of factors l does not touch keep their state
                    for (int w = 0; w < W; ++w) {
                        uint64_t bits = m->compatible_row(s)[w];
                        while (bits) {
                            const int j = (w << 6) + __builtin_ctzll(bits);
                            bits &= bits - 1;
                            if (rel_mark[factor_of[j]]) continue;
                            if (consistent(s, j, a, -1)) add(s2, j);
                        }
                    }
                }
            }
            for (int f : li.rel) rel_mark[f] = 0;
            if (time_limit >= 0 && t() > time_limit) timeout = true;
        }
    } while (changed && !timeout);
    if (timeout)
        return skipped("fixpoint did not converge within the time limit (" + to_string(passes) + " passes)");
    m->set_source("fts_fixpoint passes " + to_string(passes), t());
    return m;
}

unique_ptr<StateMutexes> compute(const FTSTask &fts, const Options &opts) {
    int N = 0;
    for (int f = 0; f < fts.get_size(); ++f) N += fts.get_ts(f).get_size();
    if (N > opts.max_facts)
        return skipped("too many facts (" + to_string(N) + ")");
    unique_ptr<StateMutexes> m;
    switch (opts.source) {
    case Source::SAS: {
        auto sas = g_sas_task();
        m = from_sas(fts, [&](const FactPair &a, const FactPair &b) {return sas->are_facts_mutex(a, b);},
                     [](const FactPair &) {return false;}, "sas");
        break;
    }
    case Source::SAS_FILE: {
        if (!h2_information::available(opts.file))
            return skipped("no h2 information in " + opts.file + " (h2 failed or was not run)");
        h2_information::H2Information info(opts.file);
        vector<int> dead;
        h2_information::dead_labels(fts, info, dead);   // reported only, not used yet
        m = from_sas(fts, [&](const FactPair &a, const FactPair &b) {return info.mutex(a, b);},
                     [&](const FactPair &a) {return info.unreachable(a);}, "sas_file");
        break;
    }
    case Source::FILE: m = from_file(fts, opts.file); break;
    case Source::FTS_FIXPOINT: m = fts_fixpoint(fts, opts.time_limit); break;
    }
    if (!m)
        return m;
    // FTS_MUTEX_DUMP=file: all mutex pairs ("f s g t") and unreachable facts ("f s")
    if (const char *dump = getenv("FTS_MUTEX_DUMP")) {
        ofstream out(dump);
        const int F = m->num_factors();
        for (int f = 0; f < F; ++f)
            for (int a = 0; a < m->factor_size(f); ++a) {
                const int i = m->fact(f, a);
                if (!m->compatible(i, i)) { out << f << " " << a << "\n"; continue; }
                for (int g = f + 1; g < F; ++g)
                    for (int b = 0; b < m->factor_size(g); ++b) {
                        const int j = m->fact(g, b);
                        if (m->compatible(j, j) && !m->compatible(i, j))
                            out << f << " " << a << " " << g << " " << b << "\n";
                    }
            }
    }
    return m;
}
}
