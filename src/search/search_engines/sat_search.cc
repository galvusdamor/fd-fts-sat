#include "sat_search.h"

// #include "../plugins/options.h"
#include "../utils/logging.h"
#include "ipasir.h"
#include "sat_encoder.h"

using namespace std;
using namespace task_representation;

namespace sat_search {
SATSearch::SATSearch(const Options &opts): SearchEngine(opts),
	planLength(opts.get<int>("plan_length")),
	fts(g_main_task){
}

void SATSearch::initialize() {
	cout << "Initialising" << endl;

	cout << "My FTS task has " << fts->get_size() << " systems." << endl;

	np_labels.resize(fts->get_num_labels());//non parallel labels
	for(int l1 = 0 ; l1 < fts->get_num_labels() ; l1++){
		np_labels[l1].push_back(l1);
		for(int l2 = l1+1 ; l2 < fts->get_num_labels() ; l2++){
			for(int ts = 0 ; ts < fts->get_size() ; ts++){
				if( !fts->get_ts(ts).is_selfloop_everywhere((task_representation::LabelID) l1) && !fts->get_ts(ts).is_selfloop_everywhere((task_representation::LabelID) l2)){
					np_labels[l1].push_back(l2);
					break;
				}
			}
		}
	}

	for(int labelID = 0 ; labelID < fts->get_num_labels() ; labelID++){
		labelOrder.push_back(labelID);
	}

	relevantLabels.resize(fts->get_size());
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int label = 0 ; label < fts->get_num_labels() ; label++){
			if(!fts->get_ts(ts).is_selfloop_everywhere((task_representation::LabelID)label)){
				relevantLabels[ts].push_back(label);
			}
		}
	}

	if (planLength != -1){
		currentLength = planLength;
	} else {
		currentLength = 1;
	}
}

vector<vector<int>> SATSearch::generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */){
	vector<vector<int>> stateVars(fts->get_size());
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			int stateVar = capsule.new_variable();
			stateVars[ts].push_back(stateVar);
		}
		atMostOne(solver, capsule, stateVars[ts]);
		atLeastOne(solver, capsule, stateVars[ts]);
	}
	return stateVars;
}

vector<int> SATSearch::generateLabelVars(void* solver, sat_capsule & capsule/* , int timestep */){
	vector<int> labelVars(fts->get_num_labels());
	for(int label = 0 ; label < fts->get_num_labels() ; label++){
		int labelVar = capsule.new_variable();
		labelVars[label] = labelVar;
	}
	//atMostOne(solver, capsule, labelVars);
	atLeastOne(solver, capsule, labelVars);
	/* for(auto v : np_labels){
		for(size_t l = 1 ; l < v.size() ; l++){
			impliesNot(solver, labelVars[v[0]], labelVars[v[l]]);
		}
	} */
	return labelVars;
}

