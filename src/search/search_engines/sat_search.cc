#include "sat_search.h"

// #include "../plugins/options.h"
#include "../utils/logging.h"
#include "../task_representation/transition_system.h"
#include "ipasir.h"
#include "sat_encoder.h"

using namespace std;

namespace sat_search {
SATSearch::SATSearch(const Options &opts): SearchEngine(opts),
	planLength(opts.get<int>("plan_length")),
	fts(g_main_task){
}


struct BDDError {};


// promise from symbolic
void
exceptionError(string /*message*/) {
    //cout << message << endl;
    throw BDDError();
}


void SATSearch::bdd_to_dot(const BDD &bdd, const std::string &file_name) const {
  std::vector<string> var_names(bdd_num_vars);
  for(int f = 0; f < num_factor_vars; f++){
	  if (f < num_factor_vars / 2)
		  var_names[f] = "factor_state_" + to_string(f);
	  else
		  var_names[f] = "factor_next_state_" + to_string(f - (num_factor_vars/2));
	  cout << "F" << f << " " << var_names[f] << endl;
  }
  for(int l = 0; l < fts->get_num_labels(); l++)
	  var_names[num_factor_vars + l] = "label_" + to_string(labelOrdering[l]);

  std::vector<char *> names(var_names.size());
  for (size_t i = 0; i < var_names.size(); ++i) {
    names[i] = &var_names[i].front();
  }
  FILE *outfile = fopen(file_name.c_str(), "w");
  DdNode **ddnodearray = (DdNode **)malloc(sizeof(bdd.Add().getNode()));
  ddnodearray[0] = bdd.Add().getNode();
  Cudd_DumpDot(_manager->getManager(), 1, ddnodearray, names.data(), NULL,
               outfile); // dump the function to .dot file
  free(ddnodearray);
  fclose(outfile);
}


map<DdNode *, int> tseitsinVars;



int SATSearch::myRecursion(DdNode * node, vector<int> & factorVars, void* solver, sat_capsule & capsule){
	// for lookup
	DdNode * myRegular = Cudd_Regular(node);

	if (tseitsinVars.count(myRegular)){
		int myVar = tseitsinVars[myRegular];
		if (Cudd_IsComplement(node)) myVar *= -1;
		return myVar;
	}
	int thisVar = tseitsinVars.size() + 1;
	tseitsinVars[myRegular] = thisVar;


	assert(!Cudd_IsConstant(node));

	// branching node
	int var_to_branch = Cudd_NodeReadIndex(node);
    DdNode* true_branch = Cudd_T(node);
    DdNode* false_branch = Cudd_E(node);


	if (Cudd_IsConstant(true_branch)){
		bool isTrue = !Cudd_IsComplement(true_branch);

		if (isTrue)
			cout << "V" << var_to_branch << " -> " << "T" << thisVar << endl;
		else
			cout << "V" << var_to_branch << " -> " << "T" << -thisVar << endl;
	} else {
		int branchvar = myRecursion(true_branch, factorVars, solver, capsule);

		cout << "V" << var_to_branch << " & " << "T" << thisVar << " -> " << "T" << branchvar << endl;
		cout << "V" << var_to_branch << " & " << "T-" << thisVar << " -> " << "T-" << branchvar << endl;
	}

	if (Cudd_IsConstant(false_branch)){
		bool isTrue = !Cudd_IsComplement(false_branch);

		if (isTrue)
			cout << "V" << -var_to_branch << " -> " << "T" << thisVar << endl;
		else
			cout << "V" << -var_to_branch << " -> " << "T" << -thisVar << endl;
	} else {
		int branchvar = myRecursion(false_branch, factorVars, solver, capsule);

		cout << "V" << -var_to_branch << " & " << "T" << thisVar << " -> " << "T" << branchvar << endl;
		cout << "V" << -var_to_branch << " & " << "T-" << thisVar << " -> " << "T-" << branchvar << endl;
	}


	if (Cudd_IsComplement(node)) return -thisVar;
	return thisVar;
}



void SATSearch::initialize() {
	cout << "Initialising" << endl;

	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;
  


	combineAllBDDsIntoOne = true;
	bdd_num_vars = fts->get_num_labels();
	num_factor_vars = 0;
	if (combineAllBDDsIntoOne) {
		for (int fac = 0; fac < fts->get_size(); fac++){
			const task_representation::TransitionSystem & factor = fts->get_ts(fac);
			if (num_factor_vars < factor.get_size()) num_factor_vars = factor.get_size();
		}
		num_factor_vars*=2;
		// add the variables for the factor -- those come first
		bdd_num_vars += num_factor_vars; 
	}
	cout << "Number BDD vars: " << bdd_num_vars << " of that " << num_factor_vars << " factor state variables." << endl;
   
    _manager = std::make_unique<Cudd> (bdd_num_vars, 0,
                                          cudd_init_nodes / fts->get_num_labels(),
                                          cudd_init_cache_size,
                                          cudd_init_available_memory);

    _manager->setHandler(exceptionError);
    _manager->setTimeoutHandler(exceptionError);
    _manager->setNodesExceededHandler(exceptionError);


	labelOrdering.resize(fts->get_num_labels());
	// for now just the natural ordering
	for(int l = 0; l < fts->get_num_labels(); l++) labelOrdering[l] = l;


	for (int fac = 0; fac < fts->get_size(); fac++){
		cout << "Precomputation for factor Nr " << fac << endl;
		const task_representation::TransitionSystem & factor = fts->get_ts(fac);
		cout << "This factor has " << factor.get_size() << " many states." << endl;

		// we compute a matrix of size |S|^2 that contains all-pair-possible-paths
		vector<vector<BDD>> allPossiblePaths (factor.get_size());
		for (int s = 0; s < factor.get_size(); s++){
			allPossiblePaths[s].resize(factor.get_size());
			for (int ss = 0; ss < factor.get_size(); ss++){
				if (s == ss){
					allPossiblePaths[s][ss] = _manager->bddOne();
				} else {
					allPossiblePaths[s][ss] = _manager->bddZero(); 
				}
			}
		}



		// DP backwards(!) over the relevant labels
		for(int l = fts->get_num_labels() -  1; l >= 0; l--){
			int label = labelOrdering[l];
			task_representation::LabelID labelID (label);
			cout << "Processing label " << label << "." << endl;
			if (!factor.is_relevant_label(labelID)){
				cout << "\tLabel is not relevant for factor. Skipping." << endl;
				continue;
			}
			vector<vector<BDD>> nextPossiblePaths (factor.get_size());
			for (int s = 0; s < factor.get_size(); s++){
				nextPossiblePaths[s].resize(factor.get_size());
				for (int ss = 0; ss < factor.get_size(); ss++){
					// situation: I am in state s and need to go to state ss.
					// The first label I can use for this transition is "label" (current variable).
					// I have two options: either use it and go to one of its successors, or stay here.

					// base case: don't use the label
					nextPossiblePaths[s][ss] = ~_manager->bddVar(label + num_factor_vars) * allPossiblePaths[s][ss];

					// inductive case: use the label and go one step
					for (const auto & transition : factor.get_transitions_with_label(label)){
						if (transition.src != s) continue;
						nextPossiblePaths[s][ss] +=
								_manager->bddVar(label + num_factor_vars) * allPossiblePaths[transition.target][ss];
					}
				}
			}
			swap(allPossiblePaths,nextPossiblePaths);	
		}

		// compute the union BDD that describes all transitions at the same time.
		BDD allTransitionsBDD = _manager->bddZero();
		for (int s = 0; s < factor.get_size(); s++){
			for (int ss = 0; ss < factor.get_size(); ss++){
				BDD thisFactorTransitionBDD = _manager->bddVar(s) * _manager->bddVar(num_factor_vars/2 + ss);
				for (int nots = 0; nots < factor.get_size(); nots++)
					if (s != nots) thisFactorTransitionBDD *= ~_manager->bddVar(nots);
				for (int notss = 0; notss < factor.get_size(); notss++)
					if (ss != notss) thisFactorTransitionBDD *= ~_manager->bddVar(num_factor_vars/2 + notss);

				thisFactorTransitionBDD *= allPossiblePaths[s][ss];

				allTransitionsBDD += thisFactorTransitionBDD;
			}
		}

		string name = "dots/factor_" + to_string(fac) + ".dot";
		bdd_to_dot(allTransitionsBDD, name);

		transition_BDDs_per_factor.push_back(allTransitionsBDD);

		//int overallVar = myRecursion(allTransitionsBDD.getNode());
		//cout << "Overall :" << "T" << overallVar << endl;
		//exit(0);

		// printing
		//for (int s = 0; s < factor.get_size(); s++){
		//	for (int ss = 0; ss < factor.get_size(); ss++){
		//		string name = "dots/factor_" + to_string(fac) + "_" + to_string(s) + "_to_" + to_string(ss) + ".dot";
		//		bdd_to_dot(allPossiblePaths[s][ss], name);
		//	}
		//}

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
	atMostOne(solver, capsule, labelVars);
	atLeastOne(solver, capsule, labelVars);
	return labelVars;
}

map<int, map<int, vector<int>>> SATSearch::getApplicableLabels(){
	map<int, map<int, vector<int>>> applicableLabels;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int label = 0 ; label < fts->get_num_labels() ; label++){
			vector<int> labelPrec = fts->get_ts(ts).get_label_precondition((task_representation::LabelID)label);
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
				auto transitions = fts->get_ts(ts).get_transitions_with_label(applicableLabels[ts][states][label]);
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
	vector<vector<vector<int>>> allTimesStateVars;
	vector<vector<int>> allTimesLabelVars;
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

	vector<int> labelVars;
	vector<vector<int>> nextStateVars;
	for(int timestep = 1 ; timestep <= currentLength ; timestep++){
		labelVars = generateLabelVars(solver, capsule/* , timestep */);
		allTimesLabelVars.push_back(labelVars);
		nextStateVars = generateStateVars(solver, capsule/* , timestep */);
		allTimesStateVars.push_back(nextStateVars);

		for(size_t ts = 0 ; ts < previousStateVars.size() ; ts++){
			for(size_t states = 0 ; states < previousStateVars[ts].size() ; states++){
				vector<int> appLabelVars;
				for(size_t l = 0 ; l < applicableLabels[ts][states].size() ; l++){
					appLabelVars.push_back(labelVars[applicableLabels[ts][states][l]]);
					vector<int> succStateVars;
					for(size_t s = 0 ; s < successorStates[ts][states][applicableLabels[ts][states][l]].size() ; s++){
						succStateVars.push_back(nextStateVars[ts][successorStates[ts][states][applicableLabels[ts][states][l]][s]]);
					}
					if(succStateVars.size() > 0){
						andImpliesOr(solver, previousStateVars[ts][states], labelVars[applicableLabels[ts][states][l]], succStateVars);
					}
				}
				if(appLabelVars.size() > 0){
					impliesOr(solver, previousStateVars[ts][states], appLabelVars);
				}
				//appLabelVars.clear();
			}
		}

		swap(previousStateVars, nextStateVars);
	}

	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		vector<int> goals = fts->get_ts(ts).get_goal_states();
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
		vector<vector<int>> statesPerTimestep(currentLength+1);
		vector<int> labelPerTimestep(currentLength);

		for(size_t ts = 0 ; ts < allTimesStateVars[0].size() ; ts++){
			for(size_t state = 0 ; state < allTimesStateVars[0][ts].size() ; state++){
				if(ipasir_val(solver, allTimesStateVars[0][ts][state]) > 0){
					statesPerTimestep[0].push_back(state);
					break;
				}
			}
		}

		for(int timestep = 1 ; timestep <= currentLength ; timestep++){
			for(size_t ts = 0 ; ts < allTimesStateVars[timestep].size() ; ts++){
				for(size_t state = 0 ; state < allTimesStateVars[timestep][ts].size() ; state++){
					if(ipasir_val(solver, allTimesStateVars[timestep][ts][state]) > 0){
						statesPerTimestep[timestep].push_back(state);
						break;
					}
				}
			}
			for(size_t label = 0 ; label < allTimesLabelVars[timestep-1].size() ; label++){
				if(ipasir_val(solver, allTimesLabelVars[timestep-1][label]) > 0){
					cout << "Time " << timestep << " var = " << allTimesLabelVars[timestep-1][label] << endl;
					//cout << "Time " << timestep << " label = " << label << endl;
					labelPerTimestep[timestep-1] = label;
					break;
				}
			}
		}

		vector<int> GS = statesPerTimestep.back();
		PlanState goalState = PlanState(std::move(GS));
		vector<PlanState> states;
		for(size_t s = 0 ; s < statesPerTimestep.size() ; s++){
			states.push_back(PlanState(std::move(statesPerTimestep[s])));
		}

		vector<int> labels;
		for(size_t o = 0 ; o < labelPerTimestep.size() ; o++){
			labels.push_back(labelPerTimestep[o]);
		}

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
