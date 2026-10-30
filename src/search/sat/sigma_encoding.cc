#include "sigma_encoding.h"

#include "ipasir.h"
#include "sat_encoder.h"

#include "../options/option_parser.h"
#include "../options/options.h"
#include "../options/plugin.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/transition_system.h"
#include "../task_utils/label_order_finder.h"
#include "../utils/timer.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>

using namespace std;
using namespace task_representation;

namespace sat_search {

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

SigmaEncodingFactory::SigmaEncodingFactory(const options::Options &opts)
    : SATEncodingFactory(opts.get<bool>("force_at_least_one_action")),
      labelOrderFinder(opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("label_order")),
      staticOptions(opts) {
}

void SigmaEncodingFactory::initialize() {
    utils::Timer timer;
    auto d = make_shared<SigmaData>();
    d->order = labelOrderFinder->find_order(*fts);
    d->factors.resize(fts->get_size());
    size_t changedVars = 0, labelsInFactors = 0, nondet = 0;
    for (int ts = 0; ts < fts->get_size(); ts++) {
        const TransitionSystem &tss = fts->get_ts(ts);
        for (int label : d->order) {
            if (tss.isIrrelevantLabel(label))
                continue;
            SigmaLabelData ld;
            ld.label = label;
            map<int, set<int>> targets, preds;
            set<int> changed, applicable;
            for (const Transition &t : tss.get_transitions_with_label(label)) {
                applicable.insert(t.src);
                targets[t.src].insert(t.target);
                if (t.src != t.target) {
                    changed.insert(t.src);
                    changed.insert(t.target);
                }
            }
            for (const Transition &t : tss.get_transitions_with_label(label))
                if (changed.count(t.target))
                    preds[t.target].insert(t.src);
            ld.changed.assign(changed.begin(), changed.end());
            ld.applicable.assign(applicable.begin(), applicable.end());
            for (auto &[x, ys] : targets) {
                ld.targets.emplace_back(x, vector<int>(ys.begin(), ys.end()));
                nondet += ys.size() > 1;
            }
            for (int s : ld.changed)
                ld.predecessors.emplace_back(s, vector<int>(preds[s].begin(), preds[s].end()));
            changedVars += ld.changed.size();
            labelsInFactors++;
            d->factors[ts].push_back(move(ld));
        }
    }
    data = d;
    staticConstraints = static_constraints::compute(*fts, staticOptions);
    cout << "SIGMASTAT sigma_vars_per_step " << changedVars << " label_factor_pairs " << labelsInFactors
         << " nondeterministic_sources " << nondet << " time " << timer() << endl;
    cout << "SAT init time: " << timer << endl;
}

unique_ptr<SATEncoding> SigmaEncodingFactory::createEncodingInstance(shared_ptr<sat_capsule> capsule) {
    auto encoding = make_unique<SigmaEncoding>(capsule, fts, forceAtLeastOneAction, data);
    encoding->setStaticConstraints(staticConstraints);
    return encoding;
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

SigmaEncoding::SigmaEncoding(shared_ptr<sat_capsule> capsule, const shared_ptr<FTSTask> &fts,
                             bool forceAtLeastOneAction, shared_ptr<const SigmaData> _data)
    : CommonEncoding(capsule, fts, forceAtLeastOneAction, false, true), data(_data) {
    // plan extraction executes the labels of a step in this order
    labelOrder = data->order;
}

void SigmaEncoding::generateAdditionalVariables(int /*fromTime*/) {
}

void SigmaEncoding::encode_frame_axioms(const vector<vector<int>> &, const vector<vector<int>> &, int) {
    // a label that does not fire copies the factor's state (encode_transition)
}

void SigmaEncoding::encode_transition(const vector<vector<int>> &previousStateVars,
                                      const vector<vector<int>> &nextStateVars, int fromTime) {
    const vector<int> &labelVars = allTimesLabelVars[fromTime];
    auto &updates = allTimesUpdates[fromTime];
    updates.assign(fts->get_size(), {});
    for (int ts = 0; ts < fts->get_size(); ts++) {
        vector<int> cur = previousStateVars[ts];   // sigma variables of the current position
        for (const SigmaLabelData &ld : data->factors[ts]) {
            const int l = labelVars[ld.label];
            // precondition
            vector<int> pre;
            for (int x : ld.applicable)
                pre.push_back(cur[x]);
            sat->impliesOr(l, pre);
            if (ld.changed.empty())
                continue;
            vector<int> next = cur;
            vector<pair<int, int>> upd;
            for (int s : ld.changed) {
                next[s] = sat->new_variable();
                DEBUG(sat->registerVariable(next[s], "sigma_" + to_string(ts) + "_" + to_string(ld.label) +
                                            "_" + to_string(s) + "@" + to_string(fromTime)));
                upd.emplace_back(s, next[s]);
                // not fired: unchanged
                sat->andImpliesOr(vector<int>{-l, cur[s]}, vector<int>{next[s]});
                sat->andImpliesOr(vector<int>{-l, next[s]}, vector<int>{cur[s]});
            }
            // effect and at most one target per source
            for (const auto &[x, ys] : ld.targets) {
                if (ys.size() == 1 && ys[0] == x && next[x] == cur[x])
                    continue;   // pure self-loop at an unchanged state
                vector<int> eff;
                for (int y : ys)
                    eff.push_back(next[y]);
                sat->andImpliesOr(vector<int>{l, cur[x]}, eff);
                for (size_t a = 0; a < ys.size(); a++)
                    for (size_t b = a + 1; b < ys.size(); b++)
                        sat->andImpliesOr(vector<int>{l, cur[x], next[ys[a]], next[ys[b]]}, vector<int>{});
            }
            // justification
            for (const auto &[s, xs] : ld.predecessors) {
                vector<int> from;
                for (int x : xs)
                    from.push_back(cur[x]);
                sat->andImpliesOr(vector<int>{l, next[s]}, from);
            }
            updates[ts].emplace_back(ld.label, move(upd));
            cur = move(next);
        }
        // the last sigma is the state at the end of the step
        for (size_t s = 0; s < cur.size(); s++) {
            if (cur[s] == nextStateVars[ts][s])
                continue;
            sat->implies(cur[s], nextStateVars[ts][s]);
            sat->implies(nextStateVars[ts][s], cur[s]);
        }
    }
}

vector<vector<int>> SigmaEncoding::extractIntermediateStates(vector<int> &selectedLabels,
                                                             vector<int> &currentLastState,
                                                             vector<int> & /*nextState*/,
                                                             int labelTimestep) {
    // selectedLabels are in label order; per factor, the state after a label is the changed
    // state whose sigma variable is true, or the previous state if none is
    const auto &updates = allTimesUpdates.at(labelTimestep);
    vector<map<int, const vector<pair<int, int>> *>> byLabel(fts->get_size());
    for (int ts = 0; ts < fts->get_size(); ts++)
        for (const auto &[label, upd] : updates[ts])
            byLabel[ts][label] = &upd;
    vector<vector<int>> states;
    vector<int> state = currentLastState;
    for (size_t i = 0; i + 1 < selectedLabels.size(); i++) {
        for (int ts = 0; ts < fts->get_size(); ts++) {
            auto it = byLabel[ts].find(selectedLabels[i]);
            if (it == byLabel[ts].end())
                continue;
            for (const auto &[s, var] : *it->second)
                if (ipasir_val(sat->solver, var) > 0) {
                    state[ts] = s;
                    break;
                }
        }
        states.push_back(state);
    }
    return states;
}

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------

static shared_ptr<SATEncodingFactory> _parse(options::OptionParser &parser) {
    parser.document_synopsis("Intermediate-state encoding over an FTS",
                             "R2E step semantics (labels in label order, several moves per factor "
                             "and step) with one variable per factor state changed by a label "
                             "(Symbolic Pattern Planning's sigma); see sat/sigma_encoding.h.");
    parser.add_option<shared_ptr<label_order_finder::LabelOrderFinder>>(
        "label_order", "label order of the step semantics", "label_order_linear()");
    parser.add_option<bool>("force_at_least_one_action",
                            "force that every time step contains at least one action", "false");
    static_constraints::add_options(parser);
    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    return make_shared<SigmaEncodingFactory>(opts);
}

static options::PluginShared<SATEncodingFactory> _plugin("sigma_sat", _parse);
}
