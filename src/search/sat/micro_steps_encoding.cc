#include "micro_steps_encoding.h"

#include <algorithm>
#include <cassert>
#include <set>

#include "../utils/logging.h"
#include "../utils/timer.h"
#include "ipasir.h"
#include "sat_encoder.h"

#include "../options/plugin.h"

using namespace std;
using namespace task_representation;

namespace micro_steps_encoding {

// ---------------------------------------------------------------------------
// Factory / plugin
// ---------------------------------------------------------------------------

MicroStepsEncodingFactory::MicroStepsEncodingFactory(const options::Options &opts)
    : SATEncodingFactory(opts.get<bool>("force_at_least_one_action")),
      useSelfloopOptimisation(opts.get<bool>("use_self_loop_optimisation")),
      dualThreshold(opts.get<int>("dual_threshold")),
      statisticsPrinted(false) {
}

static shared_ptr<sat_search::SATEncodingFactory> _parse_micro_steps_sat_factory(options::OptionParser &parser) {
    parser.add_option<bool>(
        "use_self_loop_optimisation",
        "variable sharing: only create new layer variables for the states moved by a label "
        "(source or target of one of its non-self-loop transitions) and skip labels that are "
        "irrelevant for a factor",
        "false");

    parser.add_option<int>(
        "dual_threshold",
        "arity threshold tau: a row of a micro-step is encoded by its dual constraint iff the "
        "dual clause has at most tau literals, and by its primal instances otherwise. "
        "0 yields a fully primal encoding, a negative value a fully dual one (tau = infinity)",
        "3");

    parser.add_option<bool>(
        "force_at_least_one_action",
        "force that every time step contains at least one action",
        "false");

    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<MicroStepsEncodingFactory>(opts);
}

static options::PluginShared<sat_search::SATEncodingFactory> _plugin_micro_steps_sat_factory("micro_steps_sat", _parse_micro_steps_sat_factory);

void MicroStepsEncodingFactory::computeTaskInfo() {
    auto taskInfo = make_shared<MicroStepsTaskInfo>();
    const int numLabels = fts->get_num_labels();

    // Same (reversed) default label order as the TAV and TAC encodings.
    taskInfo->labelOrder.resize(numLabels);
    taskInfo->labelPosition.resize(numLabels);
    for (int pos = 0; pos < numLabels; pos++) {
        int label = numLabels - 1 - pos;
        taskInfo->labelOrder[pos] = label;
        taskInfo->labelPosition[label] = pos;
    }

    taskInfo->steps.resize(fts->get_size());
    for (int ts = 0; ts < fts->get_size(); ts++) {
        const TransitionSystem &tss = fts->get_ts(ts);
        for (int pos = 0; pos < numLabels; pos++) {
            int label = taskInfo->labelOrder[pos];
            if (useSelfloopOptimisation && tss.isIrrelevantLabel(label))
                continue; // the layer disappears

            MicroStep step;
            step.label = label;
            step.position = pos;
            set<pair<int, int>> transitions;
            set<int> sources, targets, moved;
            for (const Transition &t : tss.get_transitions_with_label(label)) {
                transitions.insert({t.src, t.target});
                sources.insert(t.src);
                targets.insert(t.target);
                if (t.src != t.target) {
                    moved.insert(t.src);
                    moved.insert(t.target);
                }
            }
            step.transitions.assign(transitions.begin(), transitions.end());
            step.sources.assign(sources.begin(), sources.end());
            step.targets.assign(targets.begin(), targets.end());
            step.moved.assign(moved.begin(), moved.end());
            taskInfo->steps[ts].push_back(std::move(step));
        }
    }
    info = taskInfo;
}

void MicroStepsEncodingFactory::initialize() {
    utils::Timer sat_init_timer;
    cout << "Initialising" << fts << endl;
    cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;
    computeTaskInfo();

    long long layersWithNewVars = 0, sharedLayerVars = 0, fullLayerVars = 0;
    for (int ts = 0; ts < fts->get_size(); ts++) {
        for (const MicroStep &step : info->steps[ts]) {
            size_t newVars = useSelfloopOptimisation ? step.moved.size() : fts->get_ts(ts).get_size();
            if (newVars) layersWithNewVars++;
            sharedLayerVars += newVars;
        }
        fullLayerVars += (long long)fts->get_num_labels() * fts->get_ts(ts).get_size();
    }
    cout << "Micro-steps: dual threshold " << dualThreshold
         << ", variable sharing " << (useSelfloopOptimisation ? "on" : "off") << endl;
    cout << "Micro-steps: layer variables per time step " << sharedLayerVars
         << " (without sharing: " << fullLayerVars << "), layers with new variables "
         << layersWithNewVars << endl;
    cout << "SAT init time: " << sat_init_timer << endl;
}

unique_ptr<sat_search::SATEncoding> MicroStepsEncodingFactory::createEncodingInstance(std::shared_ptr<sat_capsule> capsule) {
    if (!info)
        computeTaskInfo();
    bool oldStatisticsPrinted = statisticsPrinted;
    statisticsPrinted = true;
    return make_unique<MicroStepsEncoding>(capsule, fts, forceAtLeastOneAction, useSelfloopOptimisation,
                                           dualThreshold, oldStatisticsPrinted, info);
}

void MicroStepsStatistics::print() const {
    cout << "Micro-steps statistics (first time step):" << endl;
    cout << "  layer variables       : " << layerVars << endl;
    cout << "  at-most-one aux vars  : " << amoAuxVars << endl;
    cout << "  (22) at-least-one     : " << c22_alo << endl;
    cout << "  (22) at-most-one      : " << c22_amo << endl;
    cout << "  (23) frame axioms     : " << c23 << endl;
    cout << "  (24) src -> -label    : " << c24 << endl;
    cout << "  (25) label -> -tgt    : " << c25 << endl;
    cout << "  (26) src -> -tgt      : " << c26 << endl;
    cout << "  (27) src+label -> -src: " << c27 << endl;
    cout << "  (28) dual label src   : " << d28 << endl;
    cout << "  (29) dual label tgt   : " << d29 << endl;
    cout << "  (30) dual src guarded : " << d30 << endl;
    cout << "  (31) dual src         : " << d31 << endl;
    cout << "  (32) dual tgt guarded : " << d32 << endl;
    cout << "  (33) dual tgt         : " << d33 << endl;
    cout << "  links to next state   : " << links << endl;
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

MicroStepsEncoding::MicroStepsEncoding(
    shared_ptr<sat_capsule> capsule,
    const shared_ptr<FTSTask> &fts,
    bool forceAtLeastOneAction,
    bool useSelfloopOptimisation,
    int dualThreshold,
    bool statisticsPrinted,
    shared_ptr<const MicroStepsTaskInfo> info)
    : CommonEncoding(capsule, fts, forceAtLeastOneAction, false, useSelfloopOptimisation),
      dualThreshold(dualThreshold),
      statisticsPrinted(statisticsPrinted),
      info(std::move(info)) {
    labelOrder = this->info->labelOrder;
    relevantLabels.resize(fts->get_size());
    for (int ts = 0; ts < fts->get_size(); ts++)
        for (const MicroStep &step : this->info->steps[ts])
            relevantLabels[ts].push_back(step.label);
}

void MicroStepsEncoding::generateAdditionalVariables(int /*fromTime*/) {
}

void MicroStepsEncoding::encode_transition(const vector<vector<int>> & /*previousStateVars*/,
                                           const vector<vector<int>> & /*nextStateVars*/, int /*fromTime*/) {
}

void MicroStepsEncoding::encode_frame_axioms(const vector<vector<int>> & /*previousStateVars*/,
                                             const vector<vector<int>> & /*nextStateVars*/, int /*fromTime*/) {
}

bool MicroStepsEncoding::useDual(size_t arity) const {
    return dualThreshold < 0 || arity <= (size_t)dualThreshold;
}

void MicroStepsEncoding::encode(int fromTime, int toTime) {
    // layer 0: state variables of fromTime (generate if necessary)
    auto preStateVarFind = allTimesStateVars.find(fromTime);
    const vector<vector<int>> &previousStateVars = (preStateVarFind == allTimesStateVars.end()) ?
        (allTimesStateVars[fromTime] = generateStateVars()) :
        preStateVarFind->second;

    assert(!allTimesLabelVars.contains(fromTime));
    allTimesLabelVars[fromTime] = generateLabelVars();
    if (forceAtLeastOneAction)
        sat->atLeastOne(allTimesLabelVars[fromTime]);

    MicroStepsStatistics stats;
    allTimesLayers[fromTime].resize(fts->get_size());
    vector<vector<int>> lastLayers(fts->get_size());
    for (int ts = 0; ts < fts->get_size(); ts++)
        lastLayers[ts] = encodeFactor(ts, previousStateVars[ts], fromTime, stats);

    auto nextStateVarFind = allTimesStateVars.find(toTime);
    if (nextStateVarFind == allTimesStateVars.end()) {
        // the last layer of each factor is the state of time step toTime
        allTimesStateVars[toTime] = std::move(lastLayers);
    } else {
        // The state variables of toTime already exist (e.g., the goal side of
        // the incremental search). Link them to the last layer.
        const vector<vector<int>> &nextStateVars = nextStateVarFind->second;
        for (int ts = 0; ts < fts->get_size(); ts++) {
            for (size_t s = 0; s < nextStateVars[ts].size(); s++) {
                if (lastLayers[ts][s] == nextStateVars[ts][s]) continue;
                sat->implies(lastLayers[ts][s], nextStateVars[ts][s]);
                sat->implies(nextStateVars[ts][s], lastLayers[ts][s]);
                stats.links += 2;
            }
        }
    }

    if (!statisticsPrinted) {
        statisticsPrinted = true;
        stats.print();
    }
}

void MicroStepsEncoding::encodeLayerExactlyOne(const vector<int> &layer, const vector<int> &newStates,
                                               const vector<char> &isNew, MicroStepsStatistics &stats) {
    sat->atLeastOne(layer);
    stats.c22_alo++;

    if (newStates.size() == layer.size()) {
        int before = sat->get_number_of_clauses();
        sat->atMostOne(layer);
        stats.c22_amo += sat->get_number_of_clauses() - before;
        return;
    }

    // Variable sharing: the at-most-one constraint between two shared states
    // is already enforced by an earlier layer. Only pairs involving at least
    // one new variable are needed.
    vector<int> newVars, sharedVars;
    for (size_t s = 0; s < layer.size(); s++)
        (isNew[s] ? newVars : sharedVars).push_back(layer[s]);

    int before = sat->get_number_of_clauses();
    if (layer.size() < 256) {
        sat->atMostOneBinomial(newVars);
        for (int a : newVars)
            for (int b : sharedVars)
                sat->impliesNot(a, b);
    } else {
        // Large layer: introduce one variable "some shared state is true" and
        // make the new states mutually exclusive with it.
        int someShared = sat->new_variable();
        DEBUG(sat->registerVariable(someShared, "MS-some-shared-state"));
        stats.amoAuxVars++;
        for (int b : sharedVars)
            sat->implies(b, someShared);
        newVars.push_back(someShared);
        sat->atMostOne(newVars);
    }
    stats.c22_amo += sat->get_number_of_clauses() - before;
}

vector<int> MicroStepsEncoding::encodeFactor(int ts, const vector<int> &layer0, int fromTime,
                                             MicroStepsStatistics &stats) {
    const int numStates = fts->get_ts(ts).get_size();
    vector<RecordedLayer> &recorded = allTimesLayers[fromTime][ts];

    // scratch data, reset after every micro-step for the touched states only
    vector<vector<int>> tgtOf(numStates), srcOf(numStates);
    vector<char> selfLoop(numStates, 0), isSrc(numStates, 0), isTgt(numStates, 0), isNew(numStates, 0);
    vector<char> dualSrc(numStates, 0), dualTgt(numStates, 0), mark(numStates, 0);
    vector<int> allStates(numStates);
    for (int s = 0; s < numStates; s++) allStates[s] = s;

    vector<int> prev = layer0;
    for (const MicroStep &step : info->steps[ts]) {
        const int lab = getLabelSATVar(step.label, fromTime);

        for (auto [s, t] : step.transitions) {
            tgtOf[s].push_back(t);
            srcOf[t].push_back(s);
            if (s == t) selfLoop[s] = 1;
        }
        for (int s : step.sources) isSrc[s] = 1;
        for (int t : step.targets) isTgt[t] = 1;

        // Row choices: dual iff the arity of the dual clause is <= tau.
        // Source row of s: (31) if l_k loops at s, else (30); symmetric for targets.
        for (int s = 0; s < numStates; s++) {
            dualSrc[s] = useDual((selfLoop[s] ? 1 : 2) + tgtOf[s].size());
            dualTgt[s] = useDual((selfLoop[s] ? 1 : 2) + srcOf[s].size());
        }
        const bool dual28 = useDual(1 + step.sources.size());
        const bool dual29 = useDual(1 + step.targets.size());

        // Layer k: new variables for every state (no sharing) or for M_k only.
        const vector<int> &newStates = useSelfloopOptimisation ? step.moved : allStates;
        vector<int> next = prev;
        RecordedLayer layer{step.position, {}};
        for (int s : newStates) {
            next[s] = sat->new_variable();
            isNew[s] = 1;
            layer.newVars.push_back({s, next[s]});
            DEBUG(sat->registerVariable(next[s], "MS TS:" + to_string(ts) + ";Val:" + to_string(s) +
                                        ";Pos:" + to_string(step.position) + ";Label:" +
                                        to_string(step.label) + "@" + to_string(fromTime)));
        }
        stats.layerVars += newStates.size();

        if (!newStates.empty()) {
            // (22) exactly one state per layer
            encodeLayerExactlyOne(next, newStates, isNew, stats);

            // (23) frame axioms: s^{k-1} & -l -> s^k (tautology for shared states)
            for (int s : newStates) {
                sat->andImplies(prev[s], -lab, next[s]);
                stats.c23++;
            }
        }

        // (28) l -> OR_{s in src(l)} s^{k-1}
        if (dual28) {
            vector<int> rhs;
            for (int s : step.sources) rhs.push_back(prev[s]);
            sat->impliesOr(lab, rhs);
            stats.d28++;
        }
        // (29) l -> OR_{t in tgt(l)} t^k
        if (dual29) {
            vector<int> rhs;
            for (int t : step.targets) rhs.push_back(next[t]);
            sat->impliesOr(lab, rhs);
            stats.d29++;
        }

        // (24) s^{k-1} -> -l for s not in src(l). Coincides with the guarded
        // dual (30) of s, so it is generated if that row is dual or if (28) is not.
        for (int s = 0; s < numStates; s++) {
            if (isSrc[s]) continue;
            if (dualSrc[s] || !dual28) {
                sat->impliesNot(prev[s], lab);
                stats.c24++;
            }
        }

        // (25) l -> -t^k for t not in tgt(l). For shared states it coincides
        // with (24). Coincides with the guarded dual (32) of t.
        for (int t : newStates) {
            if (isTgt[t]) continue;
            if (dualTgt[t] || !dual29) {
                sat->impliesNot(lab, next[t]);
                stats.c25++;
            }
        }

        // Dual source rows (states not in src(l) are handled by (24) above;
        // the dual rows of shared states are tautologies).
        for (int s : step.sources) {
            if (!isNew[s] || !dualSrc[s]) continue;
            vector<int> rhs;
            for (int t : tgtOf[s]) rhs.push_back(next[t]);
            if (selfLoop[s]) {
                sat->impliesOr(prev[s], rhs);            // (31) s^{k-1} -> OR t^k
                stats.d31++;
            } else {
                sat->andImpliesOr(prev[s], lab, rhs);    // (30) s^{k-1} & l -> OR t^k
                stats.d30++;
            }
        }

        // Dual target rows (states not in tgt(l) are handled by (25) above)
        for (int t : step.targets) {
            if (!isNew[t] || !dualTgt[t]) continue;
            vector<int> rhs;
            for (int s : srcOf[t]) rhs.push_back(prev[s]);
            if (selfLoop[t]) {
                sat->impliesOr(next[t], rhs);            // (33) t^k -> OR s^{k-1}
                stats.d33++;
            } else {
                sat->andImpliesOr(lab, next[t], rhs);    // (32) l & t^k -> OR s^{k-1}
                stats.d32++;
            }
        }

        // (26) s^{k-1} -> -t^k for s != t and (s,l,t) not in T, unless the
        // source row of s or the target row of t is dual. Pairs involving a
        // shared state coincide with at-most-one clauses of layer k-1 or k.
        for (int s : newStates) {
            if (dualSrc[s]) continue;
            for (int t : tgtOf[s]) mark[t] = 1;
            for (int t : newStates) {
                if (t == s || mark[t] || dualTgt[t]) continue;
                sat->impliesNot(prev[s], next[t]);
                stats.c26++;
            }
            for (int t : tgtOf[s]) mark[t] = 0;
        }

        // (27) s^{k-1} & l -> -s^k for s in src(l) without self-loop, unless
        // the source or target row of s is dual.
        for (int s : step.sources) {
            if (!isNew[s] || selfLoop[s] || dualSrc[s] || dualTgt[s]) continue;
            sat->andImplies(prev[s], lab, -next[s]);
            stats.c27++;
        }

        // reset scratch data
        for (auto [s, t] : step.transitions) {
            tgtOf[s].clear();
            srcOf[t].clear();
            selfLoop[s] = 0;
        }
        for (int s : step.sources) isSrc[s] = 0;
        for (int t : step.targets) isTgt[t] = 0;
        for (int s : newStates) isNew[s] = 0;

        if (!newStates.empty()) {
            recorded.push_back(std::move(layer));
            prev = std::move(next);
        }
    }
    return prev;
}

// ---------------------------------------------------------------------------
// Plan extraction
// ---------------------------------------------------------------------------

vector<vector<int>> MicroStepsEncoding::extractIntermediateStates(vector<int> &selectedLabels,
                                                                  vector<int> &currentLastState,
                                                                  vector<int> &nextState,
                                                                  int labelTimestep) {
    // intermediate states after all selected labels but the last
    vector<vector<int>> intermediateStates(selectedLabels.size() - 1, vector<int>(fts->get_size()));
    const vector<vector<RecordedLayer>> &layers = allTimesLayers.at(labelTimestep);

    for (int ts = 0; ts < fts->get_size(); ts++) {
        int state = currentLastState[ts];
        size_t nextLayer = 0;
        // Walk the layers in label order. If none of the new variables of a
        // layer is true, the factor is in a shared state and did not move.
        auto advanceUpTo = [&](int position) {
            for (; nextLayer < layers[ts].size() && layers[ts][nextLayer].position <= position; nextLayer++) {
                for (auto [s, var] : layers[ts][nextLayer].newVars) {
                    if (ipasir_val(sat->solver, var) > 0) {
                        state = s;
                        break;
                    }
                }
            }
        };
        for (size_t l = 0; l + 1 < selectedLabels.size(); l++) {
            advanceUpTo(info->labelPosition[selectedLabels[l]]);
            intermediateStates[l][ts] = state;
        }
        advanceUpTo(info->labelOrder.size());
        assert(state == nextState[ts]);
        (void)nextState;
    }
    return intermediateStates;
}

} // namespace micro_steps_encoding
