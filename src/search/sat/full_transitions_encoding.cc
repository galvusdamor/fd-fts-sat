#include "full_transitions_encoding.h"

#include <numeric>

#include "../utils/logging.h"
#include "ipasir.h"
#include "sat_encoder.h"

#include "../options/plugin.h"

using namespace std;
using namespace task_representation;

namespace transitions_encoding {

FullTransitionsEncodingFactory::FullTransitionsEncodingFactory(const options::Options &opts): SATEncodingFactory(opts.get<bool>("force_at_least_one_action")),
	useSelfloopOptimisation(opts.get<bool>("use_self_loop_optimisation")), useLabelsInEffectsConstraints(opts.get<bool>("use_labels_in_effects_constraints"))
	 {
	statisticsPrinted = false;
}



static shared_ptr<sat_search::SATEncodingFactory> _parse_full_transitions_sat_factory(options::OptionParser &parser) {

	parser.add_option<bool>(
    	"use_self_loop_optimisation",
    	"use optimisation for self loops",
    	"false");
    
    parser.add_option<bool>(
    	"use_labels_in_effects_constraints",
    	"use labels instead of transitions when encoding constraints for effects in an attempt to reduce size of clauses",
    	"false");

	parser.add_option<bool>(
    	"force_at_least_one_action",
    	"force that every time step contains at least one action",
    	"false");

    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<FullTransitionsEncodingFactory>(opts);
}

static options::PluginShared<sat_search::SATEncodingFactory> _plugin_full_transitions_sat_factory("full_transitions_sat", _parse_full_transitions_sat_factory);

void FullTransitionsEncodingFactory::initialize() {
	utils::Timer sat_init_timer;
	cout << "Initialising" << fts << endl;
	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

    cout << "SAT init time: " << sat_init_timer << endl;
}



unique_ptr<sat_search::SATEncoding> FullTransitionsEncodingFactory::createEncodingInstance(std::shared_ptr<sat_capsule> capsule){
	//bool oldStatisticsPrinted = statisticsPrinted;
	statisticsPrinted = true;
	return make_unique<FullTransitionsEncoding>(capsule, fts, forceAtLeastOneAction, useSelfloopOptimisation, useLabelsInEffectsConstraints);
}


// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

FullTransitionsEncoding::FullTransitionsEncoding(
    shared_ptr<sat_capsule>                         capsule,
    const shared_ptr<FTSTask>&                      fts,
    bool                                            forceAtLeastOneAction,
    bool                                            useSelfloopOptimisation,
    bool                                            useLabelsInEffectsConstraints)
    : TransitionsEncoding(capsule, fts, forceAtLeastOneAction,
                           useSelfloopOptimisation, useLabelsInEffectsConstraints)
{
    // Populate relevantLabels (inherited from LabelEncoding) here because we
    // are the only class that knows useSelfloopOptimisation, and fts is already
    // available from the base constructor.
    //
    // When useSelfloopOptimisation is false every label is relevant.
    // When true, pure self-loop labels (irrelevant to state change) are skipped.
    relevantLabels.resize(fts->get_size());
    for (int ts = 0; ts < fts->get_size(); ts++) {
        if (useSelfloopOptimisation) {
            for (int label = 0; label < fts->get_num_labels(); label++)
                if (!isIrrelevantLabel(ts, label))
                    relevantLabels[ts].push_back(label);
        } else {
            relevantLabels[ts].resize(fts->get_num_labels());
            iota(relevantLabels[ts].begin(), relevantLabels[ts].end(), 0);
        }
    }
    labelOrder.resize(fts->get_num_labels());
    for (int label = 0; label < fts->get_num_labels(); label++) {
        labelOrder[label] = label;
    }
}

// ---------------------------------------------------------------------------
// Transition predicate
// ---------------------------------------------------------------------------

bool FullTransitionsEncoding::has_to_encode_transition(Transition transition) {
    if (!useSelfloopOptimisation) return true;
    return !isSelfLoop(transition);
}

// ---------------------------------------------------------------------------
// Variable generation
// ---------------------------------------------------------------------------

void FullTransitionsEncoding::appendNewTransitionVars(map<int, map<int, vector<pair<Transition, int>>>>& newVars){
    allTimesTransitionVars.push_back(newVars);
}

void FullTransitionsEncoding::generateTransitionVars(int /*fromTime*/) {
    map<int, map<int, vector<pair<Transition, int>>>> transitionVars;

    for (int ts = 0; ts < fts->get_size(); ts++) {
        for (int label = 0; label < fts->get_num_labels(); label++) {
            if (useSelfloopOptimisation && isIrrelevantLabel(ts, label))
                continue;
            auto transitions = fts->get_ts(ts).get_transitions_with_label(getOrderedLabel(label));
            vector<int> SATVars;
            for (size_t t = 0; t < transitions.size(); t++) {
                if (useSelfloopOptimisation && transitions[t].src == transitions[t].target) {
                    transitionVars[ts][label].push_back({transitions[t], -1});
                    continue;
                }
                int transitionVar = sat->new_variable();
                SATVars.push_back(transitionVar);
                transitionVars[ts][label].push_back({transitions[t], transitionVar});
                DEBUG(sat->registerVariable(
                    transitionVar,
                    "TS:" + to_string(ts) + ";Label:" + to_string(label) +
                    "--" + to_string(transitions[t].src) +
                    "->" + to_string(transitions[t].target)));
            }
            sat->atMostOne(SATVars);
        }
    }
    appendNewTransitionVars(transitionVars);
}

// ---------------------------------------------------------------------------
// Precondition / effect appenders
// ---------------------------------------------------------------------------

void FullTransitionsEncoding::appendPreconditions(vector<int>& impliesOrPrec, int ts, int relevantLabelPrec, int src){
    for (auto& [transition, var] : allTimesTransitionVars.back()[ts][getRelevantLabel(ts, relevantLabelPrec)]){
        if (transition.target == src && !isSelfLoop(transition))
            impliesOrPrec.push_back(var);
    }
}

void FullTransitionsEncoding::appendEffects(vector<int>& impliesOrEff, int ts, int relevantLabelEff, int target, int time){
    for (auto& [transition, var] : allTimesTransitionVars.back()[ts][getRelevantLabel(ts, relevantLabelEff)]){
        if (transition.target != target && transition.src == target && !isSelfLoop(transition)){
            if(!useLabelsInEffectsConstraints || (useSelfloopOptimisation && containsSelfLoops(ts, getRelevantLabel(ts, relevantLabelEff)))){
                impliesOrEff.push_back(var);
            }else{
                impliesOrEff.push_back(getLabelSATVar(getRelevantLabel(ts, relevantLabelEff), time));
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// SAT-variable lookup (lazily cached)
// ---------------------------------------------------------------------------

vector<int> FullTransitionsEncoding::get_all_sat_vars_moving_to_state(int ts, int label, int state, int fromTime){
    auto access = make_tuple(ts, label, state, fromTime);
    auto it = moving_to_state_vars.find(access);
    if (it == moving_to_state_vars.end()) {
        vector<int> ret;
        for (auto& [transition, var] : allTimesTransitionVars.back()[ts][getRelevantLabel(ts, label)])
            if (transition.target == state && has_to_encode_transition(transition)){
                //cout << var << endl;
                ret.push_back(var);
            }
        return moving_to_state_vars[access] = ret;
    }
    return it->second;
}

vector<int> FullTransitionsEncoding::get_all_sat_vars_moving_from_state(int ts, int label, int state, int fromTime){
    auto access = make_tuple(ts, label, state, fromTime);
    auto it = moving_from_state_vars.find(access);
    if (it == moving_from_state_vars.end()) {
        vector<int> ret;
        for (auto& [transition, var] : allTimesTransitionVars.back()[ts][getRelevantLabel(ts, label)])
            if (transition.src == state && has_to_encode_transition(transition)){
                ret.push_back(var);
            }
        return moving_from_state_vars[access] = ret;
    }
    return it->second;
}

vector<int> FullTransitionsEncoding::get_all_non_encoded_self_loop_states(int ts, int label){
    auto access = make_tuple(ts, label);
    auto it = states_with_non_encoded_transitions.find(access);
    if (it == states_with_non_encoded_transitions.end()) {
        set<int> ret;
        for (Transition transition : fts->get_ts(ts).get_transitions_with_label(getOrderedLabel(getRelevantLabel(ts, label))))
            if (!has_to_encode_transition(transition))
                ret.insert(transition.target);
        vector<int> ret_vec(ret.begin(), ret.end());
        return states_with_non_encoded_transitions[access] = ret_vec;
    }
    return it->second;
}

// ---------------------------------------------------------------------------
// Label / transition consistency
// ---------------------------------------------------------------------------

void FullTransitionsEncoding::encode_label_consistency(int ts, int relevantLabelIndex, int time) {
    vector<int> labelTransitionSATVars;
    for (auto& [transition, var] : allTimesTransitionVars.back()[ts][getRelevantLabel(ts, relevantLabelIndex)])
        if (has_to_encode_transition(transition))
            labelTransitionSATVars.push_back(var);

    if (!labelTransitionSATVars.empty()) {
        if ( (useSelfloopOptimisation && !containsSelfLoops(ts, getRelevantLabel(ts, relevantLabelIndex)))
                || !useSelfloopOptimisation)
            sat->impliesOr(getLabelSATVar(getRelevantLabel(ts, relevantLabelIndex), time), labelTransitionSATVars);
        for (int v : labelTransitionSATVars)
            sat->implies(v, getLabelSATVar(getRelevantLabel(ts, relevantLabelIndex), time));
    }
}

// ---------------------------------------------------------------------------
// Encoding entry points
// ---------------------------------------------------------------------------

void FullTransitionsEncoding::encode_transition(const vector<vector<int>>& previousStateVars, const vector<vector<int>>& nextStateVars, int fromTime){
    for (int ts = 0; ts < fts->get_size(); ts++) {
        for (size_t relevantLabel = 0; relevantLabel < relevantLabels[ts].size(); relevantLabel++){
            encodeRegularPreconditionsAndEffects(ts, relevantLabel, previousStateVars, nextStateVars, fromTime);
            encode_label_consistency(ts, relevantLabel, fromTime);
        }
        encode_r2_chains(ts, fromTime);
    }
}

void FullTransitionsEncoding::encode_frame_axioms(const vector<vector<int>>& previousStateVars, const vector<vector<int>>& nextStateVars, int fromTime){
    if(forceAtLeastOneAction && !useSelfloopOptimisation)
        return;
    for (int ts = 0; ts < fts->get_size(); ts++) {
        for (int state = 0; state < fts->get_ts(ts).get_size(); state++) {
            set<int> negatedLabelsOrTransitions;
            negatedLabelsOrTransitions.insert(previousStateVars[ts][state]);

            for (size_t relevantLabel = 0; relevantLabel < relevantLabels[ts].size(); relevantLabel++){
                int globalLabel = relevantLabels[ts][relevantLabel];

                if(!useSelfloopOptimisation){
                    negatedLabelsOrTransitions.insert(-getLabelSATVar(globalLabel, fromTime));
                    continue;
                }

                if (isAlwaysSelfLoop(ts, globalLabel))
                    continue;

                bool mixed = containsSelfLoops(ts, globalLabel);

                if (!mixed) {
                    negatedLabelsOrTransitions.insert(-getLabelSATVar(globalLabel, fromTime));
                } else {
                    for (auto& [transition, var] : allTimesTransitionVars.back()[ts][globalLabel])
                        if (!isSelfLoop(transition))
                            negatedLabelsOrTransitions.insert(-var);
                }
            }
            sat->andImplies(negatedLabelsOrTransitions, nextStateVars[ts][state]);
        }
    }
}

vector<vector<int>> FullTransitionsEncoding::extractIntermediateStates(vector<int>& selectedLabels, vector<int>& currentLastState, vector<int>& /*nextState*/, int labelTimestep){
    // TODO: walk allTimesTransitionVars for the relevant timestep to recover
    // intermediate states between parallel label executions, analogous to
    // SATSearch::checkSolution in sat_search.cc lines 556-603.
    vector<vector<int>> intermediateStates;
	for(size_t l = 0 ; l < selectedLabels.size() - 1 ; l++){
		vector<int> intermediateState;
		for(int ts = 0 ; ts < fts->get_size() ; ts++){
            bool found_selected_transition = false;
			for(auto& [transition, var] : allTimesTransitionVars[labelTimestep-1][ts][selectedLabels[l]]){
                if(useSelfloopOptimisation && var == -1)
                    continue;
                if(ipasir_val(sat->solver, var) > 0){
                    intermediateState.push_back(transition.target);
                    found_selected_transition = true;
                    break;
                }
            }
            if(!found_selected_transition && useSelfloopOptimisation){
                int sameValue;
                if(intermediateStates.size() > 0){
                    sameValue = intermediateStates.back()[ts];
                }else{
                    sameValue = currentLastState[ts];
                }
                intermediateState.push_back(sameValue);
            }
		}
        assert(intermediateState.size() == (size_t)fts->get_size());
		intermediateStates.push_back(intermediateState);
        intermediateState.clear();
	}
	return intermediateStates;
}

// ---------------------------------------------------------------------------
// Requirers / opposers / achievers
// ---------------------------------------------------------------------------

void FullTransitionsEncoding::encode_r2_chains(int ts, int time){
    map<int, vector<int>> auxVars;
    int numRelevantLabels = getNumRelevantLabels(ts);
    for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
        vector<vector<int>> eventVars(numRelevantLabels);
        vector<set<int>> opposers(numRelevantLabels);
        vector<set<int>> requirers(numRelevantLabels);
        vector<set<int>> achievers(numRelevantLabels);
        for (int relevantLabel = 0; relevantLabel < numRelevantLabels; relevantLabel++) {
            int index = 0;
            for (auto& [transition, var] : allTimesTransitionVars.back()[ts][getRelevantLabel(ts, relevantLabel)]){
                if(useSelfloopOptimisation && transition.src == transition.target)
                    continue;
                eventVars[relevantLabel].push_back(var);
                if(transition.target != states && transition.src != transition.target)
                    opposers[relevantLabel].insert(index);
                if(transition.src == states)
                    requirers[relevantLabel].insert(index);
                if(transition.target == states && transition.src != transition.target)
                    achievers[relevantLabel].insert(index);
                index++;
            }
        }

        auxVars[states] = sat->compute_chains(opposers, requirers, achievers, eventVars, 0);//No guard variable (0 as the last parameter)

    }

    if(useSelfloopOptimisation){
        for (int relevantLabel = 1; relevantLabel < numRelevantLabels; relevantLabel++) {
            vector<int> auxVarsInPrec;
			vector<int> otherTransitions;
            for (auto& [transition, var] : allTimesTransitionVars.back()[ts][getRelevantLabel(ts, relevantLabel)]){
                if(transition.src == transition.target){
                    auxVarsInPrec.push_back(auxVars[transition.src][relevantLabel-1]);//Previous Aux Var
                }else{
                    otherTransitions.push_back(var);
                }
            }

            if(auxVarsInPrec.size() == 0) continue;
            int labelSATVar = getLabelSATVar(getRelevantLabel(ts, relevantLabel), time);
            otherTransitions.push_back(-labelSATVar);
            sat->andImpliesOr(auxVarsInPrec, otherTransitions);//In a label with mixed transitions: If all Aux Vars are active (disabling the self-loops), then either another transition was executed or the label was not executed at all
        }
    }
	
}

} // namespace transitions_encoding