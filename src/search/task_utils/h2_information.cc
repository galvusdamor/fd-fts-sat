#include "h2_information.h"

#include "../globals.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/sas_task.h"
#include "../task_transformation/plan_reconstruction.h"
#include "../utils/system.h"

#include <fstream>
#include <iostream>

using namespace std;
using namespace task_representation;

namespace h2_information {
H2Information::H2Information(const string &h2_sas_file) {
    ifstream in(h2_sas_file);
    if (!in) {
        cerr << "h2_information: cannot read " << h2_sas_file << endl;
        utils::exit_with(utils::ExitCode::INPUT_ERROR);
    }
    h2 = make_unique<SASTask>();
    h2->read_from_file(in);
    auto sas = g_sas_task();

    // facts by name; "<none of those>" is not unique and carries no information
    unordered_map<string, FactPair> h2_fact;
    set<string> ambiguous;
    for (int v = 0; v < h2->get_num_variables(); ++v)
        for (int x = 0; x < h2->get_variable_domain_size(v); ++x) {
            const string name = h2->get_fact_name(FactPair(v, x));
            if (h2_fact.count(name)) ambiguous.insert(name);
            h2_fact.emplace(name, FactPair(v, x));
        }
    // which original variables survive in the h2 task: those with any value found there
    to_h2.resize(sas->get_num_variables());
    for (int v = 0; v < sas->get_num_variables(); ++v) {
        const int n = sas->get_variable_domain_size(v);
        to_h2[v].assign(n, FactPair(-1, -1));
        int h2var = -1;
        bool consistent = true;
        for (int x = 0; x < n; ++x) {
            const string name = sas->get_fact_name(FactPair(v, x));
            auto it = h2_fact.find(name);
            if (it == h2_fact.end() || ambiguous.count(name)) continue;
            if (h2var == -1) h2var = it->second.var;
            else if (h2var != it->second.var) consistent = false;
            to_h2[v][x] = it->second;
        }
        if (h2var == -1 || !consistent) {
            // variable dropped (or not identifiable): no information about any of its values
            to_h2[v].assign(n, FactPair(-1, -1));
            continue;
        }
        for (int x = 0; x < n; ++x) {
            const string name = sas->get_fact_name(FactPair(v, x));
            if (to_h2[v][x].var == -1 && !ambiguous.count(name))
                to_h2[v][x] = FactPair(h2var, -1);   // the value was dropped: unreachable
        }
    }

    // operators by name
    set<string> h2_ops;
    for (int o = 0; o < h2->get_num_operators(); ++o)
        h2_ops.insert(h2->get_operator_name(o));
    operator_removed.assign(sas->get_num_operators(), false);
    for (int o = 0; o < sas->get_num_operators(); ++o)
        if (!h2_ops.count(sas->get_operator_name(o))) {
            operator_removed[o] = true;
            ++num_removed;
        }
    int unreachable_facts = 0, mapped_vars = 0;
    for (const auto &var : to_h2) {
        bool any = false;
        for (const auto &f : var) {
            if (f.var >= 0) any = true;
            if (f.var >= 0 && f.value < 0) ++unreachable_facts;
        }
        mapped_vars += any;
    }
    cout << "H2INFO file " << h2_sas_file << " variables " << sas->get_num_variables()
         << " mapped_variables " << mapped_vars << " unreachable_facts " << unreachable_facts
         << " operators " << sas->get_num_operators() << " removed_operators " << num_removed << endl;
}

H2Information::~H2Information() = default;

bool H2Information::unreachable(const FactPair &a) const {
    const FactPair &f = to_h2[a.var][a.value];
    return f.var >= 0 && f.value < 0;
}

bool H2Information::mutex(const FactPair &a, const FactPair &b) const {
    if (unreachable(a) || unreachable(b))
        return true;
    const FactPair &x = to_h2[a.var][a.value], &y = to_h2[b.var][b.value];
    if (x.var < 0 || y.var < 0)
        return false;
    if (x.var == y.var)
        return x.value != y.value;
    return h2->are_facts_mutex(x, y);
}

bool available(const string &h2_sas_file) {
    return ifstream(h2_sas_file).good();
}

bool dead_labels(const FTSTask &fts, const H2Information &info, vector<int> &dead) {
    auto sas = g_sas_task();
    task_transformation::LabelSets sets;
    sets.operators.resize(sas->get_num_operators());
    for (int o = 0; o < sas->get_num_operators(); ++o)
        sets.operators[o] = {o};
    string why;
    if (g_plan_reconstruction && !g_plan_reconstruction->map_label_sets(sets, why)) {
        cout << "H2LABELS skipped: " << why << endl;
        return false;
    }
    if ((int)sets.operators.size() < fts.get_num_labels()) {
        cout << "H2LABELS skipped: " << sets.operators.size() << " mapped labels for "
             << fts.get_num_labels() << " labels" << endl;
        return false;
    }
    dead.clear();
    long ops_in_labels = 0;
    for (int l = 0; l < fts.get_num_labels(); ++l) {
        const auto &ops = sets.operators[l];
        ops_in_labels += ops.size();
        bool all_removed = !ops.empty();
        for (int o : ops)
            if (!info.removed_operator(o)) { all_removed = false; break; }
        if (all_removed)
            dead.push_back(l);
    }
    cout << "H2LABELS labels " << fts.get_num_labels() << " dead " << dead.size()
         << " operators " << sas->get_num_operators() << " removed_operators " << info.get_num_removed_operators()
         << " operators_in_labels " << ops_in_labels << endl;
    return true;
}
}
