#include "sat_search.h"

// #include "../plugins/options.h"
#include "../utils/logging.h"
#include "ipasir.h"
#include "sat_encoder.h"

using namespace std;

namespace sat_search {
SATSearch::SATSearch(const Options &opts): SearchEngine(opts),
	planLength(opts.get<int>("plan_length")),
	fts(g_main_task){
}

void SATSearch::initialize() {
	cout << "Initialising" << endl;

	cout << "My FTS task has " << fts->get_size() << " systems." << endl;


	if (planLength != -1){
		currentLength = planLength;
	} else {
		currentLength = 1;
	}
}

vector<vector<int>> SATSearch::generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */){
	vector<vector<int>> stateVars(fts()->get_size());
	for(size_t ts = 0 ; ts < fts()->get_size() ; ts++){
		for(size_t states = 0 ; states < fts->get_ts(ts)->get_size() ; states++){
			int stateVar = capsule.new_variable();
			stateVars[ts].push_back(stateVar);
		}
		atMostOne(solver, capsule, stateVars[ts]);
		atLeastOne(solver, capsule, stateVars[ts]);
	}
	return stateVars;
}

vector<int> SATSearch::generateLabelVars(void* solver, sat_capsule & capsule/* , int timestep */){
	vector<int> labelVars(fts()->get_num_labels());
	for(size_t label = 0 ; label < fts()->get_num_labels() ; label++){
		int labelVar = capsule.new_variable();
		labelVars[label] = labelVar;
	}
	atMostOne(solver, capsule, labelVar);
	atLeastOne(solver, capsule, labelVar);
	return labelVars;
}

map<int, map<int, vector<int>>> SATSearch::getApplicableLabels(){
	map<int, map<int, vector<int>>> applicableLabels;
	for(size_t ts = 0 ; ts < fts()->get_size() ; ts++){
		for(size_t label = 0 ; label < fts()->get_num_labels() ; label++){
			vector<int> labelPrec = fts[ts]->get_label_precondition(label);
			for(size_t state = 0 ; state < labelPrec.size() ; state++){
				applicableLabels[ts][labelPrec[state]].push_back(label);
			}
		}
	}
	return applicableLabels
}

map<int, map<int, vector<int>>> SATSearch::getSuccessorStates(map<int, map<int, vector<int>>> applicableLabels){
	map<int, map<int, map<int, vector<int>>>> successorStates;
	for(size_t ts = 0 ; ts < fts()->get_size() ; ts++){
		for(size_t states = 0 ; states < fts->get_ts(ts)->get_size() ; states++){
			for(size_t label = 0 ; label < applicableLabels[ts][states].size() ; label++){
				vector<Transition> transitions = fts[ts]->get_transitions_with_label(applicableLabels[ts][states][label]);
				for(size_t t = 0 ; t < transitions.size() ; t++){
					if(transitions[t].src == states){
						successorStates[ts][states][applicableLabels[ts][states][label]].push_back(transitions[t].target);
					}
				}
			}
		}
	}
	return successorStates;
}


SearchStatus SATSearch::step() {
	cout << "HI doing step! SAT: " << ipasir_signature() << endl;

	sat_capsule capsule;
	reset_number_of_clauses();
	void* solver = ipasir_init();
	// try to solve for length currentLength
	map<int, map<int, vector<int>>> applicableLabels = getApplicableLabels();
	map<int, map<int, map<int, vector<int>>>> successorStates = getSuccessorStates(applicableLabels);

	vector<vector<int>> previousStateVars = generateStateVars(solver, capsule/* , 0 */);

	for(size_t ts = 0 ; ts < fts()->get_size() ; ts++){
		assertYes(solver, previousStateVars[ts][fts[ts]->get_init_state()]);
	}

	vector<int> labelVars;
	vector<vector<int>> nextStateVars;
	for(int timestep = 1 ; timestep <= currentLength ; timestep++){
		labelVars = generateLabelVars(solver, capsule/* , timestep */);
		nextStateVars = generateStateVars(solver, capsule/* , timestep */);

		for(size_t ts = 0 ; ts < previousStateVars.size() ; ts++){
			for(size_t states = 0 ; states < previousStateVars[ts].size() ; states++){
				vector<int> appLabelVars;
				for(size_t l = 0 ; l < applicableLabels[ts][states].size() ; l++){
					appLabelVars.push_back(labelVars[applicableLabels[ts][states][l]]);
					vector<int> succStateVars;
					for(size_t s = 0 ; s < successorStates[ts][states][l].size() ; s++){
						succStateVars.push_back(nextStateVars[ts][successorStates[ts][states][l][s]]);
					}
					andImpliesOr(solver, previousStateVars[ts][states], labelVars[applicableLabels[ts][states][l]], succStateVars);
				}
				impliesOr(previousStateVars[ts][states], appLabelVars);
				//appLabelVars.clear();
			}
		}

		swap(previousStateVars, nextStateVars);
	}

	for(size_t ts = 0 ; ts < fts()->get_size() ; ts++){
		vector<int> goals = fts[ts]->get_goal_states();
		vector<int> goalStateVars;
		for(size_t goal = 0 ; goal < goals.size() ; goal++){
			goalStateVars.push_back(previousStateVars[ts][goals[goal]]);
		}
		atLeastOne(solver, capsule, goalStateVars);
	}


	//implies(solver,2,3);	


	int solverState = ipasir_solve(solver);
	cout << "SAT solver state: " << solverState << endl;

	if (solverState == 10){
		// run plan extraction
		// likely check_goal_and_set_plan with four arguments
		ipasir_release(solver);
		return SOLVED;
	}


	ipasir_release(solver);
	// otherwise
	if (planLength == currentLength)
		return FAILED;
	else {
		currentLength++; // TODO better strategies for satisficing
		return IN_PROGRESS;
	}
}


void SATSearch::print_statistics() const{
	statistics.print_detailed_statistics();
}

};
