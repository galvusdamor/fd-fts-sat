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
	implicationalTseitsin(opts.get<bool>("impltseitsin")),
	combineAllBDDsIntoOne(opts.get<bool>("combinebdds")),
	bddCutting(opts.get<bool>("cutbdds")),
	fts(g_main_task){

	switch (opts.get<int>("encoding")){
		case 0: do_BDD_encoding = false; break;
		case 1: do_BDD_encoding = true; break;
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

		if (combineAllBDDsIntoOne) transition_BDDs_per_factor.resize(fts->get_size());
		else transition_BDDs_per_factor_per_state_pair.resize(fts->get_size());
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
				int label = labelOrder[l];
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

						thisFactorTransitionBDD *= allPossiblePaths[s][ss];

						allTransitionsBDD += thisFactorTransitionBDD;
					}
				}

				string name = "dots/factor_" + to_string(fac) + ".dot";
				bdd_to_dot(allTransitionsBDD, name);

				transition_BDDs_per_factor[fac] = allTransitionsBDD;
			} else {
				transition_BDDs_per_factor_per_state_pair[fac] = allPossiblePaths;	
			}

			//int overallVar = bdd_to_cnf(allTransitionsBDD.getNode());
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
		
		BDD stateCube = _manager->bddOne();
		for (int i = 0; i < num_factor_vars; i++) stateCube *= _manager->bddVar(i);


		if (bddCutting){
			// fixpoint algorithm. Will break from the inside if no BDD changes.
			int round = 0;
			while (true) {	
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
								any_transition_per_factor[fac] += transition_BDDs_per_factor_per_state_pair[fac][s][ss];
							}
						}
					} else {
						// project away the state variables.
						any_transition_per_factor[fac] = transition_BDDs_per_factor[fac].ExistAbstract(stateCube);
					}
				}
	
				bool anyUpdate = false;
				// 2. Go over all pairs of factors	
				for (int facS = 0; facS < fts->get_size(); facS++){
					const task_representation::TransitionSystem & factorSource = fts->get_ts(facS);
					for (int facT = 0; facT < fts->get_size(); facT++){
						const task_representation::TransitionSystem & factorTarget = fts->get_ts(facT);

						if (facS == facT) continue;
					
						// cube for the variables that need to be abstracted away
						BDD cube = _manager->bddOne();
						cout << "Propagate from " << facS << " to " << facT << " Round: " << round << endl;
						for(int label = 0; label < fts->get_num_labels(); label++){
							task_representation::LabelID labelID (label);
							// will not be mentioned in this BDD anyway
							if (!factorSource.is_relevant_label(labelID)) continue;
							// don't project away labels that *are* relevant
							if (factorTarget.is_relevant_label(labelID)) continue;
					
							//cout << "label " << label << endl;
							cube *= _manager->bddVar(num_factor_vars + label);
						}

						// no shared variables
						if (cube == _manager->bddOne()) continue;

						//string name = "dots/bef"+to_string(facS) + "-" + to_string(facT)+".dot";
						//bdd_to_dot(any_transition_per_factor[facS], name);
						BDD relevantLabelsForTarget = any_transition_per_factor[facS].ExistAbstract(cube);
						//name = "dots/aft"+to_string(facS) + "-" + to_string(facT)+".dot";
						//bdd_to_dot(relevantLabelsForTarget, name);

						// if the relevant BDD is 1, then there is nothing to propagate.
						if (relevantLabelsForTarget == _manager->bddOne()) continue;
						// actually propagate
						if (!combineAllBDDsIntoOne){
							for (int s = 0; s < factorTarget.get_size(); s++){
								for (int ss = 0; ss < factorTarget.get_size(); ss++){
									BDD old = transition_BDDs_per_factor_per_state_pair[facT][s][ss];
									transition_BDDs_per_factor_per_state_pair[facT][s][ss] *= relevantLabelsForTarget;
									if (old != transition_BDDs_per_factor_per_state_pair[facT][s][ss])
										anyUpdate = true;
								}
							}
						
						} else {
							BDD old = transition_BDDs_per_factor[facT];
							transition_BDDs_per_factor[facT] *= relevantLabelsForTarget;
							if (old != transition_BDDs_per_factor[facT])
								anyUpdate = true;
						}
					}
				}

				if (!anyUpdate) break; // did not change any BDD
			}
			//exit(0);
		}

	}


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

