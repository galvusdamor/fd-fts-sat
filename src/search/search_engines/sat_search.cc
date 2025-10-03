#include <chrono>
#include <thread>
#include <ctime>
#include <atomic>

#include "sat_search.h"

// #include "../plugins/options.h"
#include "../utils/logging.h"
#include "../utils/timer.h"
#include "ipasir.h"
#include "sat_encoder.h"
#include "../task_utils/label_order_finder.h"

using namespace std;
using namespace task_representation;


bool kissat_quietMode;

extern "C"{
	void ipasir_terminate (void * solver);
}

namespace fts_base_manager{
	FTSBaseManager::FTSBaseManager(std::shared_ptr<task_representation::FTSTask> __fts):fts(__fts){}
}

namespace transitions_encoding{

TransitionsEncoding::TransitionsEncoding(std::shared_ptr<task_representation::FTSTask> __fts) : FTSBaseManager(__fts){}

FullTransitionsEncoding::FullTransitionsEncoding(bool __no_selfloop_SATvars, std::shared_ptr<task_representation::FTSTask> __fts):
	TransitionsEncoding(__fts), no_selfloop_SATvars(__no_selfloop_SATvars){}

void FullTransitionsEncoding::appendNewTransitionVars(map<int, map<int, vector<pair<Transition, int>>>> &newVars){
	allTimesTransitionVars.push_back(newVars);
}

void FullTransitionsEncoding::generateTransitionVars(void* solver, sat_capsule &capsule) {
	map<int, map<int, vector<pair<Transition, int>>>> transitionVars;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int label = 0 ; label < fts->get_num_labels() ; label++){
			if(no_selfloop_SATvars && lm->isIrrelevantLabel(ts, label)){
				continue;
			}
			auto transitions = fts->get_ts(ts).get_transitions_with_label(lm->getOrderedLabel(label));
			vector<int> SATVars;
			for(size_t t = 0 ; t < transitions.size() ; t++){
				if(no_selfloop_SATvars && transitions[t].src == transitions[t].target){
					transitionVars[ts][label].push_back({transitions[t],-1});
					continue;
				}
				int transitionVar = capsule.new_variable();
				SATVars.push_back(transitionVar);
				transitionVars[ts][label].push_back({transitions[t],transitionVar});
				DEBUG(capsule.registerVariable(transitionVar,"TS:"+to_string(ts)+";Label:"+to_string(label)+"--"+to_string(transitions[t].src)+"->"+to_string(transitions[t].target)));
			}
			atMostOne(solver, capsule, SATVars);
		}
	}
	appendNewTransitionVars(transitionVars);
}

void FullTransitionsEncoding::appendPreconditions(vector<int> &impliesOrPrec, int ts, int relevantLabelPrec, int src){
	for(pair<Transition, int> transition_prec : allTimesTransitionVars.back()[ts][lm->getRelevantLabel(ts, relevantLabelPrec)]){
		if(transition_prec.first.target == src && !isSelfLoop(transition_prec.first)){
			//Save transition vars whose target matches the source of the transition we are currently looking at
			impliesOrPrec.push_back(transition_prec.second);
		}
	}
}

vector<int> TransitionsEncoding::getPreconditions(int src, int ts, int relevantLabel){
	vector<int> impliesOrPrec;
	for(int relevantLabelPrec = 0 ; relevantLabelPrec < relevantLabel ; relevantLabelPrec++){
		appendPreconditions(impliesOrPrec, ts, relevantLabelPrec, src);
	}
	return impliesOrPrec;
}

void FullTransitionsEncoding::appendEffects(vector<int> &impliesOrEff, int ts, int relevantLabelEff, int target){
	for(pair<Transition, int> transition_eff : allTimesTransitionVars.back()[ts][lm->getRelevantLabel(ts, relevantLabelEff)]){
		if(transition_eff.first.target != target && !isSelfLoop(transition_eff.first)){
			//Saving transitions that have a different target than the current transition and that are from a label that appears later
			impliesOrEff.push_back(transition_eff.second);
		}
	}
}

vector<int> TransitionsEncoding::get_all_later_vars_moving_to_different_state(int target, int ts, int relevantLabel){
	tuple<int,int,int> access = make_tuple(target,ts,relevantLabel);
	auto it = moving_to_different_state_vars.find(access);
	if (it == moving_to_different_state_vars.end()){
		vector<int> impliesOrEff;
		for(int relevantLabelEff = relevantLabel+1 ; relevantLabelEff < lm->getNumRelevantLabels(ts) ; relevantLabelEff++){
			appendEffects(impliesOrEff, ts, relevantLabelEff, target);
		}
		moving_to_different_state_vars[access] = impliesOrEff;
		return impliesOrEff;
	}
	return it->second;
}


bool FullTransitionsEncoding::has_to_encode_transition(Transition transition){
	if (!no_selfloop_SATvars) return true;
	return !isSelfLoop(transition);
}


bool SplitTransitionsEncoding::has_to_encode_transition(Transition transition){
	return transition.src > 0;//the same as "return true" but just a "hacky way" to still use the argument so the compiler doesn't complain
	//return true;
}


/**
 * all SAT variables that represent moving *to* state "state" using the label "label" -- except for self-loop transitions
*/
vector<int> FullTransitionsEncoding::get_all_sat_vars_moving_to_state(int ts, int label, int state){
	tuple<int,int,int> access = make_tuple(ts,label,state);
	auto it = moving_to_state_vars.find(access);
	if (it == moving_to_state_vars.end()){
		vector<int> ret;
		for(/* pair<Transition, int> */ auto transition : allTimesTransitionVars.back()[ts][lm->getRelevantLabel(ts, label)])
			if (transition.first.target == state && has_to_encode_transition(transition.first))
				ret.push_back(transition.second);
		moving_to_state_vars[access] = ret;
		return ret;
	}
	return it->second;
}


/**
 * all states that have a non-encoded self-loop for this label
*/
vector<int> FullTransitionsEncoding::get_all_non_encoded_self_loop_states(int ts, int label){
	tuple<int,int> access = make_tuple(ts,label);
	auto it = states_with_non_encoded_transitions.find(access);
	if (it == states_with_non_encoded_transitions.end()){
		set<int> ret;
		for(Transition transition : fts->get_ts(ts).get_transitions_with_label(lm->getOrderedLabel(lm->getRelevantLabel(ts, label))))
			if (!has_to_encode_transition(transition))
				ret.insert(transition.target);
		vector<int> ret_vec(ret.begin(),ret.end());
		return states_with_non_encoded_transitions[access] = ret_vec;
	}
	return it->second;
}

vector<vector<int>> TransitionsEncoding::compute_possible_label_movements(int ts, int label){
	set<int> to,from,non;
	for(Transition transition : fts->get_ts(ts).get_transitions_with_label(lm->getRelevantLabel(ts, label)))
		if (has_to_encode_transition(transition)){
			to.insert(transition.target);
			from.insert(transition.src);
		} else 
			non.insert(transition.src);
	vector<int> to_vec(to.begin(),to.end());
	vector<int> from_vec(from.begin(),from.end());
	vector<int> non_vec(non.begin(),non.end());

	vector<vector<int>> from_to_non = {from_vec,to_vec,non_vec};
	return states_label_can_move_from_to_and_non_encoded_state[make_tuple(ts,label)] = from_to_non;
}

vector<int> TransitionsEncoding::get_all_states_label_can_move_from(int ts, int label){
	tuple<int,int> access = make_tuple(ts,label);
	auto it = states_label_can_move_from_to_and_non_encoded_state.find(access);
	if (it == states_label_can_move_from_to_and_non_encoded_state.end()){
		return compute_possible_label_movements(ts,label)[0];
	}
	return (it->second)[0];
}

vector<int> TransitionsEncoding::get_all_states_label_can_move_to(int ts, int label){
	tuple<int,int> access = make_tuple(ts,label);
	auto it = states_label_can_move_from_to_and_non_encoded_state.find(access);
	if (it == states_label_can_move_from_to_and_non_encoded_state.end()){
		return compute_possible_label_movements(ts,label)[1];
	}
	return (it->second)[1];
}

vector<int> TransitionsEncoding::get_all_non_encoded_self_loop_states(int ts, int label){
	tuple<int,int> access = make_tuple(ts,label);
	auto it = states_label_can_move_from_to_and_non_encoded_state.find(access);
	if (it == states_label_can_move_from_to_and_non_encoded_state.end()){
		return compute_possible_label_movements(ts,label)[2];
	}
	return (it->second)[2];
}


