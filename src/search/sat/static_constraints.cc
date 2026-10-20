#include "static_constraints.h"

#include "sat_encoder.h"

#include "../options/option_parser.h"
#include "../options/options.h"
#include "../task_representation/fts_task.h"

#include <iostream>

using namespace std;

namespace static_constraints {
void add_options(options::OptionParser &parser) {
    parser.add_option<bool>("state_mutexes",
        "add mutexes between states of different factors (see mutex_source) as clauses at every step", "false");
    vector<string> mutex_sources {"SAS", "FILE", "FTS_FIXPOINT", "SAS_FILE"};
    vector<string> mutex_sources_doc {
        "mutexes of the finite-domain task (translator mutex groups, or h2 if preprocessed)",
        "read from mutex_file: lines 'f s g t' (mutex) or 'f s' (unreachable state)",
        "h^2-style pairwise reachability fixpoint on the FTS itself",
        "h2 mutexes from the SAS file in mutex_file (the h2 preprocessor's output, the task itself not preprocessed)"};
    parser.add_enum_option("mutex_source", mutex_sources, "where state mutexes come from", "SAS", mutex_sources_doc);
    parser.add_option<string>("mutex_file", "mutex_source=FILE: the mutex list; SAS_FILE: the h2 output", "none");
    parser.add_option<int>("mutex_max_facts", "skip the mutex computation for more facts (factor states) than this", "20000");
    parser.add_option<int>("mutex_clauses_max", "state_mutexes: at most this many pair clauses per step", "2000000");
    parser.add_option<double>("mutex_time_limit",
        "seconds for the mutex fixpoint; if it does not converge, no mutexes are used", "30");
    vector<string> dead_sources {"NONE", "SAS_FILE"};
    vector<string> dead_sources_doc {"no dead labels",
        "labels all of whose operators the h2 preprocessor removed; dead_labels_file is its output "
        "(the task itself not preprocessed)"};
    parser.add_enum_option("dead_labels", dead_sources, "labels no plan needs: forced false at every step",
        "NONE", dead_sources_doc);
    parser.add_option<string>("dead_labels_file", "dead_labels=SAS_FILE: the h2 output", "none");
}

Options::Options(const options::Options &opts)
    : state_mutexes(opts.get<bool>("state_mutexes")),
      mutex_clauses_max(opts.get<int>("mutex_clauses_max")) {
    // in the order of the enum options above
    const state_mutexes::Source msources[] = {state_mutexes::Source::SAS, state_mutexes::Source::FILE,
                                              state_mutexes::Source::FTS_FIXPOINT, state_mutexes::Source::SAS_FILE};
    mutex_options.source = msources[opts.get_enum("mutex_source")];
    mutex_options.file = opts.get<string>("mutex_file");
    mutex_options.max_facts = opts.get<int>("mutex_max_facts");
    mutex_options.time_limit = opts.get<double>("mutex_time_limit");
    const dead_labels::Source dsources[] = {dead_labels::Source::NONE, dead_labels::Source::SAS_FILE};
    dead_label_options.source = dsources[opts.get_enum("dead_labels")];
    dead_label_options.file = opts.get<string>("dead_labels_file");
}

shared_ptr<const StaticConstraints> compute(const task_representation::FTSTask &fts, const Options &opts) {
    auto c = make_shared<StaticConstraints>();
    if (opts.state_mutexes) {
        auto m = state_mutexes::compute(fts, opts.mutex_options);
        if (m) {
            long mutex_pairs = 0;
            const int F = fts.get_size();
            for (int f = 0; f < F; ++f)
                for (int a = 0; a < m->factor_size(f); ++a) {
                    const int i = m->fact(f, a);
                    if (!m->compatible(i, i)) {c->unreachable.emplace_back(f, a); continue;}
                    for (int g = f + 1; g < F; ++g)
                        for (int b = 0; b < m->factor_size(g); ++b) {
                            const int j = m->fact(g, b);
                            if (!m->compatible(j, j) || m->compatible(i, j)) continue;
                            ++mutex_pairs;
                            if (opts.mutex_clauses_max < 0 || (long)c->mutex_pairs.size() < opts.mutex_clauses_max)
                                c->mutex_pairs.push_back({f, a, g, b});
                        }
                }
            cout << "SATSTAT state_mutexes source " << m->get_source() << " facts " << m->num_facts()
                 << " unreachable_facts " << c->unreachable.size() << " mutex_pairs " << mutex_pairs
                 << " clauses " << c->unreachable.size() + c->mutex_pairs.size()
                 << " time " << m->get_seconds() << endl;
        }
    }
    vector<bool> dead;
    if (dead_labels::compute(fts, opts.dead_label_options, dead))
        for (int l = 0; l < (int)dead.size(); ++l)
            if (dead[l]) c->dead_labels.push_back(l);
    return c;
}

void encode(sat_capsule &sat, const StaticConstraints &c,
            const vector<vector<int>> &stateVars, const vector<int> &labelVars) {
    for (int l : c.dead_labels) sat.notAll(vector<int>{labelVars[l]});
    for (const auto &[f, s] : c.unreachable) sat.notAll(vector<int>{stateVars[f][s]});
    for (const auto &m : c.mutex_pairs) sat.notAll(vector<int>{stateVars[m[0]][m[1]], stateVars[m[2]][m[3]]});
}
}