vector<int> SATSearch::generateLabelVars(__attribute__((unused)) void* solver, sat_capsule & capsule/* , int timestep */){
	vector<int> labelVars(fts->get_num_labels());
	for(int label = 0 ; label < fts->get_num_labels() ; label++){
		int labelVar = capsule.new_variable();
		labelVars[label] = labelVar;
	}
	//atMostOne(solver, capsule, labelVars);
	//atLeastOne(solver, capsule, labelVars);
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

		if (!do_BDD_encoding){
			auxVars = generateAuxVars(capsule);
			transitionVars = generateTransitionVars(solver, capsule/* , timestep */);
			allTimesTransitionVars.push_back(transitionVars);
			
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
					impliesOr(solver, labelVars[label], labelTransitionSATVars);
					for(size_t ltsv = 0 ; ltsv < labelTransitionSATVars.size() ; ltsv++){
						implies(solver, labelTransitionSATVars[ltsv], labelVars[label]);
					}

				}

				for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
					for(pair<Transition, int> transition : transitionVars[ts][0]){
						if(transition.first.target != states && transition.first.src != transition.first.target){
							implies(solver, transition.second, auxVars[ts][states][0]);
						}
					}
					for(int label = 1 ; label < fts->get_num_labels()-1 ; label++){
						vector<int> supportingTransitions;
						supportingTransitions.push_back(auxVars[ts][states][label]);
						for(pair<Transition, int> transition : transitionVars[ts][label]){
							if(transition.first.target != states && transition.first.src != transition.first.target){
								implies(solver, transition.second, auxVars[ts][states][label]);
							}
							if(transition.first.target == states && transition.first.src != transition.first.target){
								supportingTransitions.push_back(transition.second);
							}
							if(transition.first.src == states){
								impliesNot(solver, auxVars[ts][states][label-1], transition.second);
							}
						}
						impliesOr(solver, auxVars[ts][states][label-1], supportingTransitions);
					}
					for(pair<Transition, int> transition : transitionVars[ts][fts->get_num_labels()]){
						if(transition.first.src == states){
							impliesNot(solver, auxVars[ts][states][fts->get_num_labels()-1], transition.second);
						}
					}
				}
			}
		} else {
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

		//fts->get_ts(ts).dump_dot_graph();
	}


	//assertYes(solver,-allTimesLabelVars[0][0]);
	//assertYes(solver,-allTimesLabelVars[0][1]);
	//assertYes(solver,-allTimesLabelVars[0][2]);
	//assertYes(solver,allTimesLabelVars[0][3]);
	//assertYes(solver,-allTimesLabelVars[0][4]);
	//assertYes(solver,-allTimesLabelVars[0][5]);
	//assertYes(solver,-allTimesLabelVars[0][6]);
	//assertYes(solver,allTimesLabelVars[0][7]);
	//assertYes(solver,-allTimesStateVars[1][0][0]);
	//assertYes(solver,-allTimesStateVars[1][0][1]);
	//assertYes(solver,allTimesStateVars[1][0][2]);
	//assertYes(solver,-allTimesStateVars[1][0][3]);
	//assertYes(solver,-allTimesStateVars[1][0][4]);
	//assertYes(solver,allTimesStateVars[1][1][3]);
	//assertYes(solver,allTimesStateVars[1][2][3]);
	//assertYes(solver,allTimesStateVars[1][3][3]);
	////implies(solver,2,3);	



	cout << "Formula has " << get_number_of_clauses() << " clauses and " << capsule.number_of_variables << " variables." << endl;
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
			cout << "Time " << timestep << endl;
			vector<int> selectedLabels;
			for(size_t label = 0 ; label < allTimesLabelVars[timestep-1].size() ; label++){
				stateReconstructor.clear();
				if(ipasir_val(solver, allTimesLabelVars[timestep-1][label]) <= 0){
					continue;
				}else{
					selectedLabels.push_back(label);
					cout << "Label : " << label << endl;
					if (!do_BDD_encoding){
						for(int ts = 0 ; ts < fts->get_size() ; ts++){
							for(pair<Transition, int> transition : allTimesTransitionVars[timestep-1][ts][label]){
								if(ipasir_val(solver, transition.second) > 0){
									stateReconstructor.push_back(transition.first.target);
									continue;
								}
							}
						}
						statesPerTimestep.push_back(stateReconstructor);
					}
				}
			}
			labelsPerTimestep.push_back(selectedLabels);
			 stateReconstructor.clear();
			for(size_t ts = 0 ; ts < allTimesStateVars[timestep].size() ; ts++){
				for(size_t state = 0 ; state < allTimesStateVars[timestep][ts].size() ; state++){
					if(ipasir_val(solver, allTimesStateVars[timestep][ts][state]) > 0){
						stateReconstructor.push_back(state);
						break;
					}
				}
			}
			statesPerTimestep.push_back(stateReconstructor);
			
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