void FullTransitionsEncoding::encode_label_consistency(void* solver, int ts, int relevantLabelIndex){
	vector<int> labelTransitionSATVars;

	for(pair<Transition, int> transition : allTimesTransitionVars.back()[ts][lm->getRelevantLabel(ts, relevantLabelIndex)])
		if (has_to_encode_transition(transition.first))
			labelTransitionSATVars.push_back(transition.second);

	if(labelTransitionSATVars.size() > 0){
		//Assert double sided implication between labels and transitions
		if(!(lm->containsSelfLoops(ts, lm->getRelevantLabel(ts, relevantLabelIndex)))){
			impliesOr(solver, lm->getLabelSATVar(lm->getRelevantLabel(ts, relevantLabelIndex)), labelTransitionSATVars);
		}
		for(size_t ltsv = 0 ; ltsv < labelTransitionSATVars.size() ; ltsv++){
			implies(solver, labelTransitionSATVars[ltsv], lm->getLabelSATVar(lm->getRelevantLabel(ts, relevantLabelIndex)));
		}
	}
}

void TransitionsEncoding::encodeRegularPreconditionsAndEffects(int ts, int relevantLabel){
	for(int state : get_all_states_label_can_move_from(ts,relevantLabel)){
		for(int transition_from_state : get_all_sat_vars_moving_from_state(ts,relevantLabel,state)){
			vector<int> impliesOrPrec = getPreconditions(state, ts, relevantLabel);
			//Save previous state for possible precondition of a transition
			impliesOrPrec.push_back(previousStateVars[ts][state]);

			//Constraint enforcing preconditions for a non self-loop transition
			impliesOr(solver, transition_from_state, impliesOrPrec);
		}
	}

	for(int state : get_all_states_label_can_move_to(ts,relevantLabel)){
		for(int transition_to_state : get_all_sat_vars_moving_to_state(ts,relevantLabel,state)){
			vector<int> impliesOrEff = get_all_later_vars_moving_to_different_state(state, ts, relevantLabel);
			impliesOrEff.push_back(nextStateVars[ts][state]);
			//Constraint enforcing effects for a non self-loop transition
			impliesOr(solver, move_totransition_to_state_var, impliesOrEff);
		}
	}

	///// Encoding self-loops, i.e., transitions that don't get encoded explicitly as transitions
	vector<int> precsForSelfLoops;
	for(int state : get_all_non_encoded_self_loop_states(ts,relevantLabel)){
		vector<int> impliesOrPrec = getPreconditions(state, ts, relevantLabel);
		//Save the preconditions stored until now to ensure correct possible utilisation of self loops in constraints later
		precsForSelfLoops.insert(precsForSelfLoops.end(), impliesOrPrec.begin(), impliesOrPrec.end());
		//Save previous state for possible precondition of a transition
		precsForSelfLoops.push_back(previousStateVars[ts][state]);
	}


	if(precsForSelfLoops.size() > 0){
		// all variables that represent a move to a non-self-loop transition
		vector<int> otherTransitionsInLabelWithSelfLoop;
		for(int state : get_all_states_label_can_move_from(ts,relevantLabel))
			for(int transition_from_state : get_all_sat_vars_moving_from_state(ts,relevantLabel,state))
				otherTransitionsInLabelWithSelfLoop.push_back(transition_from_state);

		//Assert preconditions for self loops
		//If a label has both types of transitions, then either one of the none self loop transitions is used or one of the preconditions for the self loops is satisfied
		precsForSelfLoops.insert(precsForSelfLoops.end(), otherTransitionsInLabelWithSelfLoop.begin(), otherTransitionsInLabelWithSelfLoop.end());
		impliesOr(solver, labelVars[relevantLabels[ts][relevantLabel]], precsForSelfLoops);
	}
}

bool TransitionsEncoding::isIrrelevantLabel(int ts, int label) const {
	auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
	if((int)transitions.size() != fts->get_ts(ts).get_size()) return false;
	for(size_t t = 0 ; t < transitions.size() ; t++){
		if(transitions[t].src != transitions[t].target){
			return false;
		}
	}
	return true;
}

bool TransitionsEncoding::isSelfLoop(Transition t) const {
	return t.src == t.target;
}

};

namespace label_manager{

	int LabelManager::getOrderedLabel(int index){
		return labelOrder[index];
	}

	int LabelManager::getRelevantLabel(int ts, int labelIndex){
		return relevantLabels[ts][labelIndex];
	}

	int LabelManager::getNumRelevantLabels(int ts){
		return relevantLabels[ts].size();
	}

	int LabelManager::getLabelSATVar(int label){
		return allTimesLabelVars.back()[label];
	}

	
};

namespace states_manager{
	
	int StatesManager::getPreviousStateSATVar(int ts, int state){
	
	}

	int StatesManager::getNextStateSATVar(int ts, int state){
	
	}

};