map<int, map<int, vector<pair<Transition, int>>>> SATSearch::generateTransitionVars(void* solver, sat_capsule &capsule){//[transition_systems[labels[<transition, SATVar>, <transition, SATVar>...]]]
	map<int, map<int, vector<pair<Transition, int>>>> transitionVars;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int label = 0 ; label < fts->get_num_labels() ; label++){
			if(fts->get_ts(ts).is_selfloop_everywhere((task_representation::LabelID)label)){
				//transitionVars[ts][label].push_back({(src=-1,target=-1),labelVars[label]});
				continue;
			}
			auto transitions = fts->get_ts(ts).get_transitions_with_label(labelOrder[label]);
			vector<int> SATVars;
			for(size_t t = 0 ; t < transitions.size() ; t++){
				int transitionVar = capsule.new_variable();
				SATVars.push_back(transitionVar);
				transitionVars[ts][label].push_back({transitions[t],transitionVar});
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
				if(fts->get_ts(ts).is_selfloop_everywhere((task_representation::LabelID)label)){
					auxVars[ts][states].push_back(-1);
					continue;
				}
				int auxVar = capsule.new_variable();
				auxVars[ts][states].push_back(auxVar);
			}
			cout << auxVars[ts][states].size() << endl;
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


SearchStatus SATSearch::step() {
	cout << "HI doing step! SAT: " << ipasir_signature() << endl;
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
	vector<vector<int>> nextStateVars;
	map<int, map<int, vector<int>>> auxVars;
	for(int timestep = 1 ; timestep <= currentLength ; timestep++){
		labelVars = generateLabelVars(solver, capsule/* , int timestep */);
		allTimesLabelVars.push_back(labelVars);
		nextStateVars = generateStateVars(solver, capsule/* , timestep */);
		allTimesStateVars.push_back(nextStateVars);
		transitionVars = generateTransitionVars(solver, capsule/* , timestep */);
		allTimesTransitionVars.push_back(transitionVars);
		auxVars = generateAuxVars(capsule);

		for(int ts = 0 ; ts < fts->get_size() ; ts++){
			for(int label = 0 ; label < fts->get_num_labels() ; label++){
				vector<int> labelTransitionSATVars;
				for(pair<Transition, int> transition : transitionVars[ts][label]){
					labelTransitionSATVars.push_back(transition.second);
					vector<int> impliesOrPrec;
					impliesOrPrec.push_back(previousStateVars[ts][transition.first.src]);
					for(int label_prec = 0 ; label_prec < label ; label_prec++){
						for(pair<Transition, int> transition_prec : transitionVars[ts][label_prec]){
							if(transition_prec.first.target == transition.first.src){
								impliesOrPrec.push_back(transition_prec.second);
							}
						}
					}
					impliesOr(solver, transition.second, impliesOrPrec);

					vector<int> impliesOrEff;
					impliesOrEff.push_back(nextStateVars[ts][transition.first.target]);
					for(int label_eff = label+1 ; label_eff < fts->get_num_labels() ; label_eff++){
						for(pair<Transition, int> transition_eff : transitionVars[ts][label_eff]){
							if(transition_eff.first.target != transition.first.target){
								impliesOrEff.push_back(transition_eff.second);
							}
						}
					}
					impliesOr(solver, transition.second, impliesOrEff);
				}
				if(labelTransitionSATVars.size() > 0){
					impliesOr(solver, labelVars[label], labelTransitionSATVars);
					for(size_t ltsv = 0 ; ltsv < labelTransitionSATVars.size() ; ltsv++){
						implies(solver, labelTransitionSATVars[ltsv], labelVars[label]);
					}
				}

			}

			for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
				set<int> negatedRelevantLabels;
				negatedRelevantLabels.insert(previousStateVars[ts][states]);
				for(int label = 0 ; label < fts->get_num_labels() ; label++){
					if(!fts->get_ts(ts).is_selfloop_everywhere((task_representation::LabelID)label)){
						negatedRelevantLabels.insert(-labelVars[label]);
					}
				}
				andImplies(solver, negatedRelevantLabels, nextStateVars[ts][states]);
				for(pair<Transition, int> transition : transitionVars[ts][relevantLabels[ts][0]]){
					if(transition.first.target != states && transition.first.src != transition.first.target){
						implies(solver, transition.second, auxVars[ts][states][relevantLabels[ts][0]]);
					}
				}
				for(size_t relevantLabel = 1 ; relevantLabel < relevantLabels[ts].size()-1 ; relevantLabel++){
					vector<int> supportingTransitions;
					supportingTransitions.push_back(auxVars[ts][states][relevantLabels[ts][relevantLabel]]);
					for(pair<Transition, int> transition : transitionVars[ts][relevantLabels[ts][relevantLabel]]){
						if(transition.first.target != states && transition.first.src != transition.first.target){
							implies(solver, transition.second, auxVars[ts][states][relevantLabels[ts][relevantLabel]]);
						}
						if(transition.first.target == states && transition.first.src != transition.first.target){
							supportingTransitions.push_back(transition.second);
						}
						if(transition.first.src == states){
							impliesNot(solver, auxVars[ts][states][relevantLabels[ts][relevantLabel-1]], transition.second);
						}
					}
					impliesOr(solver, auxVars[ts][states][relevantLabels[ts][relevantLabel-1]], supportingTransitions);
				}
				for(pair<Transition, int> transition : transitionVars[ts][fts->get_num_labels()]){
					if(transition.first.src == states){
						impliesNot(solver, auxVars[ts][states][fts->get_num_labels()-1], transition.second);
					}
				}
			}
		}

		swap(previousStateVars, nextStateVars);

	}

	// vector<int> labelVars;
	// vector<vector<int>> nextStateVars;
	// for(int timestep = 1 ; timestep <= currentLength ; timestep++){
	// 	labelVars = generateLabelVars(solver, capsule/* , timestep */);
	// 	allTimesLabelVars.push_back(labelVars);
	// 	nextStateVars = generateStateVars(solver, capsule/* , timestep */);
	// 	allTimesStateVars.push_back(nextStateVars);

	// 	for(size_t ts = 0 ; ts < previousStateVars.size() ; ts++){
	// 		for(size_t states = 0 ; states < previousStateVars[ts].size() ; states++){
	// 			//vector<int> appLabelVars;
	// 			set<int> appLabelVars;
	// 			for(size_t l = 0 ; l < applicableLabels[ts][states].size() ; l++){
	// 				//appLabelVars.push_back(labelVars[applicableLabels[ts][states][l]]);
	// 				appLabelVars.insert(applicableLabels[ts][states][l]);
	// 				vector<int> succStateVars;
	// 				for(size_t s = 0 ; s < successorStates[ts][states][applicableLabels[ts][states][l]].size() ; s++){
	// 					if(parallelism && (int)states == successorStates[ts][states][applicableLabels[ts][states][l]][s]){//found a self loop
	// 						set<int> labelsInStateWithSelfLoop = {labelVars[applicableLabels[ts][states][l]]};
	// 						for(size_t l_aux = 0 ; l_aux < applicableLabels[ts][states].size() ; l_aux++){
	// 							if(l == l_aux) continue;
	// 							labelsInStateWithSelfLoop.insert(-labelVars[applicableLabels[ts][states][l_aux]]);
	// 						}
	// 						andImplies(solver, labelsInStateWithSelfLoop, nextStateVars[ts][successorStates[ts][states][applicableLabels[ts][states][l]][s]]);
	// 					}else{
	// 						succStateVars.push_back(nextStateVars[ts][successorStates[ts][states][applicableLabels[ts][states][l]][s]]);
	// 					}
	// 				}
	// 				if(succStateVars.size() > 0){
	// 					andImpliesOr(solver, previousStateVars[ts][states], labelVars[applicableLabels[ts][states][l]], succStateVars);
	// 				}
	// 			}
	// 			assert(appLabelVars.size() > 0);
	// 			for(size_t l = 0 ; l < labelVars.size() ; l++){
	// 				if(appLabelVars.find(l) == appLabelVars.end()){
	// 					impliesNot(solver, previousStateVars[ts][states], labelVars[l]);
	// 				}
	// 			}
	// 			//impliesOr(solver, previousStateVars[ts][states], appLabelVars);
				
	// 		}
	// 	}

	// 	swap(previousStateVars, nextStateVars);
	// }



	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		vector<int> goals = fts->get_ts(ts).get_goal_states();
		vector<int> goalStateVars;
		for(size_t goal = 0 ; goal < goals.size() ; goal++){
			goalStateVars.push_back(previousStateVars[ts][goals[goal]]);
		}
		atLeastOne(solver, capsule, goalStateVars);

		fts->get_ts(ts).dump_dot_graph();
	}


	//implies(solver,2,3);	


	int solverState = ipasir_solve(solver);
	cout << "SAT solver state: " << solverState << endl;

	if (solverState == 10){
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
		

		for(int timestep = 1 ; timestep <= currentLength ; timestep++){
			
			
			vector<int> selectedLabels;
			for(size_t label = 0 ; label < allTimesLabelVars[timestep-1].size() ; label++){
				stateReconstructor.clear();
				if(ipasir_val(solver, allTimesLabelVars[timestep-1][label]) <= 0){
					continue;
				}else{
					selectedLabels.push_back(label);
					cout << "Label : " << label << endl;
					for(int ts = 0 ; ts < fts->get_size() ; ts++){
						if(allTimesTransitionVars[timestep-1][ts][label].size() == 0){
							stateReconstructor.push_back(statesPerTimestep.back()[ts]);
						}else{
							for(pair<Transition, int> transition : allTimesTransitionVars[timestep-1][ts][label]){
								if(ipasir_val(solver, transition.second) > 0){
									stateReconstructor.push_back(transition.first.target);
									continue;
								}
							}
						}
					}
				}
				statesPerTimestep.push_back(stateReconstructor);
			}
			labelsPerTimestep.push_back(selectedLabels);
			/* stateReconstructor.clear();
			for(size_t ts = 0 ; ts < allTimesStateVars[timestep].size() ; ts++){
				for(size_t state = 0 ; state < allTimesStateVars[timestep][ts].size() ; state++){
					if(ipasir_val(solver, allTimesStateVars[timestep][ts][state]) > 0){
						stateReconstructor.push_back(state);
						break;
					}
				}
			}
			statesPerTimestep.push_back(stateReconstructor); */
			
		}

		vector<int> GS = statesPerTimestep.back();
		for(auto x : statesPerTimestep){
			cout << x.size() << endl; 
		}
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

		cout << "Total states : " << states.size() << endl;
		cout << "Total labels : " << labels.size() << endl;

		check_goal_and_set_plan(goalState, states, std::move(labels), fts);

		ipasir_release(solver);
		return SOLVED;
	}


	ipasir_release(solver);
	// otherwise
	if (planLength == currentLength)
		return FAILED;
	else {
		allTimesStateVars.clear();
		allTimesLabelVars.clear();
		currentLength++; // TODO better strategies for satisficing
		return IN_PROGRESS;
	}
}


void SATSearch::print_statistics() const{
	statistics.print_detailed_statistics();
}

};
