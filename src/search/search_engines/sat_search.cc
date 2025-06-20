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
	bddEncodingSizeLimit(opts.get<int>("bdd_size_limit")),
	implicationalTseitsin(opts.get<bool>("impltseitsin")),
	combineAllBDDsIntoOne(opts.get<bool>("combinebdds")),
	bddCutting(opts.get<bool>("cutbdds")),
	fts(g_main_task){

	switch (opts.get<int>("encoding")){
		case 0: do_BDD_encoding = false; do_R2_encoding = false; break;
		case 1: do_BDD_encoding = false; do_R2_encoding = true; no_selfloop_SATvars = false; break;
		case 2: do_BDD_encoding = false; do_R2_encoding = true; no_selfloop_SATvars = true; break;
		case 3: do_BDD_encoding = true; considerOnlyOneStepTransitions = false; break;
		case 4: do_BDD_encoding = true; considerOnlyOneStepTransitions = true; break;
	}
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
	  var_names[num_factor_vars + l] = "label_" + to_string(labelOrder[l]);

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


int SATSearch::givevar(int bddvar, vector<int> & factorVars, std::vector<int> & labelVars, vector<int> & nextFactorVars){
	if (bddvar >= num_factor_vars) return labelVars[bddvar - num_factor_vars];
	if (bddvar < num_factor_vars / 2) {
		assert(factorVars.size() > size_t(bddvar));
		return factorVars[bddvar];
	}
	assert(int(factorVars.size()) > bddvar - num_factor_vars / 2);
	return nextFactorVars[bddvar - num_factor_vars / 2];
}


int SATSearch::bdd_to_cnf(DdNode * node, vector<int> & factorVars, std::vector<int> & labelVars, vector<int> & nextFactorVars, void* solver, sat_capsule & capsule){
	
	// for lookup
	DdNode * lookup;
	if (implicationalTseitsin) lookup = node;
	else lookup = Cudd_Regular(node);

	if (tseitsinVars.count(lookup)){
		int myVar = tseitsinVars[lookup];
		if (!implicationalTseitsin && Cudd_IsComplement(node)) myVar *= -1;
		return myVar;
	}
	int thisVar = capsule.new_variable();
	DEBUG(capsule.registerVariable(thisVar, "BDD_eval_var_" + to_string(tseitsinVars.size())));
	tseitsinVars[lookup] = thisVar;

	assert(!Cudd_IsConstant(node));

	// branching node
	int var_to_branch = givevar(Cudd_NodeReadIndex(node), factorVars, labelVars, nextFactorVars);
    DdNode* true_branch = Cudd_T(node);
    DdNode* false_branch = Cudd_E(node);
	//cout << "Rec: " << node << " " << var_to_branch << "T " << true_branch << " F " << false_branch << endl;
	if (implicationalTseitsin && Cudd_IsComplement(node)) {
		true_branch = Cudd_Not(true_branch);
		false_branch = Cudd_Not(false_branch);
	}

	vector<pair<int,DdNode*>> successors {{var_to_branch, true_branch}, {-var_to_branch, false_branch}};

	for (const auto & succ : successors){ // TODO structure binding once FD compiles with C++17
		int condition_var = succ.first;
		DdNode* branch = succ.second;
		if (Cudd_IsConstant(branch)){
			bool isTrue = !Cudd_IsComplement(branch);
			//if (implicationalTseitsin) isTrue = !(negationStatus != Cudd_IsComplement(branch));  // read != as XOR
			//else isTrue = ;

			if (isTrue){
				//cout << "V" << condition_var << " <-> " << "T" << thisVar << endl;
				
				implies(solver,condition_var, thisVar);
			} else {
				//cout << "V" << condition_var << " <-> " << "T" << -thisVar << endl;
				implies(solver,condition_var,-thisVar);
			}
		} else {
			int branchvar = bdd_to_cnf(branch, factorVars, labelVars, nextFactorVars, solver, capsule);
			//cout << "V" << condition_var << " & " << "T" << thisVar << " -> " << "T" << branchvar << endl;
			andImplies(solver, condition_var, thisVar, branchvar);
			if (!implicationalTseitsin){
				// in implicational Tseitin mode, we only care about the true outcome -- every Tseitin var will be forced to true anyway.
				// Then we don't need to implication in the backwards direction.
				//cout << "V" << var_to_branch << " & " << "T-" << thisVar << " -> " << "T-" << branchvar << endl;
				andImplies(solver, condition_var, -thisVar, -branchvar);
			}
		}
	}

	if (!implicationalTseitsin && Cudd_IsComplement(node)) return -thisVar;
	return thisVar;
}

bool SATSearch::isIrrelevantLabel(int ts, int label){
	auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
	if((int)transitions.size() != fts->get_ts(ts).get_size()) return false;
	for(size_t t = 0 ; t < transitions.size() ; t++){
		if(transitions[t].src != transitions[t].target){
			return false;
		}
	}
	return true;
}

bool isSelfLoop(Transition t){
	return t.src == t.target;
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

/* map<int, map<int, vector<vector<int>>>> SATSearch::buildTable(){
	map<int, map<int, vector<vector<int>>>> tables;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		vector<vector<int>> table(fts->get_ts(ts).get_size(), vector<int>(fts->get_ts(ts).get_size(), 0));
		for(int label = 0 ; label < fts->get_num_labels() ; label++){
			if(isIrrelevantLabel(ts, label)) continue;
			auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
			for(Transition t : transitions){
				table[t.src][t.target] = 1;
			}
		}
		tables[ts][label] = table;
	}
	return tables;
} */

void SATSearch::checkSolution(vector<vector<vector<int>>> &allTimesStateVars, vector<vector<int>> &allTimesLabelVars, vector<map<int, map<int, vector<pair<Transition, int>>>>> &allTimesTransitionVars, 
					int length, void* solver){
	if (do_BDD_encoding || !do_R2_encoding) return; // TODO needs to be implemented still
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



void exitOutOfMemory(size_t) {
    cerr << "Memory exceeded within BDD operation" << endl;
    utils::exit_with(utils::ExitCode::OUT_OF_MEMORY);
}






void SATSearch::initialize() {
	cout << "Initialising" << endl;
	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

	labelOrder.resize(fts->get_num_labels());
	// for now just the natural ordering
	for(int l = 0; l < fts->get_num_labels(); l++) labelOrder[l] = l;
 
	if (do_BDD_encoding){
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
		_manager->RegisterOutOfMemoryCallback(exitOutOfMemory);

		if (combineAllBDDsIntoOne) transition_BDDs_per_factor.resize(fts->get_size());
		else {
			if (!considerOnlyOneStepTransitions)
				transition_BDDs_per_factor_per_state_pair.resize(fts->get_size());
			if (considerOnlyOneStepTransitions || bddEncodingSizeLimit != -1)
				one_step_transition_BDDs_per_factor_per_state_pair.resize(fts->get_size());
		}
		for (int fac = 0; fac < fts->get_size(); fac++){
			const task_representation::TransitionSystem & factor = fts->get_ts(fac);

			// we compute a matrix of size |S|^2 that contains all-pair-possible-paths
			vector<vector<BDD>> allPossiblePaths (factor.get_size());
			int num_relevant_labels = 0;

			// pre-compute the full reachability 
			if (!considerOnlyOneStepTransitions){
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
					int label = labelOrder[l];
					task_representation::LabelID labelID (label);
					//cout << "Processing label " << label << "." << endl;
					if (!factor.is_relevant_label(labelID) && factor.is_selfloop_everywhere(labelID)){
						//cout << "\tLabel is not relevant for factor. Skipping." << endl;
						continue;
					}
					num_relevant_labels++;
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
			}

			// compute the BDDs expressing single transitions + possible loops before and after
			if (considerOnlyOneStepTransitions || bddEncodingSizeLimit != -1){
				one_step_transition_BDDs_per_factor_per_state_pair[fac].resize(factor.get_size());
				for (int s = 0; s < factor.get_size(); s++){
					one_step_transition_BDDs_per_factor_per_state_pair[fac][s].resize(factor.get_size());
					for (int ss = 0; ss < factor.get_size(); ss++){
						one_step_transition_BDDs_per_factor_per_state_pair[fac][s][ss] = _manager->bddZero();
					}
				}
				for(int l = 0; l < fts->get_num_labels(); l++){
					for (const auto & transition : factor.get_transitions_with_label(l)){
						BDD thisTrans = _manager->bddOne();
						bool beforeLabelL = true;
						for(int ll = 0; ll < fts->get_num_labels(); ll++){
							int label = labelOrder[ll];
							task_representation::LabelID labelID (label);
							if (l == label){
								beforeLabelL = false;
								thisTrans *= _manager->bddVar(label + num_factor_vars);
							} else {
								int loopState;
								if (beforeLabelL) loopState = transition.src; else loopState = transition.target;
								
								bool foundLoop = false;
								for (const auto & transition : factor.get_transitions_with_label(label)){
									if (transition.src == loopState && transition.target == loopState){
										foundLoop = true;
										break;
									}
								}
								if (!foundLoop)
									thisTrans *= ~_manager->bddVar(label + num_factor_vars);
							}
						}
						one_step_transition_BDDs_per_factor_per_state_pair[fac][transition.src][transition.target] += thisTrans;
					}
				}
			}



			cout << "Precomputation for factor Nr " << fac << " with " << factor.get_size() << " states. ";
			if (num_relevant_labels) cout << num_relevant_labels << " of " << fts->get_num_labels() << " labels relevant.";
			cout << endl;

			if (combineAllBDDsIntoOne){
				// compute the union BDD that describes all transitions at the same time.
				BDD allTransitionsBDD = _manager->bddZero();
				for (int s = 0; s < factor.get_size(); s++){
					for (int ss = 0; ss < factor.get_size(); ss++){
						BDD thisFactorTransitionBDD = _manager->bddVar(s) * _manager->bddVar(num_factor_vars/2 + ss);
						for (int nots = 0; nots < factor.get_size(); nots++)
							if (s != nots) thisFactorTransitionBDD *= ~_manager->bddVar(nots);
						for (int notss = 0; notss < factor.get_size(); notss++)
							if (ss != notss) thisFactorTransitionBDD *= ~_manager->bddVar(num_factor_vars/2 + notss);

						if (considerOnlyOneStepTransitions)
							thisFactorTransitionBDD *= one_step_transition_BDDs_per_factor_per_state_pair[fac][s][ss];
						else
							thisFactorTransitionBDD *= allPossiblePaths[s][ss];

						allTransitionsBDD += thisFactorTransitionBDD;
					}
				}

				//string name = "dots/factor_" + to_string(fac) + ".dot";
				//bdd_to_dot(allTransitionsBDD, name);

				transition_BDDs_per_factor[fac] = allTransitionsBDD;
			} else {
				if (!considerOnlyOneStepTransitions)
					transition_BDDs_per_factor_per_state_pair[fac] = allPossiblePaths;	
			
				int summedSizeBefore = 0, summedSizeAfter = 0, possibleSingleTrans = 0, allTrans = 0;

				
				map<int,vector<pair<int,int>>> bdd_sizes;

				// printing
				for (int s = 0; s < factor.get_size(); s++){
					for (int ss = 0; ss < factor.get_size(); ss++){
						if (!considerOnlyOneStepTransitions){
							if (transition_BDDs_per_factor_per_state_pair[fac][s][ss] != _manager->bddZero()){
								allTrans++;

								int thisBDDsize = transition_BDDs_per_factor_per_state_pair[fac][s][ss].nodeCount();
								summedSizeBefore += thisBDDsize;
								bdd_sizes[thisBDDsize].push_back({s,ss});
							}
						}
					
						if (considerOnlyOneStepTransitions || bddEncodingSizeLimit != -1)
							if (one_step_transition_BDDs_per_factor_per_state_pair[fac][s][ss] != _manager->bddZero())
								possibleSingleTrans++;
						
						//cout << "Factor"  << fac << " " << s << " " << ss << " state: " <<  allPossiblePaths[s][ss].nodeCount() << endl;
						
						//string name = "dots/factor_" + to_string(fac) + "_" + to_string(s) + "_to_" + to_string(ss) + ".dot";
						//bdd_to_dot(allPossiblePaths[s][ss], name);
						
						//name = "dots/factor_" + to_string(fac) + "_one_" + to_string(s) + "_to_" + to_string(ss) + ".dot";
						//bdd_to_dot(one_step_transition_BDDs_per_factor_per_state_pair[fac][s][ss], name);
					}
				}

				if (bddEncodingSizeLimit != -1){
					int size_up_to_now = 0;
					for (const auto & thisSizeBDDs : bdd_sizes){
						for (const pair<int,int> & s_ss : thisSizeBDDs.second){
							// if overall BDD-size would be larger than the limit,
							// replace it by the one that only allows for a single action in this factor
							const int & s = s_ss.first;
							const int & ss = s_ss.second;
							if (size_up_to_now + thisSizeBDDs.first > bddEncodingSizeLimit){
								transition_BDDs_per_factor_per_state_pair[fac][s][ss] = 
									one_step_transition_BDDs_per_factor_per_state_pair[fac][s][ss];
							} else 
								size_up_to_now += thisSizeBDDs.first;
						
							summedSizeAfter += transition_BDDs_per_factor_per_state_pair[fac][s][ss].nodeCount();
						}
					}
				}
				cout << "Factor Overall: before limiting " << summedSizeBefore << " after limiting " << summedSizeAfter  << " all transitions: " << allTrans << " possible 1-step transitions: " << possibleSingleTrans <<  endl;
			}

			//int overallVar = bdd_to_cnf(allTransitionsBDD.getNode());
			//cout << "Overall :" << "T" << overallVar << endl;
		}
			//exit(0);
		
		BDD stateCube = _manager->bddOne();
		for (int i = 0; i < num_factor_vars; i++) stateCube *= _manager->bddVar(i);


		if (bddCutting){
			// fixpoint algorithm. Will break from the inside if no BDD changes.
			int round = 0;
			bool anyUpdate = true;
			while (anyUpdate) {	
				cout << "Propagation Round " << round;
				anyUpdate = false;
				round++;
				// 1. We need to prepare the data structures for cutting
				// we need a BDD for every factor that describes any legal transition in that factor
				vector<BDD> any_transition_per_factor(fts->get_size());
				for (int fac = 0; fac < fts->get_size(); fac++){
					const task_representation::TransitionSystem & factor = fts->get_ts(fac);
					if (!combineAllBDDsIntoOne){
						any_transition_per_factor[fac] = _manager->bddZero();
						for (int s = 0; s < factor.get_size(); s++){
							for (int ss = 0; ss < factor.get_size(); ss++){
								if (considerOnlyOneStepTransitions)
									any_transition_per_factor[fac] += one_step_transition_BDDs_per_factor_per_state_pair[fac][s][ss];
								else
									any_transition_per_factor[fac] += transition_BDDs_per_factor_per_state_pair[fac][s][ss];
							}
						}
					} else {
						// project away the state variables.
						any_transition_per_factor[fac] = transition_BDDs_per_factor[fac].ExistAbstract(stateCube);
					}
				}
	
				// 2. Go over all pairs of factors	
				for (int facS = 0; facS < fts->get_size(); facS++){
					const task_representation::TransitionSystem & factorSource = fts->get_ts(facS);
					for (int facT = 0; facT < fts->get_size(); facT++){
						const task_representation::TransitionSystem & factorTarget = fts->get_ts(facT);

						if (facS == facT) continue;
					
						// cube for the variables that need to be abstracted away
						BDD cube = _manager->bddOne();
						//cout << "Propagate from " << facS << " to " << facT << " Round: " << round << endl;
						bool foundRemainingVariable = false;
						for(int label = 0; label < fts->get_num_labels(); label++){
							task_representation::LabelID labelID (label);
							// will not be mentioned in this BDD anyway
							if (!factorSource.is_relevant_label(labelID) && factorSource.is_selfloop_everywhere(labelID)) continue;
							// don't project away labels that *are* relevant
							if (factorTarget.is_relevant_label(labelID) || !factorTarget.is_selfloop_everywhere(labelID)) {
								foundRemainingVariable = true;
								continue;
							}
					
							//cout << "label " << label << endl;
							cube *= _manager->bddVar(num_factor_vars + label);
						}

						// no shared variables
						if (!foundRemainingVariable) continue;

						//string name = "dots/bef"+to_string(facS) + "-" + to_string(facT)+".dot";
						//bdd_to_dot(any_transition_per_factor[facS], name);
						BDD constraintsOverLabelsRelevantForTarget = any_transition_per_factor[facS].ExistAbstract(cube);
						//name = "dots/aft"+to_string(facS) + "-" + to_string(facT)+".dot";
						//bdd_to_dot(constraintsOverLabelsRelevantForTarget, name);

						// if the relevant BDD is 1, then there is nothing to propagate.
						if (constraintsOverLabelsRelevantForTarget == _manager->bddOne()) continue;
						// actually propagate
						if (!combineAllBDDsIntoOne){
							for (int s = 0; s < factorTarget.get_size(); s++){
								for (int ss = 0; ss < factorTarget.get_size(); ss++){
									// reference to access
									BDD & currentMemory = (considerOnlyOneStepTransitions)?
									   one_step_transition_BDDs_per_factor_per_state_pair[facT][s][ss]:
										transition_BDDs_per_factor_per_state_pair[facT][s][ss];
									// copy to compare
									BDD old = currentMemory;
									currentMemory *= constraintsOverLabelsRelevantForTarget;
									if (old != currentMemory) anyUpdate = true;
								}
							}
						
						} else {
							BDD old = transition_BDDs_per_factor[facT];
							transition_BDDs_per_factor[facT] *= constraintsOverLabelsRelevantForTarget;
							if (old != transition_BDDs_per_factor[facT])
								anyUpdate = true;
						}
					}
				}
				cout << " completed with " << (anyUpdate?"some reduction. Continuing": "no reduction. Reached fixpoint.") << endl;
			}
			//exit(0);
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
	if(!do_R2_encoding && !do_BDD_encoding){
		atMostOne(solver, capsule, labelVars);
	}
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

	// empty for BDD-based encoding
	map<int, map<int, vector<pair<Transition, int>>>> transitionVars;
	vector<int> labelVars;
	vector<vector<int>> nextStateVars;
	// empty for BDD-based encoding
	map<int, map<int, vector<int>>> auxVars;
	for(int timestep = 1 ; timestep <= currentLength ; timestep++){
		labelVars = generateLabelVars(solver, capsule/* , int timestep */);
		allTimesLabelVars.push_back(labelVars);
		nextStateVars = generateStateVars(solver, capsule/* , timestep */);
		allTimesStateVars.push_back(nextStateVars);

		if (do_BDD_encoding){
			// BDD-based encoding
			for(int fac = 0 ; fac < fts->get_size() ; fac++){
				if (combineAllBDDsIntoOne){
					tseitsinVars.clear();
					int transitionVar = bdd_to_cnf(transition_BDDs_per_factor[fac].getNode(), previousStateVars[fac], labelVars, nextStateVars[fac], solver, capsule);

					assertYes(solver,transitionVar);
				} else {
					const task_representation::TransitionSystem & factor = fts->get_ts(fac);
					for (int s = 0; s < factor.get_size(); s++){
						for (int ss = 0; ss < factor.get_size(); ss++){
							// edge case: it can happen that this transition is impossible under the chosen order
							if (transition_BDDs_per_factor_per_state_pair[fac][s][ss] == _manager->bddZero()){
								impliesNot(solver,previousStateVars[fac][s], nextStateVars[fac][ss]);
								continue;
							}
							if (transition_BDDs_per_factor_per_state_pair[fac][s][ss] == _manager->bddOne()){
								// Nothing to encode, this transition is always allowed
								continue;
							}
							

							tseitsinVars.clear();
							// Providing the previous and next state here is useless -- they will not be accessed anyway.
							// But the function API requires them.
							int transitionVar = bdd_to_cnf(transition_BDDs_per_factor_per_state_pair[fac][s][ss].getNode(), previousStateVars[fac], labelVars, nextStateVars[fac], solver, capsule);

							andImplies(solver,previousStateVars[fac][s], nextStateVars[fac][ss], transitionVar);
						}
					}
				}
			}
		}else if(do_R2_encoding && no_selfloop_SATvars){
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
							labelTransitionSATVars.push_back(transition.second);
						}
						vector<int> impliesOrPrec;
						impliesOrPrec.push_back(previousStateVars[ts][transition.first.src]);
						for(size_t relevantLabelPrec = 0 ; relevantLabelPrec < relevantLabel ; relevantLabelPrec++){
							for(pair<Transition, int> transition_prec : transitionVars[ts][relevantLabels[ts][relevantLabelPrec]]){
								if(transition_prec.first.target == transition.first.src && !isSelfLoop(transition_prec.first)){
									impliesOrPrec.push_back(transition_prec.second);
								}
							}
						}
						if(isSelfLoop(transition.first)){
							precsForSelfLoops.insert(precsForSelfLoops.end(), impliesOrPrec.begin(), impliesOrPrec.end());
							continue;
						}

						otherTransitionsInLabelWithSelfLoop.push_back(transition.second);

						impliesOr(solver, transition.second, impliesOrPrec);

						vector<int> impliesOrEff;
						impliesOrEff.push_back(nextStateVars[ts][transition.first.target]);
						for(size_t relevantLabelEff = relevantLabel+1 ; relevantLabelEff < relevantLabels[ts].size() ; relevantLabelEff++){
							for(pair<Transition, int> transition_eff : transitionVars[ts][relevantLabels[ts][relevantLabelEff]]){
								if(transition_eff.first.target != transition.first.target && !isSelfLoop(transition_eff.first)){
									impliesOrEff.push_back(transition_eff.second);
								}
							}
						}
						impliesOr(solver, transition.second, impliesOrEff);
					}

					if(labelTransitionSATVars.size() > 0){
						if(!containsSelfLoops(ts, relevantLabels[ts][relevantLabel])){
							impliesOr(solver, labelVars[relevantLabels[ts][relevantLabel]], labelTransitionSATVars);
						}
						for(size_t ltsv = 0 ; ltsv < labelTransitionSATVars.size() ; ltsv++){
							implies(solver, labelTransitionSATVars[ltsv], labelVars[relevantLabels[ts][relevantLabel]]);
						}
					}
					if(precsForSelfLoops.size() > 0){
						precsForSelfLoops.insert(precsForSelfLoops.end(), otherTransitionsInLabelWithSelfLoop.begin(), otherTransitionsInLabelWithSelfLoop.end());
						impliesOr(solver, labelVars[relevantLabels[ts][relevantLabel]], precsForSelfLoops);
					}
				}

				for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
					set<int> negatedRelevantLabels;
					negatedRelevantLabels.insert(previousStateVars[ts][states]);
					for(size_t relevantLabel = 0 ; relevantLabel < relevantLabels[ts].size() ; relevantLabel++){
						if(!isAlwaysSelfLoop(ts, relevantLabels[ts][relevantLabel])){//Should I also add a condition for labels which are not irrelevant but always self loops??????? YES!!!!
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
		}else{
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
				}
			}
		}
		swap(previousStateVars, nextStateVars);

		cout << "Constructed time " << timestep << " of " << currentLength << ". Now " << get_number_of_clauses() << " clauses and " << capsule.number_of_variables << " variables." << endl;
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
	int solverState = ipasir_solve(solver);
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
	
		set<int> timesteps_with_labels;	

		for(int timestep = 1 ; timestep <= currentLength ; timestep++){
			cout << "Time " << timestep << endl;
			vector<int> selectedLabels;
			for(size_t label = 0 ; label < allTimesLabelVars[timestep-1].size() ; label++){
				stateReconstructor.clear();
				if(ipasir_val(solver, allTimesLabelVars[timestep-1][label]) <= 0){
					continue;
				}else{
					selectedLabels.push_back(label);
					timesteps_with_labels.insert(timestep);
					cout << "Label : " << label << endl;
					if (!do_BDD_encoding){
						for(int ts = 0 ; ts < fts->get_size() ; ts++){
							if(do_R2_encoding){
								if(allTimesTransitionVars[timestep-1][ts][label].size() == 0){
									stateReconstructor.push_back(statesPerTimestep.back()[ts]);
								}else{
									bool addedState = false;
									for(pair<Transition, int> transition : allTimesTransitionVars[timestep-1][ts][label]){
										if(ipasir_val(solver, transition.second) > 0){
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
						statesPerTimestep.push_back(stateReconstructor);
					}
				}
			}
			labelsPerTimestep.push_back(selectedLabels);
			// For the BDD-based encoding, we need to reconstruct the plan via search (labels are non-deterministic)
			// What we have: the labels to be applied in which order (in selectedLabels) and the previous and next overall state
			// We know how many intermediate state there are *and*
			// that the determination of the intermediate states is independent between all factors.
			// So we can reconstruct the visited states per factor
			if (do_BDD_encoding && selectedLabels.size()){ // if we don't execute any label, we don't have to extract a new state.
				
				vector<vector<int>> reconstructedStates(selectedLabels.size() + 1);
				for (size_t i = 1; i < selectedLabels.size(); i++) reconstructedStates[i].resize(fts->get_size());
				reconstructedStates[0] = statesPerTimestep.back();
				
				for(size_t ts = 0 ; ts < allTimesStateVars[timestep].size() ; ts++){
					for(size_t state = 0 ; state < allTimesStateVars[timestep][ts].size() ; state++){
						if(ipasir_val(solver, allTimesStateVars[timestep][ts][state]) > 0){
							reconstructedStates[selectedLabels.size()].push_back(state);
							break;
						}
					}
				}
				for (int fac = 0; fac < fts->get_size(); fac++){
					std::set<std::pair<int,int>> visited;
					//cout << "Factor " << fac << " from " << reconstructedStates[0][fac] << " to " << reconstructedStates.back()[fac] << endl;
					bool reconstruction_successful = bdd_state_reconstruction_dfs(fac,reconstructedStates,0,selectedLabels,visited);
					if (!reconstruction_successful) cout << "Reconstruction failed on factor " << fac << "." << endl;
					assert(reconstruction_successful);
				}
				// add the reconstructed states to the list of states
				for (size_t i = 1; i <= selectedLabels.size(); i++)
					statesPerTimestep.push_back(reconstructedStates[i]);
			}
			
		}

		vector<int> GS = statesPerTimestep.back();
		for(auto x : statesPerTimestep){
			cout << x.size() << ":";
			for (size_t f = 0; f < x.size() ; f++)
				cout << " " << f << "=" << x[f];
			cout << endl;
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

		cout << "Total states: " << states.size() << endl;
		cout << "Total labels: " << labels.size() << endl;
		cout << "Total timesteps with label: " << timesteps_with_labels.size() << endl;

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
	

bool SATSearch::bdd_state_reconstruction_dfs(int fac, std::vector<std::vector<int>> & reconstructedStates, int depth, std::vector<int> & plan, std::set<std::pair<int,int>> & visited){
	const task_representation::TransitionSystem & factor = fts->get_ts(fac);

	int currentState = reconstructedStates[depth][fac];

	// if already visited it will be unsuccessful.
	if (visited.count({currentState, depth})) return false;
	visited.insert({currentState, depth});

	// try to walk one step further
	for (const auto & transition : factor.get_transitions_with_label(plan[depth])){
		if (transition.src != currentState) continue;
		if (depth == int(plan.size()) - 1){
			// this is the last label to we need to have reached the goal state
			if (transition.target == reconstructedStates[depth+1][fac]){
				return true;
			} else continue; // cannot use this transition
		}

		reconstructedStates[depth + 1][fac] = transition.target;
		if (bdd_state_reconstruction_dfs(fac,reconstructedStates,depth+1,plan,visited))
			return true;
	}
	return false;	
}


void SATSearch::print_statistics() const{
	statistics.print_detailed_statistics();
}

};