namespace sat_search {
SATSearch::SATSearch(const Options &opts): SearchEngine(opts),
	label_order_finder(opts.get<shared_ptr<label_order_finder::LabelOrderFinder>>("label_order")),
	stepTimeLimit(opts.get<int>("step_time_limit")),
	planLength(opts.get<int>("plan_length")),
	start_length(opts.get<int>("start_length")),
	multiplier(opts.get<double>("multiplier")),
	length_by_iteration(opts.get<bool>("length_by_iteration")),
	maximum_iteration(opts.get<int>("maximum_iteration")),
	fts(g_main_task) {

	kissat_quietMode = opts.get<bool>("solver_quiet");

	switch (opts.get<int>("encoding")){

		case 0: do_R2_encoding = false; computing_block = false; eliminating_rnc_and_pairs = true; useSelfloopOptimisation = true; useLabelGroups = true; chainsParallelism = true; break;
		case 1: do_R2_encoding = false; computing_block = false; eliminating_rnc_and_pairs = true; useSelfloopOptimisation = true; useLabelGroups = true; selfloopParallelism = true; break;
		case 2: do_R2_encoding = false; computing_block = false; eliminating_rnc_and_pairs = true; useSelfloopOptimisation = true; useLabelGroups = true; sequential = true; break;

		case 3: do_R2_encoding = false; computing_block = false; eliminating_rnc_and_pairs = true; useSelfloopOptimisation = true; chainsParallelism = true; break;
		case 4: do_R2_encoding = false; computing_block = false; eliminating_rnc_and_pairs = true; useSelfloopOptimisation = true; selfloopParallelism = true; break;
		case 5: do_R2_encoding = false; computing_block = false; eliminating_rnc_and_pairs = true; useSelfloopOptimisation = true; sequential = true; break;

		case 6: do_R2_encoding = false; computing_block = false; eliminating_rnc_and_pairs = true; useLabelGroups = true; sequential = true; break;

		case 7: do_R2_encoding = false; computing_block = false; eliminating_rnc_and_pairs = true; sequential = true; break;


		case 8: do_R2_encoding = false; computing_block = false; basic_per_row = true; useSelfloopOptimisation = true; useLabelGroups = true; chainsParallelism = true; break;
		case 9: do_R2_encoding = false; computing_block = false; basic_per_row = true; useSelfloopOptimisation = true; useLabelGroups = true; selfloopParallelism = true; break;
		case 10: do_R2_encoding = false; computing_block = false; basic_per_row = true; useSelfloopOptimisation = true; useLabelGroups = true; sequential = true; break;

		case 11: do_R2_encoding = false; computing_block = false; basic_per_row = true; useSelfloopOptimisation = true; chainsParallelism = true; break;
		case 12: do_R2_encoding = false; computing_block = false; basic_per_row = true; useSelfloopOptimisation = true; selfloopParallelism = true; break;
		case 13: do_R2_encoding = false; computing_block = false; basic_per_row = true; useSelfloopOptimisation = true; sequential = true; break;

		case 14: do_R2_encoding = false; computing_block = false; basic_per_row = true; useLabelGroups = true; sequential = true; break;

		case 15: do_R2_encoding = false; computing_block = false; basic_per_row = true; sequential = true; break;


		case 100: do_R2_encoding = true; no_selfloop_SATvars = false; break;
		case 101: do_R2_encoding = true; no_selfloop_SATvars = true; break;
	}

	if (opts.get<int>("length_iteration") != -1){
		planLength = int(0.5 + start_length * pow(multiplier, opts.get<int>("length_iteration")));
		forceAtLeastOneAction = false;
	} else
		forceAtLeastOneAction = true;

	if (length_by_iteration) forceAtLeastOneAction = false;
}





bool SATSearch::containsSelfLoops(int ts, int label){
	auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
	for(size_t t = 0 ; t < transitions.size() ; t++){
		if(transitions[t].src == transitions[t].target){
			return true;
		}
	}
	return false;
}

bool SATSearch::hasMixedTransitions(int ts, int label){
	bool selfloop = false;
	bool normalTransition = false;
	auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
	for(size_t t = 0 ; t < transitions.size() ; t++){
		if(transitions[t].src == transitions[t].target){
			return selfloop = true;
		}else if(transitions[t].src != transitions[t].target){
			return normalTransition = true;
		}
	}
	return (selfloop && normalTransition);
}

bool SATSearch::isAlwaysSelfLoop(int ts, int label){
	auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
	for(size_t t = 0 ; t < transitions.size() ; t++){
		if(transitions[t].src != transitions[t].target){
			return false;
		}
	}
	return true;
}

int SATSearch::findPreviousValidAuxVar(vector<int> &auxVars, int label){
	for(int prev = label-1 ; prev >=0 ; prev--){
		if(auxVars[prev] != -1) return auxVars[prev];
	}
	return -1;
}

BlockInfo SATSearch::find_largest_block(const vector<vector<int>>& filled_columns_per_row) {
    unordered_map<int, unordered_set<int>> row_to_cols;
    unordered_map<int, unordered_set<int>> col_to_rows;
    unordered_set<int> all_columns;

    int num_rows = filled_columns_per_row.size();

    for (int r = 0; r < num_rows; ++r) {
        for (int c : filled_columns_per_row[r]) {
            row_to_cols[r].insert(c);
            col_to_rows[c].insert(r);
            all_columns.insert(c);
        }
    }

    BlockInfo result;

    // Step 1: Identify empty rows
    for (int r = 0; r < num_rows; ++r) {
        if (row_to_cols.find(r) == row_to_cols.end() || row_to_cols[r].empty()) {
            result.empty_rows.push_back(r);
        }
    }

    // Step 2: Identify empty columns
    if (!all_columns.empty()) {
        int max_col = *max_element(all_columns.begin(), all_columns.end());
        for (int c = 0; c <= max_col; ++c) {
            if (col_to_rows.find(c) == col_to_rows.end() || col_to_rows[c].empty()) {
                result.empty_columns.push_back(c);
            }
        }
    }

    // Step 3: Find largest block (rows x columns of 1's)
    int max_area = 0;
    set<int> best_rows, best_cols;

    for (auto& [base_row, base_cols] : row_to_cols) {
        map<vector<int>, vector<int>> colset_to_rows;
        for (int r = 0; r < num_rows; ++r) {
            vector<int> common;
            for (int c : row_to_cols[r]) {
                if (base_cols.count(c)) {
                    common.push_back(c);
                }
            }
            if (!common.empty()) {
                sort(common.begin(), common.end());
                colset_to_rows[common].push_back(r);
            }
        }

        for (auto& [cols, rows] : colset_to_rows) {
            int area = rows.size() * cols.size();
            if (area > max_area) {
                max_area = area;
                best_rows = set<int>(rows.begin(), rows.end());
                best_cols = set<int>(cols.begin(), cols.end());
            }
        }
    }

    unordered_set<int> row_block_set(best_rows.begin(), best_rows.end());
    unordered_set<int> col_block_set(best_cols.begin(), best_cols.end());

    // Step 4: Identify extra 1's not in the block
    for (size_t r = 0; r < filled_columns_per_row.size(); ++r) {
        for (int c : filled_columns_per_row[r]) {
            bool in_block = row_block_set.count(r) && col_block_set.count(c);
            if (!in_block) {
                result.extra_ones_per_row[r].push_back(c);
                result.extra_ones_per_column[c].push_back(r);
            }
        }
    }

    return result;
}

bool SATSearch::hasSelfLoopOnValue(int ts, int value, int label){
	auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
	for(Transition t : transitions){
		if(t.src == value && t.target == value) return true;
	}
	return false;
}


void SATSearch::checkSolution(vector<vector<vector<int>>> &allTimesStateVars, vector<vector<int>> &allTimesLabelVars, vector<map<int, map<int, vector<pair<Transition, int>>>>> &allTimesTransitionVars, 
					int length, void* solver){
	if (!do_R2_encoding) return; // TODO needs to be implemented still
	vector<int> previousState;
	vector<int> nextState;
	for(vector<int> vars : allTimesStateVars[0]){
		for(size_t val = 0 ; val < vars.size() ; val++){
			if(ipasir_val(solver, vars[val]) > 0){
				previousState.push_back(val);//Initial State
				break;
			}
		}
	}
	for(int l = 1 ; l <= length ; l++){
		for(int label = 0 ; label < fts->get_num_labels() ; label++){
			if(ipasir_val(solver, allTimesLabelVars[l-1][label]) <= 0)continue;
			for(int ts = 0 ; ts < fts->get_size() ; ts++){
				bool selfLoopCanBeUsed = false;
				bool selectedTransitionVar = false;
				for(pair<Transition, int> t : allTimesTransitionVars[l-1][ts][label]){
					if(t.second != -1 && ipasir_val(solver, t.second) > 0 && t.first.src == previousState[ts]){
						nextState.push_back(t.first.target);
						selectedTransitionVar = true;
						break;
					}else if(t.second == -1 && t.first.src == previousState[ts]){
						selfLoopCanBeUsed = true;
					}else if(t.second != -1 && ipasir_val(solver, t.second) > 0 && t.first.src != previousState[ts]){
						cout << "WEEWOO WEEWOO WEEWOO WEEWOO" << endl;
						cout << "SAT solver tried to apply a transition (" << t.first.src << "," << t.first.target << ") in TS " << ts << " with label " << label << ", but the previous state was " << previousState[ts] << endl;
						return;
						//exit(0);
					}
				}
				if(!selectedTransitionVar && (selfLoopCanBeUsed || isIrrelevantLabel(ts, label))){
					nextState.push_back(previousState[ts]);
				}else if(!selfLoopCanBeUsed && !selectedTransitionVar){
					cout << "WEEWOO WEEWOO WEEWOO WEEWOO" << endl;
					cout << "SAT solver tried to apply a selfloop transition in TS " << ts << " with label " << label << ", but the previous state was " << previousState[ts] << endl;
					return;
					//exit(0);
				}
			}
			assert(previousState.size() == nextState.size());
			swap(previousState, nextState);
			nextState.clear();
		}
	}
	cout << "SOLUTION SEEMS TO BE VALID!!! YIIPEEEEEE!!!!" << endl;
}


void SATSearch::initialize() {
	utils::Timer sat_init_timer;
	cout << "Initialising" << endl;
	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

	labelOrder = label_order_finder->find_order(*fts);


	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		map<set<pair<int, int>>, vector<int>> label_groups;
		for(int label = 0 ; label < fts->get_num_labels() ; label++){
			auto transitions = fts->get_ts(ts).get_transitions_with_label(label);

			std::set<std::pair<int, int>> transition_set;
			for (const auto& t : transitions) {
				transition_set.emplace(t.src, t.target);  // direction matters
			}

			label_groups[transition_set].push_back(label);
		}

		for(const auto& [_, labels] : label_groups){
			labelGroups[ts].push_back(labels);
		}
	}

	if(computing_block){
		labelsWithoutOnlySelfLoops.resize(fts->get_size());
		for(int ts = 0 ; ts < fts->get_size() ; ts++){
			for(int label = 0 ; label < fts->get_num_labels() ; label++){
				if(isIrrelevantLabel(ts, label) || isAlwaysSelfLoop(ts, label)) continue;
				auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
				vector<vector<int>> gridInfo(fts->get_ts(ts).get_size());
				for(size_t t = 0 ; t < transitions.size() ; t++){
					gridInfo[transitions[t].src].push_back(transitions[t].target);
				}
				BlockInfo bi = find_largest_block(gridInfo);
				labelBasedEncodingInfo[ts][label] = bi;
				labelsWithoutOnlySelfLoops[ts].push_back(label);
			}

			for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
				for(int label = 0 ; label < fts->get_num_labels() ; label++){
					if(isAlwaysSelfLoop(ts, label)) continue;
					auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
					for(Transition t : transitions){
						if(t.target == states){
							labelsWithEffectOnValue[ts][states].push_back(label);
							break;
						}
					}
				}
			}

		}
	}

	if(basic_per_row || eliminating_rnc_and_pairs){
		for(int ts = 0 ; ts < fts->get_size() ; ts++){
			set<int> values;
			labelProjection[ts] = vector<vector<int>> (fts->get_ts(ts).get_size(), vector<int>(fts->get_ts(ts).get_size(), 0));
			for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
				values.insert(states);
				labelProjection[ts][states][states] = 1;
				for(int label = 0 ; label < fts->get_num_labels() ; label++){
					if(isAlwaysSelfLoop(ts, label)) continue;
					auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
					for(Transition t : transitions){
						if(t.target == states){
							labelsWithEffectOnValue[ts][states].push_back(label);
							break;
						}
					}
				}
			}
			for(size_t lg = 0 ; lg < labelGroups[ts].size() ; lg++){
				empty_rows[ts][lg] = values;
				empty_cols[ts][lg] = values;
				int label = labelGroups[ts][lg][0];
				if(useSelfloopOptimisation && (isIrrelevantLabel(ts, label) || isAlwaysSelfLoop(ts, label))) continue;
				auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
				for(Transition t : transitions){
					empty_rows[ts][lg].erase(t.src);
					empty_cols[ts][lg].erase(t.target);
					ones_per_row[ts][lg][t.src].insert(t.target);
					//ones_per_column[ts][lg][t.target].insert(t.src);
					labelProjection[ts][t.src][t.target] = 1;
				}
			}
			for(int src = 0 ; src < fts->get_ts(ts).get_size() ; src++){
				for(int target = 0 ; target < fts->get_ts(ts).get_size() ; target++){
					if(labelProjection[ts][src][target] == 0)
						empty_projected_cells_per_row[ts][src].push_back(target);
				}
			}
		}
	}

	

	relevantLabels.resize(fts->get_size());
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		if (no_selfloop_SATvars){
			for(int label = 0 ; label < fts->get_num_labels() ; label++){
				if(!isIrrelevantLabel(ts, label) ){
					relevantLabels[ts].push_back(label);
				}
			}
		} else {
			relevantLabels[ts].resize(fts->get_num_labels());
			iota(relevantLabels[ts].begin(), relevantLabels[ts].end(), 0);
		}
	}
	
	stepNumber = 0;

	if (planLength != -1){
		currentLength = planLength;
	} else {
		if (length_by_iteration){
			currentLength = start_length;
		} else {
			currentLength = 1;
		}
	}

    cout << "SAT init time: " << sat_init_timer << endl;
}

vector<vector<int>> SATSearch::generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */){
	vector<vector<int>> stateVars(fts->get_size());
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			int stateVar = capsule.new_variable();
			stateVars[ts].push_back(stateVar);
			DEBUG(capsule.registerVariable(stateVar,"TS:"+to_string(ts)+";Val:"+to_string(states)));
		}
		atMostOne(solver, capsule, stateVars[ts]);
		atLeastOne(solver, capsule, stateVars[ts]);
	}
	return stateVars;
}

vector<int> SATSearch::generateLabelVars(__attribute__((unused)) void* solver, sat_capsule & capsule/* , int timestep */){
	vector<int> labelVars(fts->get_num_labels());
	for(int label = 0 ; label < fts->get_num_labels() ; label++){
		int labelVar = capsule.new_variable();
		labelVars[label] = labelVar;
		DEBUG(capsule.registerVariable(labelVar,"Label:"+to_string(label)));
		//cout << labelVar << endl;
	}
	if(sequential){
		atMostOne(solver, capsule, labelVars);
	}else if(selfloopParallelism){
		for(int ts = 0 ; ts < fts->get_size() ; ts++){
			vector<int> non_parallelisable_labels;
			for(int label = 0 ; label < fts->get_num_labels() ; label++){
				if(!isAlwaysSelfLoop(ts, label)){
					non_parallelisable_labels.push_back(labelVars[label]);
				}
			}
			atMostOne(solver, capsule, non_parallelisable_labels);
		}
	}
	if (forceAtLeastOneAction)
		atLeastOne(solver, capsule, labelVars);
	return labelVars;
}

map<int, vector<int>> SATSearch::generateLabelGroupVars(void* solver, sat_capsule & capsule, vector<int> &labelVars/* , int timestep */){
	map<int, vector<int>> labelGroupVars;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(size_t lg = 0 ; lg < labelGroups[ts].size() ; lg++){
			if(labelGroups[ts][lg].size() == 1){
				labelGroupVars[ts].push_back(-1);
				continue;
			}
			int lab_group = capsule.new_variable();
			DEBUG(capsule.registerVariable(lab_group,"LabelGroup:"+to_string(lg)));
			labelGroupVars[ts].push_back(lab_group);
			vector<int> labels;
			for(int label : labelGroups[ts][lg]){
				implies(solver, labelVars[label], lab_group);
				labels.push_back(labelVars[label]);
			}
			impliesOr(solver, lab_group, labels);
		}
	}
	return labelGroupVars;
}

map<int, map<int, vector<int>>> SATSearch::generateHelperVars(sat_capsule & capsule/* , int timestep */){
	map<int, map<int, vector<int>>> helperVars;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			for(size_t h = 1 ; h < labelsWithEffectOnValue[ts][states].size() ; h++){
				int helperVar = capsule.new_variable();
				DEBUG(capsule.registerVariable(helperVar, "Helpers"));
				helperVars[ts][states].push_back(helperVar);
			}
		}
	}
	return helperVars;
}

map<int, map<int, vector<pair<Transition, int>>>> SATSearch::generateTransitionVars(void* solver, sat_capsule &capsule){//[transition_systems[labels[<transition, SATVar>, <transition, SATVar>...]]]
	map<int, map<int, vector<pair<Transition, int>>>> transitionVars;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int label = 0 ; label < fts->get_num_labels() ; label++){
			if(no_selfloop_SATvars && isIrrelevantLabel(ts, label)){
				continue;
			}
			auto transitions = fts->get_ts(ts).get_transitions_with_label(labelOrder[label]);
			vector<int> SATVars;
			for(size_t t = 0 ; t < transitions.size() ; t++){
				if(no_selfloop_SATvars && transitions[t].src == transitions[t].target){
					transitionVars[ts][label].push_back({transitions[t],-1});
					continue;
				}
				int transitionVar = capsule.new_variable();
				SATVars.push_back(transitionVar);
				transitionVars[ts][label].push_back({transitions[t],transitionVar});
				DEBUG(capsule.registerVariable(transitionVar,"TS:"+to_string(ts)+";Label:"+to_string(label)+"--"+to_string(transitions[t].src)+"->"+to_string(transitions[t].target)));
			}
			atMostOne(solver, capsule, SATVars);
		}
	}
	return transitionVars;
}

map<int, map<int, vector<int>>> SATSearch::generateAuxVars(sat_capsule &capsule){
	map<int, map<int, vector<int>>> auxVars;//[transition_system[value[vars]]]
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			for(int label = 0 ; label < fts->get_num_labels()-1 ; label++){
				if(no_selfloop_SATvars){
					bool is_always_self_loop = true;
					auto transitions = fts->get_ts(ts).get_transitions_with_label(labelOrder[label]);
					for(size_t t = 0 ; t < transitions.size() ; t++){
						if(transitions[t].src != transitions[t].target){
							is_always_self_loop = false;
							break;
						}
					}
					if(no_selfloop_SATvars && is_always_self_loop){
						auxVars[ts][states].push_back(-1);
						continue;
					}
				}
				int auxVar = capsule.new_variable();
				auxVars[ts][states].push_back(auxVar);
			}
		}
	}
	return auxVars;
}

map<int, map<int, vector<int>>> SATSearch::getApplicableLabels(){
	map<int, map<int, vector<int>>> applicableLabels;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int label = 0 ; label < fts->get_num_labels() ; label++){
			vector<int> labelPrec = fts->get_ts(ts).get_label_precondition((task_representation::LabelID)labelOrder[label]);
			for(size_t state = 0 ; state < labelPrec.size() ; state++){
				applicableLabels[ts][labelPrec[state]].push_back(label);
			}
		}
	}
	/* cout << endl;
	for (auto const& x : applicableLabels){
		cout << "TS : " << x.first << endl;
		for (auto const& y : x.second){
			cout << "\tState : " << y.first << endl;
			cout << "\t\tLabels :";
			for(size_t a = 0 ; a < y.second.size() ; a++){
					cout << " " << y.second[a];
			}
			cout << endl;
		}
	} */

	return applicableLabels;
}

map<int, map<int, map<int, vector<int>>>> SATSearch::getSuccessorStates(map<int, map<int, vector<int>>> applicableLabels){
	map<int, map<int, map<int, vector<int>>>> successorStates;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			for(size_t label = 0 ; label < applicableLabels[ts][states].size() ; label++){
				auto transitions = fts->get_ts(ts).get_transitions_with_label(labelOrder[applicableLabels[ts][states][label]]);
				for(size_t t = 0 ; t < transitions.size() ; t++){
					if(transitions[t].src == states){
						successorStates[ts][states][applicableLabels[ts][states][label]].push_back(transitions[t].target);
					}
				}
			}
		}
	}

	/* cout << endl;
	for (auto const& x : successorStates){
		cout << "TS : " << x.first << endl;
		for (auto const& y : x.second){
			cout << "\tState : " << y.first << endl;
			for (auto const& z : y.second){
				cout << "\t\tApplicable Label :" << z.first << endl;
				cout << "\t\t\tSuccessor States :";
				for(size_t s = 0 ; s < z.second.size() ; s++){
					cout << " " << z.second[s];
				}
				cout << endl;
			}
		}
	} */

	return successorStates;
}


struct solver_timer {
	std::atomic_bool & stop;
	void* solver; 
	int time_in_ms;
	std::chrono::system_clock::time_point t_start;
	
	solver_timer(void* _solver, int _time_in_ms,std::atomic_bool& _stop, std::chrono::system_clock::time_point _t_start) :
		stop(_stop), solver(_solver), time_in_ms(_time_in_ms), t_start(_t_start) {}
	solver_timer(solver_timer const& other) : stop(other.stop), solver(other.solver), time_in_ms(other.time_in_ms), t_start(other.t_start) {}
	solver_timer(solver_timer&& other ) : stop(other.stop), solver(other.solver), time_in_ms(other.time_in_ms), t_start(other.t_start) {}


	void operator() () {
	    std::chrono::milliseconds delay(time_in_ms);
	    while(!stop) {
	      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	      auto t_now = std::chrono::system_clock::now();
	      std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_start);
	      if (stop) break;
		  if(delay <= elapsed) {
	          ipasir_terminate(solver);
			  cout << "SAT solver exceeded time limit. Terminating." << endl;
			  return;
	      }
	    }
	}
};



SearchStatus SATSearch::step() {
    utils::Timer step_timer;
	auto t_start = std::chrono::system_clock::now();
	cout << "HI doing step! SAT: " << ipasir_signature() << endl; // << " starting at " << t_start << endl;
	//bool parallelism = false;
	vector<vector<vector<int>>> allTimesStateVars;
	vector<vector<int>> allTimesLabelVars;
	vector<map<int, map<int, vector<pair<Transition, int>>>>> allTimesTransitionVars;
	sat_capsule capsule;
	reset_number_of_clauses();
	void* solver = ipasir_init();
	// try to solve for length currentLength
	map<int, map<int, vector<int>>> applicableLabels = getApplicableLabels();
	map<int, map<int, map<int, vector<int>>>> successorStates = getSuccessorStates(applicableLabels);

	vector<vector<int>> previousStateVars = generateStateVars(solver, capsule/* , 0 */);
	allTimesStateVars.push_back(previousStateVars);

	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		assertYes(solver, previousStateVars[ts][fts->get_ts(ts).get_init_state()]);
	}

	map<int, map<int, vector<pair<Transition, int>>>> transitionVars;
	vector<int> labelVars;
	map<int, vector<int>> labelGroupVars;
	vector<vector<int>> nextStateVars;
	map<int, map<int, vector<int>>> auxVars;
	

	for(int timestep = 1 ; timestep <= currentLength ; timestep++){
		labelVars = generateLabelVars(solver, capsule/* , int timestep */);
		allTimesLabelVars.push_back(labelVars);
		if(useLabelGroups){
			labelGroupVars = generateLabelGroupVars(solver, capsule, labelVars/* , int timestep */);
		}
		nextStateVars = generateStateVars(solver, capsule/* , timestep */);
		allTimesStateVars.push_back(nextStateVars);

		if(do_R2_encoding && no_selfloop_SATvars){
			transitionVars = generateTransitionVars(solver, capsule/* , timestep */);
			allTimesTransitionVars.push_back(transitionVars);
			auxVars = generateAuxVars(capsule);

			for(int ts = 0 ; ts < fts->get_size() ; ts++){
				for(size_t relevantLabel = 0 ; relevantLabel < relevantLabels[ts].size() ; relevantLabel++){
					vector<int> otherTransitionsInLabelWithSelfLoop;
					vector<int> precsForSelfLoops;
					vector<int> labelTransitionSATVars;
					for(pair<Transition, int> transition : transitionVars[ts][relevantLabels[ts][relevantLabel]]){
						if(!isSelfLoop(transition.first)){
							//Save the transition vars to make the double implication between label vars and transition vars
							labelTransitionSATVars.push_back(transition.second);
						}
						vector<int> impliesOrPrec;
						//Save previous state for possible precondition of a transition
						impliesOrPrec.push_back(previousStateVars[ts][transition.first.src]);
						for(size_t relevantLabelPrec = 0 ; relevantLabelPrec < relevantLabel ; relevantLabelPrec++){
							for(pair<Transition, int> transition_prec : transitionVars[ts][relevantLabels[ts][relevantLabelPrec]]){
								if(transition_prec.first.target == transition.first.src && !isSelfLoop(transition_prec.first)){
									//Save transition vars whose target matches the source of the transition we are currently looking at
									impliesOrPrec.push_back(transition_prec.second);
								}
							}
						}
						if(isSelfLoop(transition.first)){
							//Save the preconditions stored until now to ensure correct possible utilisation of self loops in constraints later
							precsForSelfLoops.insert(precsForSelfLoops.end(), impliesOrPrec.begin(), impliesOrPrec.end());
							continue;
						}

						//Save a non-loop transition from labels that have both types of transitions
						otherTransitionsInLabelWithSelfLoop.push_back(transition.second);

						//Constraint enforcing preconditions for a non self-loop transition
						impliesOr(solver, transition.second, impliesOrPrec);

						vector<int> impliesOrEff;
						impliesOrEff.push_back(nextStateVars[ts][transition.first.target]);
						for(size_t relevantLabelEff = relevantLabel+1 ; relevantLabelEff < relevantLabels[ts].size() ; relevantLabelEff++){
							for(pair<Transition, int> transition_eff : transitionVars[ts][relevantLabels[ts][relevantLabelEff]]){
								if(transition_eff.first.target != transition.first.target && !isSelfLoop(transition_eff.first)){
									//Saving transitions that have a different target than the current transition and that are from a label that appears later
									impliesOrEff.push_back(transition_eff.second);
								}
							}
						}
						//Constraint enforcing effects for a non self-loop transition
						impliesOr(solver, transition.second, impliesOrEff);
					}

					if(labelTransitionSATVars.size() > 0){
						//Assert double sided implication between labels and transitions
						if(!containsSelfLoops(ts, relevantLabels[ts][relevantLabel])){
							impliesOr(solver, labelVars[relevantLabels[ts][relevantLabel]], labelTransitionSATVars);
						}
						for(size_t ltsv = 0 ; ltsv < labelTransitionSATVars.size() ; ltsv++){
							implies(solver, labelTransitionSATVars[ltsv], labelVars[relevantLabels[ts][relevantLabel]]);
						}
					}
					if(precsForSelfLoops.size() > 0){
						//Assert preconditions for self loops
						//If a label has both types of transitions, then either one of the none self loop transitions is used or one of the preconditions for the self loops is satisfied
						precsForSelfLoops.insert(precsForSelfLoops.end(), otherTransitionsInLabelWithSelfLoop.begin(), otherTransitionsInLabelWithSelfLoop.end());
						impliesOr(solver, labelVars[relevantLabels[ts][relevantLabel]], precsForSelfLoops);
					}
				}

				for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
					set<int> negatedRelevantLabels;
					negatedRelevantLabels.insert(previousStateVars[ts][states]);
					for(size_t relevantLabel = 0 ; relevantLabel < relevantLabels[ts].size() ; relevantLabel++){
						if(!isAlwaysSelfLoop(ts, relevantLabels[ts][relevantLabel]) && !hasMixedTransitions(ts, relevantLabels[ts][relevantLabel])){//Should I also add a condition for labels which are not irrelevant but always self loops??????? YES!!!!
							negatedRelevantLabels.insert(-labelVars[relevantLabels[ts][relevantLabel]]);
						}else if(hasMixedTransitions(ts, relevantLabels[ts][relevantLabel])){
							for(pair<Transition, int> transition : transitionVars[ts][relevantLabels[ts][relevantLabel]]){
								if(!isSelfLoop(transition.first)){
									negatedRelevantLabels.insert(-transition.second);
								}
							}
						}
					}
					andImplies(solver, negatedRelevantLabels, nextStateVars[ts][states]);

					for(size_t relevantLabel = 0 ; relevantLabel < relevantLabels[ts].size() ; relevantLabel++){
						vector<int> supportingTransitions;
						if(relevantLabel < relevantLabels[ts].size()-1 && !isAlwaysSelfLoop(ts, relevantLabels[ts][relevantLabel])){
							supportingTransitions.push_back(auxVars[ts][states][relevantLabels[ts][relevantLabel]]);
						}
						for(pair<Transition, int> transition : transitionVars[ts][relevantLabels[ts][relevantLabel]]){
							if(transition.first.target != states && relevantLabel < relevantLabels[ts].size()-1 && !isSelfLoop(transition.first)){
								implies(solver, transition.second, auxVars[ts][states][relevantLabels[ts][relevantLabel]]);
							}
							if(transition.first.target == states && !isSelfLoop(transition.first)){
								supportingTransitions.push_back(transition.second);
							}
							if(transition.first.src == states && relevantLabel > 0 && !isSelfLoop(transition.first)){
								int previousValidAuxVar = findPreviousValidAuxVar(auxVars[ts][states], relevantLabels[ts][relevantLabel]);
								if(previousValidAuxVar != -1){
									impliesNot(solver, previousValidAuxVar, transition.second);
								}
							}
						}
						if(supportingTransitions.size() > 0 && relevantLabel > 0 && relevantLabel < relevantLabels[ts].size()-1 && !isAlwaysSelfLoop(ts, relevantLabels[ts][relevantLabel])){
							int previousValidAuxVar = findPreviousValidAuxVar(auxVars[ts][states], relevantLabels[ts][relevantLabel]);
							if(previousValidAuxVar != -1){
								impliesOr(solver, previousValidAuxVar, supportingTransitions);
							}
						}
					}
				}
				for(size_t relevantLabel = 1 ; relevantLabel < relevantLabels[ts].size() ; relevantLabel++){//What happens to the first label??????????
					vector<int> auxVarsInPrec;
					vector<int> otherTransitions;
					for(const pair<Transition, int> & transition : transitionVars[ts][relevantLabels[ts][relevantLabel]]){
						if(transition.second == -1){
							int previousValidAuxVar = findPreviousValidAuxVar(auxVars[ts][transition.first.src], relevantLabels[ts][relevantLabel]);
							if(previousValidAuxVar != -1){
								auxVarsInPrec.push_back(previousValidAuxVar);
							}
						}else{
							otherTransitions.push_back(transition.second);
						}
					}
					
					otherTransitions.push_back(-labelVars[relevantLabels[ts][relevantLabel]]);
					if(auxVarsInPrec.size() > 0){
						andImpliesOr(solver, auxVarsInPrec, otherTransitions);
					}
				}
			}
		}else if(do_R2_encoding && !no_selfloop_SATvars){
			transitionVars = generateTransitionVars(solver, capsule/* , timestep */);
			allTimesTransitionVars.push_back(transitionVars);
			auxVars = generateAuxVars(capsule);

			for(int ts = 0 ; ts < fts->get_size() ; ts++){
				for(size_t relevantLabel = 0 ; relevantLabel < relevantLabels[ts].size() ; relevantLabel++){
					vector<int> labelTransitionSATVars;
					for(pair<Transition, int> transition : transitionVars[ts][relevantLabels[ts][relevantLabel]]){
						labelTransitionSATVars.push_back(transition.second);
						vector<int> impliesOrPrec;
						impliesOrPrec.push_back(previousStateVars[ts][transition.first.src]);
						for(size_t relevantLabelPrec = 0 ; relevantLabelPrec < relevantLabel ; relevantLabelPrec++){
							for(pair<Transition, int> transition_prec : transitionVars[ts][relevantLabels[ts][relevantLabelPrec]]){
								if(transition_prec.first.target == transition.first.src && !isSelfLoop(transition_prec.first)){
									impliesOrPrec.push_back(transition_prec.second);
								}
							}
						}
						impliesOr(solver, transition.second, impliesOrPrec);

						vector<int> impliesOrEff;
						impliesOrEff.push_back(nextStateVars[ts][transition.first.target]);
						for(size_t relevantLabelEff = relevantLabel+1 ; relevantLabelEff < relevantLabels[ts].size() ; relevantLabelEff++){
							for(pair<Transition, int> transition_eff : transitionVars[ts][relevantLabelEff]){
								if(transition_eff.first.target != transition.first.target){
									impliesOrEff.push_back(transition_eff.second);
								}
							}
						}
						impliesOr(solver, transition.second, impliesOrEff);
					}
					impliesOr(solver, labelVars[relevantLabels[ts][relevantLabel]], labelTransitionSATVars);
					for(size_t ltsv = 0 ; ltsv < labelTransitionSATVars.size() ; ltsv++){
						implies(solver, labelTransitionSATVars[ltsv], labelVars[relevantLabels[ts][relevantLabel]]);
					}
				}

				for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
					for(size_t relevantLabel = 0 ; relevantLabel < relevantLabels[ts].size() ; relevantLabel++){
						vector<int> supportingTransitions;
						if(relevantLabel < relevantLabels[ts].size()-1){
							supportingTransitions.push_back(auxVars[ts][states][relevantLabels[ts][relevantLabel]]);
						}

						for(pair<Transition, int> transition : transitionVars[ts][relevantLabels[ts][relevantLabel]]){
							if(transition.first.target != states && !isSelfLoop(transition.first) && relevantLabel < relevantLabels[ts].size()-1){
								implies(solver, transition.second, auxVars[ts][states][relevantLabels[ts][relevantLabel]]);
							}
							if(transition.first.target == states && !isSelfLoop(transition.first)){
								supportingTransitions.push_back(transition.second);
							}
							if(transition.first.src == states && relevantLabel > 0){
								impliesNot(solver, auxVars[ts][states][relevantLabels[ts][relevantLabel-1]], transition.second);
							}
						}

						if(supportingTransitions.size() > 0 && relevantLabel > 0 && relevantLabel < relevantLabels[ts].size()-1){
							impliesOr(solver, auxVars[ts][states][relevantLabels[ts][relevantLabel-1]], supportingTransitions);
						}
					}
				}
			}
		}else if(basic_per_row){
			map<int, map<int, vector<int>>> topHelperVars;
			map<int, map<int, vector<int>>> bottomHelperVars;
			if(chainsParallelism){
				topHelperVars = generateHelperVars(capsule/* , int timestep */);
				bottomHelperVars = generateHelperVars(capsule/* , int timestep */);
			}

			for(int ts = 0 ; ts < fts->get_size() ; ts++){

				vector<int> labelGroupsWithActualTransitions;
				for(size_t lg = 0 ; lg < labelGroups[ts].size() ; lg++){
					int label = labelGroups[ts][lg][0];
					if(!useLabelGroups){
						if(useSelfloopOptimisation){
							if(isIrrelevantLabel(ts, label)) continue;
							if(isAlwaysSelfLoop(ts, label)){
								auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
								vector<int> preconditions;
								vector<int> effects;
								for(Transition t : transitions){
									preconditions.push_back(previousStateVars[ts][t.src]);
									effects.push_back(nextStateVars[ts][t.target]);
								}
								for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
									impliesOr(solver, labelVars[labelGroups[ts][lg][l]], preconditions);
									impliesOr(solver, labelVars[labelGroups[ts][lg][l]], effects);
								}
								continue;
							}
						}
						if(useSelfloopOptimisation){
							for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
								labelGroupsWithActualTransitions.push_back(labelVars[labelGroups[ts][lg][l]]);//These are labels and not labelgroups
							}
						}
						for(int src = 0 ; src < fts->get_ts(ts).get_size() ; src++){
							if(ones_per_row[ts][lg][src].size() == 1){
								int t = *ones_per_row[ts][lg][src].begin();
								for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
									andImplies(solver, labelVars[labelGroups[ts][lg][l]], previousStateVars[ts][src], nextStateVars[ts][t]);
								}
								continue;
							}
							for(int target = 0 ; target < fts->get_ts(ts).get_size() ; target++){
								if(ones_per_row[ts][lg][src].find(target) == ones_per_row[ts][lg][src].end()){
									for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
										andImplies(solver, labelVars[labelGroups[ts][lg][l]], previousStateVars[ts][src], -nextStateVars[ts][target]);
									}
								}
							}
						}
					}else{
						if(useSelfloopOptimisation){
							if(isIrrelevantLabel(ts, label)) continue;
							if(isAlwaysSelfLoop(ts, label)){
								auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
								vector<int> preconditions;
								vector<int> effects;
								for(Transition t : transitions){
									preconditions.push_back(previousStateVars[ts][t.src]);
									effects.push_back(nextStateVars[ts][t.target]);
								}
								if(labelGroupVars[ts][lg] == -1){
									impliesOr(solver, labelVars[labelGroups[ts][lg][0]], preconditions);
									impliesOr(solver, labelVars[labelGroups[ts][lg][0]], effects);
								}else{
									impliesOr(solver, labelGroupVars[ts][lg], preconditions);
									impliesOr(solver, labelGroupVars[ts][lg], effects);
								}
								continue;
							}
						}
						if(useSelfloopOptimisation){
							if(labelGroupVars[ts][lg] == -1){
								labelGroupsWithActualTransitions.push_back(labelVars[labelGroups[ts][lg][0]]);
							}else{
								labelGroupsWithActualTransitions.push_back(labelGroupVars[ts][lg]);
							}
						}

						for(int src = 0 ; src < fts->get_ts(ts).get_size() ; src++){
							if(ones_per_row[ts][lg][src].size() == 1){
								int t = *ones_per_row[ts][lg][src].begin();
								if(labelGroupVars[ts][lg] == -1){
									andImplies(solver, labelVars[labelGroups[ts][lg][0]], previousStateVars[ts][src], nextStateVars[ts][t]);
									continue;
								}
								andImplies(solver, labelGroupVars[ts][lg], previousStateVars[ts][src], nextStateVars[ts][t]);
								continue;
							}
							for(int target = 0 ; target < fts->get_ts(ts).get_size() ; target++){
								if(ones_per_row[ts][lg][src].find(target) == ones_per_row[ts][lg][src].end()){
									if(labelGroupVars[ts][lg] == -1){
										andImplies(solver, labelVars[labelGroups[ts][lg][0]], previousStateVars[ts][src], -nextStateVars[ts][target]);
									}else{
										andImplies(solver, labelGroupVars[ts][lg], previousStateVars[ts][src], -nextStateVars[ts][target]);
									}
								}
							}
						}
						
					}
				}
				if(useSelfloopOptimisation){
					int selfLoopAuxVar= capsule.new_variable();
					DEBUG(capsule.registerVariable(selfLoopAuxVar,"selfLoopAuxVar"));
					impliesOr(solver, -selfLoopAuxVar, labelGroupsWithActualTransitions);
					for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
						andImplies(solver, selfLoopAuxVar, previousStateVars[ts][states], nextStateVars[ts][states]);
						andImplies(solver, -selfLoopAuxVar, previousStateVars[ts][states], -nextStateVars[ts][states]);
					}
				}

				if(chainsParallelism){
					
					for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
						for(size_t l = 0 ; l < labelsWithEffectOnValue[ts][states].size() ; l++){
							if(l < labelsWithEffectOnValue[ts][states].size()-1){
								andImplies(solver, labelVars[labelsWithEffectOnValue[ts][states][l]], nextStateVars[ts][states], topHelperVars[ts][states][l]);
								if(!hasSelfLoopOnValue(ts, states, labelsWithEffectOnValue[ts][states][l+1])){
									implies(solver, topHelperVars[ts][states][l], -labelVars[labelsWithEffectOnValue[ts][states][l+1]]);
								}
								if(!hasSelfLoopOnValue(ts, states, labelsWithEffectOnValue[ts][states][l])){
									implies(solver, bottomHelperVars[ts][states][l], -labelVars[labelsWithEffectOnValue[ts][states][l]]);
								}
							}
							if(l > 0){
								andImplies(solver, labelVars[labelsWithEffectOnValue[ts][states][l]], nextStateVars[ts][states], bottomHelperVars[ts][states][l-1]);
							}
							if(l > 0 && l < labelsWithEffectOnValue[ts][states].size()-1){
								implies(solver, topHelperVars[ts][states][l-1], topHelperVars[ts][states][l]);
								implies(solver, bottomHelperVars[ts][states][l], bottomHelperVars[ts][states][l-1]);
							}
						}
						
					}
				}
			}
		}else if(eliminating_rnc_and_pairs){
			map<int, map<int, vector<int>>> topHelperVars;
			map<int, map<int, vector<int>>> bottomHelperVars;
			if(chainsParallelism){
				topHelperVars = generateHelperVars(capsule/* , int timestep */);
				bottomHelperVars = generateHelperVars(capsule/* , int timestep */);
			}

			for(int ts = 0 ; ts < fts->get_size() ; ts++){
				
				vector<int> labelGroupsWithActualTransitions;
				for(size_t lg = 0 ; lg < labelGroups[ts].size() ; lg++){
					int label = labelGroups[ts][lg][0];

					if(!useLabelGroups){
						if(useSelfloopOptimisation){
							if(isIrrelevantLabel(ts, label)) continue;
							if(isAlwaysSelfLoop(ts, label)){
								auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
								vector<int> preconditions;
								vector<int> effects;
								for(Transition t : transitions){
									preconditions.push_back(previousStateVars[ts][t.src]);
									effects.push_back(nextStateVars[ts][t.target]);
								}
								for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
									impliesOr(solver, labelVars[labelGroups[ts][lg][l]], preconditions);
									impliesOr(solver, labelVars[labelGroups[ts][lg][l]], effects);
								}
								continue;
							}
						}
						if(useSelfloopOptimisation){
							for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
								labelGroupsWithActualTransitions.push_back(labelVars[labelGroups[ts][lg][l]]);//These are labels and not labelgroups
							}
						}

						for(int neg_prec : empty_rows[ts][lg]){
							for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
								impliesNot(solver, labelVars[labelGroups[ts][lg][l]], previousStateVars[ts][neg_prec]);
							}
						}
						for(int neg_eff : empty_cols[ts][lg]){
							for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
								impliesNot(solver, labelVars[labelGroups[ts][lg][l]], nextStateVars[ts][neg_eff]);
							}
						}
						for(int src = 0 ; src < fts->get_ts(ts).get_size() ; src++){
							if(empty_rows[ts][lg].find(src) != empty_rows[ts][lg].end()) continue;
							for(int t : empty_projected_cells_per_row[ts][src]){
								implies(solver, previousStateVars[ts][src], -nextStateVars[ts][t]);
							}
							if(ones_per_row[ts][lg][src].size() == 1){
								int t = *ones_per_row[ts][lg][src].begin();
								for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
									andImplies(solver, labelVars[labelGroups[ts][lg][l]], previousStateVars[ts][src], nextStateVars[ts][t]);
								}
								continue;
							}
							for(int target = 0 ; target < fts->get_ts(ts).get_size() ; target++){
								if(empty_cols[ts][lg].find(target) != empty_cols[ts][lg].end()) continue;
								if(labelProjection[ts][src][target] == 0) continue;
								if(ones_per_row[ts][lg][src].find(target) == ones_per_row[ts][lg][src].end()){
									for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
										andImplies(solver, labelVars[labelGroups[ts][lg][l]], previousStateVars[ts][src], -nextStateVars[ts][target]);
									}
								}
							}
						}

					}else{
						if(useSelfloopOptimisation){
							if(isIrrelevantLabel(ts, label)) continue;
							if(isAlwaysSelfLoop(ts, label)){
								auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
								vector<int> preconditions;
								vector<int> effects;
								for(Transition t : transitions){
									preconditions.push_back(previousStateVars[ts][t.src]);
									effects.push_back(nextStateVars[ts][t.target]);
								}
								if(labelGroupVars[ts][lg] == -1){
									impliesOr(solver, labelVars[labelGroups[ts][lg][0]], preconditions);
									impliesOr(solver, labelVars[labelGroups[ts][lg][0]], effects);
								}else{
									impliesOr(solver, labelGroupVars[ts][lg], preconditions);
									impliesOr(solver, labelGroupVars[ts][lg], effects);
								}
								continue;
							}
						}
						if(useSelfloopOptimisation){
							if(labelGroupVars[ts][lg] == -1){
								labelGroupsWithActualTransitions.push_back(labelVars[labelGroups[ts][lg][0]]);
							}else{
								labelGroupsWithActualTransitions.push_back(labelGroupVars[ts][lg]);
							}
						}

						for(int neg_prec : empty_rows[ts][lg]){
							if(labelGroupVars[ts][lg] == -1){
								impliesNot(solver, labelVars[labelGroups[ts][lg][0]], previousStateVars[ts][neg_prec]);
							}else{
								impliesNot(solver, labelGroupVars[ts][lg], previousStateVars[ts][neg_prec]);
							}
						}
						for(int neg_eff : empty_cols[ts][lg]){
							if(labelGroupVars[ts][lg] == -1){
								impliesNot(solver, labelVars[labelGroups[ts][lg][0]], nextStateVars[ts][neg_eff]);
							}else{
								impliesNot(solver, labelGroupVars[ts][lg], nextStateVars[ts][neg_eff]);
							}
						}
						for(int src = 0 ; src < fts->get_ts(ts).get_size() ; src++){
							if(empty_rows[ts][lg].find(src) != empty_rows[ts][lg].end()) continue;
							for(int t : empty_projected_cells_per_row[ts][src]){
								implies(solver, previousStateVars[ts][src], -nextStateVars[ts][t]);
							}
							if(ones_per_row[ts][lg][src].size() == 1){
								int t = *ones_per_row[ts][lg][src].begin();
								if(labelGroupVars[ts][lg] == -1){
									andImplies(solver, labelVars[labelGroups[ts][lg][0]], previousStateVars[ts][src], nextStateVars[ts][t]);
									continue;
								}
								andImplies(solver, labelGroupVars[ts][lg], previousStateVars[ts][src], nextStateVars[ts][t]);
								continue;
							}
							for(int target = 0 ; target < fts->get_ts(ts).get_size() ; target++){
								if(empty_cols[ts][lg].find(target) != empty_cols[ts][lg].end()) continue;
								if(labelProjection[ts][src][target] == 0) continue;
								if(ones_per_row[ts][lg][src].find(target) == ones_per_row[ts][lg][src].end()){
									if(labelGroupVars[ts][lg] == -1){
										andImplies(solver, labelVars[labelGroups[ts][lg][0]], previousStateVars[ts][src], -nextStateVars[ts][target]);
									}else{
										andImplies(solver, labelGroupVars[ts][lg], previousStateVars[ts][src], -nextStateVars[ts][target]);
									}
								}
							}
						}
					}
				}

				if(useSelfloopOptimisation){
					int selfLoopAuxVar = capsule.new_variable();
					DEBUG(capsule.registerVariable(selfLoopAuxVar,"selfLoopAuxVar"));
					impliesOr(solver, -selfLoopAuxVar, labelGroupsWithActualTransitions);
					for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
						andImplies(solver, selfLoopAuxVar, previousStateVars[ts][states], nextStateVars[ts][states]);
						andImplies(solver, -selfLoopAuxVar, previousStateVars[ts][states], -nextStateVars[ts][states]);
					}
				}

				if(chainsParallelism){
					
					for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
						for(size_t l = 0 ; l < labelsWithEffectOnValue[ts][states].size() ; l++){
							if(l < labelsWithEffectOnValue[ts][states].size()-1){
								andImplies(solver, labelVars[labelsWithEffectOnValue[ts][states][l]], nextStateVars[ts][states], topHelperVars[ts][states][l]);
								if(!hasSelfLoopOnValue(ts, states, labelsWithEffectOnValue[ts][states][l+1])){
									implies(solver, topHelperVars[ts][states][l], -labelVars[labelsWithEffectOnValue[ts][states][l+1]]);
								}
								if(!hasSelfLoopOnValue(ts, states, labelsWithEffectOnValue[ts][states][l])){
									implies(solver, bottomHelperVars[ts][states][l], -labelVars[labelsWithEffectOnValue[ts][states][l]]);
								}
							}
							if(l > 0){
								andImplies(solver, labelVars[labelsWithEffectOnValue[ts][states][l]], nextStateVars[ts][states], bottomHelperVars[ts][states][l-1]);
							}
							if(l > 0 && l < labelsWithEffectOnValue[ts][states].size()-1){
								implies(solver, topHelperVars[ts][states][l-1], topHelperVars[ts][states][l]);
								implies(solver, bottomHelperVars[ts][states][l], bottomHelperVars[ts][states][l-1]);
							}
						}
					}
				}
			}
		}
		swap(previousStateVars, nextStateVars);

		cout << "Constructed time " << timestep << " of " << currentLength << ". Now " << get_number_of_clauses() << " clauses and " << capsule.number_of_variables << " variables." << endl;
	
		if (stepTimeLimit != -1){
			auto t_now = std::chrono::system_clock::now();
			std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_start);
			std::chrono::milliseconds time_limit(stepTimeLimit * 1000);
			if(time_limit <= elapsed) {
				// generation of formula ran out of time
				cout << "Generation of SAT formula exceeded time limit. Aborting overall run." << endl;
				return FAILED;
			}
		}
	}

	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		vector<int> goals = fts->get_ts(ts).get_goal_states();
		vector<int> goalStateVars;
		for(size_t goal = 0 ; goal < goals.size() ; goal++){
			goalStateVars.push_back(previousStateVars[ts][goals[goal]]);
		}
		atLeastOne(solver, capsule, goalStateVars);

		//cout << endl << endl << "Factor " << ts << endl;
		//fts->get_ts(ts).dump_dot_graph();
	}

	//DEBUG(capsule.printVariables());

	cout << "Formula has " << get_number_of_clauses() << " clauses and " << capsule.number_of_variables << " variables." << endl;

	
	int solverState;

	if (stepTimeLimit == -1){
		solverState = ipasir_solve(solver);
	} else {
		std::atomic_bool stop(false);
		solver_timer timer(solver,stepTimeLimit * 1000,stop,t_start);
	    std::thread thread_for_timer(timer);
		
		solverState = ipasir_solve(solver);
		
		// Stop it
	    timer.stop = true;
		thread_for_timer.join();
	}
 
	cout << "SAT solver state: " << solverState << endl;

	if (solverState == 10){
		/* cout << allTimesStateVars[1][1][0] << endl;
		cout << allTimesStateVars[1][1][1] << endl;
		cout << allTimesLabelVars[1][7] << endl;
		cout << allTimesTransitionVars[1][1][7][1].second << endl;
		cout << ipasir_val(solver, allTimesStateVars[1][1][0]) << endl;
		cout << ipasir_val(solver, allTimesStateVars[1][1][1]) << endl;
		cout << ipasir_val(solver, allTimesLabelVars[1][7]) << endl;
		cout << ipasir_val(solver, allTimesTransitionVars[1][1][7][1].second) << endl; */

		checkSolution(allTimesStateVars, allTimesLabelVars, allTimesTransitionVars, currentLength, solver);

		// run plan extraction
		// likely check_goal_and_set_plan with four arguments
		vector<vector<int>> statesPerTimestep;
		vector<vector<int>> labelsPerTimestep;
		vector<int> stateReconstructor;
		for(size_t ts = 0 ; ts < allTimesStateVars[0].size() ; ts++){
			for(size_t state = 0 ; state < allTimesStateVars[0][ts].size() ; state++){
				if(ipasir_val(solver, allTimesStateVars[0][ts][state]) > 0){
					stateReconstructor.push_back(state);
					break;
				}
			}
		}
		statesPerTimestep.push_back(stateReconstructor);
		stateReconstructor.clear();
	
		set<int> timesteps_with_labels;	

		for(int timestep = 1 ; timestep <= currentLength ; timestep++){
			cout << "Time " << timestep << endl;
			vector<int> selectedLabels;
			for(size_t label = 0 ; label < allTimesLabelVars[timestep-1].size() ; label++){
				if(do_R2_encoding) stateReconstructor.clear();
				if(ipasir_val(solver, allTimesLabelVars[timestep-1][label]) <= 0){
					continue;
				}else{
					selectedLabels.push_back(label);
					timesteps_with_labels.insert(timestep);
					cout << "Label : " << label << endl;
					for(int ts = 0 ; ts < fts->get_size() ; ts++){
						if(do_R2_encoding){
							if(allTimesTransitionVars[timestep-1][ts][label].size() == 0){
								stateReconstructor.push_back(statesPerTimestep.back()[ts]);
							}else{
								bool addedState = false;
								for(pair<Transition, int> transition : allTimesTransitionVars[timestep-1][ts][label]){
									if(ipasir_val(solver, transition.second) > 0 && transition.second != -1){
										stateReconstructor.push_back(transition.first.target);
										addedState = true;
										break;
									}
								}
								if(!addedState){
									stateReconstructor.push_back(statesPerTimestep.back()[ts]);
								}
							}
						}else{
							for(size_t state = 0 ; state < allTimesStateVars[timestep][ts].size() ; state++){
								if(ipasir_val(solver, allTimesStateVars[timestep][ts][state]) > 0){
									stateReconstructor.push_back(state);
									break;
								}
							}
							
						}
					}
					if(do_R2_encoding) statesPerTimestep.push_back(stateReconstructor);
				}
			}

			if(selectedLabels.size() > 1 && !do_R2_encoding){
				for(size_t l = 0 ; l < selectedLabels.size() - 1 ; l++){
					vector<int> intermediateState;
					for(int ts = 0 ; ts < fts->get_size() ; ts++){
						if(isAlwaysSelfLoop(ts, selectedLabels[l])){
							intermediateState.push_back(statesPerTimestep.back()[ts]);
						}else{
							intermediateState.push_back(stateReconstructor[ts]);
						}
					}
					statesPerTimestep.push_back(intermediateState);
				}
			}

			
			if(!do_R2_encoding){
				vector<int> notRepeated(stateReconstructor.begin(), stateReconstructor.begin() + fts->get_size());
				statesPerTimestep.push_back(notRepeated);
				stateReconstructor.clear();
				notRepeated.clear();
			}

			//cout << statesPerTimestep << endl;
			
			if (selectedLabels.size()){
				set<int> labelSet(selectedLabels.begin(), selectedLabels.end());
				selectedLabels.clear();
				for (const int & l : labelOrder)
					if (labelSet.count(l)){
						cout << "Actual Order label: " << l << endl;
						selectedLabels.push_back(l);
					}
			}

			labelsPerTimestep.push_back(selectedLabels);
		}

		vector<int> GS = statesPerTimestep.back();
		//for(auto x : statesPerTimestep){
		//	cout << x.size() << ":";
		//	for (size_t f = 0; f < x.size() ; f++)
		//		cout << " " << f << "=" << x[f];
		//	cout << endl;
		//}
		PlanState goalState = PlanState(std::move(GS));
		vector<PlanState> states;
		for(size_t s = 0 ; s < statesPerTimestep.size() ; s++){
			states.push_back(PlanState(std::move(statesPerTimestep[s])));
		}

		vector<int> labels;
		for(size_t o = 0 ; o < labelsPerTimestep.size() ; o++){
			for(size_t o1 = 0 ; o1 < labelsPerTimestep[o].size() ; o1++){
				labels.push_back(labelsPerTimestep[o][o1]);
			}
		}

		cout << "Total states: " << states.size() << endl;
		cout << "Total labels: " << labels.size() << endl;
		cout << "Total timesteps with label: " << timesteps_with_labels.size() << endl;

		check_goal_and_set_plan(goalState, states, std::move(labels), fts);

		ipasir_release(solver);
		
		cout << "STEP " << stepNumber << " length " << currentLength
				<< " SAT time " << step_timer
				<< " clauses " << get_number_of_clauses() << " vars " << capsule.number_of_variables
				<< " labels " << labels.size() << " timesteps with label " << timesteps_with_labels.size()
				<< " compression " << double(labels.size()) / timesteps_with_labels.size()
				<< endl;
		if (maximum_iteration == -1){
			return SOLVED;
		}
	} else {
		cout << "STEP " << stepNumber << " length " << currentLength
				<< " UNSAT time " << step_timer
				<< " clauses " << get_number_of_clauses() << " vars " << capsule.number_of_variables
				<< endl;
		ipasir_release(solver);
	}


	// otherwise
	if (planLength == currentLength || (length_by_iteration && stepNumber == maximum_iteration))
		return FAILED;
	else {
		allTimesStateVars.clear();
		allTimesLabelVars.clear();
		
		stepNumber++;
		if (length_by_iteration){
			currentLength = int(0.5 + start_length * pow(multiplier, stepNumber));
		} else // simple sequential iteration
			currentLength++;
		return IN_PROGRESS;
	}
}

void SATSearch::print_statistics() const{
	statistics.print_detailed_statistics();
}

};
