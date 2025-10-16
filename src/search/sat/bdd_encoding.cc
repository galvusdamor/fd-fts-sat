#include <chrono>
#include <thread>
#include <ctime>
#include <atomic>

#include "bdd_encoding.h"

// #include "../plugins/options.h"
#include "../utils/logging.h"
#include "../utils/timer.h"
#include "ipasir.h"
#include "../task_utils/label_order_finder.h"

using namespace std;
using namespace task_representation;


namespace sat_search {
BDDSATEncoding::BDDSATEncoding(const Options &opts,std::shared_ptr<task_representation::FTSTask> main_task): //SearchEngine(opts),
	bddEncodingSizeLimit(opts.get<int>("bdd_size_limit")),
	implicationalTseitsin(opts.get<bool>("impltseitsin")),
	omitForcedVariables(opts.get<bool>("omitforcedvariables")),
	forcedVariablesThreshold(opts.get<int>("forcedvariablesthreshold")),
	combineAllBDDsIntoOne(opts.get<bool>("combinebdds")),
	bddCutting(opts.get<bool>("cutbdds")),
	bddCovering(opts.get<bool>("coverbdds")),
	fts(main_task) {
}


struct BDDError {};


// promise from symbolic
void
exceptionError(string /*message*/) {
    //cout << message << endl;
    throw BDDError();
}


void BDDSATEncoding::bdd_to_dot(const BDD &bdd, const std::string &file_name) const {
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


// TODO Get rid of global variables!
map<DdNode *, int> tseitsinVars;
map<DdNode *, int> node_indegree;


int BDDSATEncoding::givevar(int bddvar, vector<int> & factorVars, std::vector<int> & labelVars, vector<int> & nextFactorVars){
	if (bddvar >= num_factor_vars) return labelVars[bddvar - num_factor_vars];
	if (bddvar < num_factor_vars / 2) {
		assert(factorVars.size() > size_t(bddvar));
		return factorVars[bddvar];
	}
	assert(int(factorVars.size()) > bddvar - num_factor_vars / 2);
	return nextFactorVars[bddvar - num_factor_vars / 2];
}


void BDDSATEncoding::bdd_in_degree(DdNode * node){

	// first handle edge cases
	if (Cudd_IsConstant(node)) return;

	// this node is branching
    DdNode* true_branch = Cudd_T(node);
    DdNode* false_branch = Cudd_E(node);
	if (implicationalTseitsin && Cudd_IsComplement(node)) {
		true_branch = Cudd_Not(true_branch);
		false_branch = Cudd_Not(false_branch);
	}

	// compute lookup node. For the biimplicational encoding we only keep one copy,
	// for tseitsin we need both positive and negative versions
	DdNode * lookup;
	if (implicationalTseitsin) lookup = node; else lookup = Cudd_Regular(node);

	if (node_indegree.count(lookup)){
		node_indegree[lookup]++;
		return;
	}

	node_indegree[lookup]++;
	bdd_in_degree(true_branch);
	bdd_in_degree(false_branch);
}

void BDDSATEncoding::bdd_to_cnf(DdNode * node, std::vector<int> & currentConditions, vector<int> & factorVars, std::vector<int> & labelVars, vector<int> & nextFactorVars, void* solver, sat_capsule & capsule){

	// first handle edge cases
	if (Cudd_IsConstant(node)){
		bool isTrue = !Cudd_IsComplement(node);

		if (!isTrue){
			// if conditions are true, we would end up at the false node, thus the conditions must be false
			notAll(solver, currentConditions);
		}
		// in the true case, we have nothing to do as the BDD is automatically satisfied

		return;
	}

	// this node is branching
    DdNode* true_branch = Cudd_T(node);
    DdNode* false_branch = Cudd_E(node);
	//cout << "Rec: " << node << " " << var_to_branch << "T " << true_branch << " F " << false_branch << endl;
	if (implicationalTseitsin && Cudd_IsComplement(node)) {
		true_branch = Cudd_Not(true_branch);
		false_branch = Cudd_Not(false_branch);
	}
	int var_to_branch = givevar(Cudd_NodeReadIndex(node), factorVars, labelVars, nextFactorVars);
	vector<tuple<int,DdNode*,DdNode*>> successors {{var_to_branch, true_branch, false_branch}, {-var_to_branch, false_branch, true_branch}};

	// compute lookup node. For the biimplicational encoding we only keep one copy,
	// for tseitsin we need both positive and negative versions
	DdNode * lookup;
	if (implicationalTseitsin) lookup = node; else lookup = Cudd_Regular(node);


	// forcing takes precedence over lookup.
	if (implicationalTseitsin && omitForcedVariables && node_indegree[lookup] <= forcedVariablesThreshold){
		// check if one of the branches leads to the false node
		for (const auto & [branch_var, branch, otherbranch] : successors){
			if (Cudd_IsConstant(branch) && Cudd_IsComplement(branch)){
				// this branch leads immediately to false. Thus we *must* take the other branch.
				// Thus: if conditions are true, the branch var must direct us in the other direction.
				andImplies(solver,currentConditions,-branch_var);

				// and the conditions are then propagated further down the tree
				bdd_to_cnf(otherbranch, currentConditions, factorVars, labelVars, nextFactorVars, solver, capsule);
				// this can happen for only one branch (otherwise BDD is not reduced)
				return;
			}
			if (Cudd_IsConstant(branch) && !Cudd_IsComplement(branch)){
				// this branch immediately leads to true. The other branch is only relevant if condition is false.
				// This means that the negation of the branch variable essentially becomes a new condition.
				vector<int> newConditions = currentConditions;
				newConditions.push_back(-branch_var);
				
				bdd_to_cnf(otherbranch, newConditions, factorVars, labelVars, nextFactorVars, solver, capsule);
				return;
			}
		}
	}


	// we now know that we (may) need to create a new decision variable here.
	// TODO: in theory, we could extend the condition with the branch vars,
	// but only up to a point as otherwise this will be an exponential encoding



	if (tseitsinVars.count(lookup)){
		int myVar = tseitsinVars[lookup];
		if (!implicationalTseitsin && Cudd_IsComplement(node)) myVar *= -1;
		andImplies(solver,currentConditions, myVar);
		return;
	}

	// create formula for this BDD for the first time, so we need to generate a variable representing its truth
	int thisVar = capsule.new_variable();
	DEBUG(capsule.registerVariable(thisVar, "BDD_eval_var_" + to_string(tseitsinVars.size())));
	tseitsinVars[lookup] = thisVar;

	// variable for this node becomes the new condition
	vector<int> trueVarVector = {thisVar, var_to_branch};
	vector<int> falseVarVector = {thisVar, -var_to_branch};
	
	bdd_to_cnf(true_branch, trueVarVector, factorVars, labelVars, nextFactorVars, solver, capsule);
	bdd_to_cnf(false_branch, falseVarVector, factorVars, labelVars, nextFactorVars, solver, capsule);
	if (!implicationalTseitsin){
		// if the variable for this one is false, and we take a branch, than that variable also must be false.
		trueVarVector[0] *= -1;	
		falseVarVector[0] *= -1;	
		bdd_to_cnf(Cudd_Not(true_branch), trueVarVector, factorVars, labelVars, nextFactorVars, solver, capsule);
		bdd_to_cnf(Cudd_Not(false_branch), falseVarVector, factorVars, labelVars, nextFactorVars, solver, capsule);
		if (Cudd_IsComplement(node)) thisVar *= -1;
	}
	
	andImplies(solver,currentConditions,thisVar);
}

void exitOutOfMemory(size_t) {
    cerr << "Memory exceeded within BDD operation" << endl;
    utils::exit_with(utils::ExitCode::OUT_OF_MEMORY);
}


void BDDSATEncoding::initialize() {
	utils::Timer sat_init_timer;
	cout << "Initialising" << endl;
	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

	//labelOrder = label_order_finder->find_order(*fts);

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


			// try to find a better BDD representation
			if (bddCovering){
				vector<vector<BDD>> pathsToForbit (factor.get_size());
				for (int s = 0; s < factor.get_size(); s++){
					pathsToForbit[s].resize(factor.get_size());
					for (int ss = 0; ss < factor.get_size(); ss++){
						pathsToForbit[s][ss] = !allPossiblePaths[s][ss]; 
					}
				}

				// We are trying to find smaller conditions/BDDs that express some of the constraints that are always true/false
				// The idea is that these extra constraints will provide for overall smaller BDDs and more concise constructions
				//
				// In this loop, we try to find
				//   labels that for a specific source or target state are always true or false
				// These give implications of the form
				//   state -> (+-) label
				//
				// Implications of the type
				//   label -> state
				// Can be inferred from the state -> -label implications. If for a label all implications,
				// but one are true, the label -> state implication is true 
				
				map<int, vector<int> > prev_state_implies_pos_label;
				map<int, vector<int> > prev_state_implies_neg_label;
				map<int, vector<int> > next_state_implies_pos_label;
				map<int, vector<int> > next_state_implies_neg_label;
				
				map<int, vector<int> > pos_label_implies_prev_state;
				map<int, vector<int> > pos_label_implies_next_state;
				
				for(int label = 0; label < fts->get_num_labels(); label++){
					task_representation::LabelID labelID (label);
					if (!factor.is_relevant_label(labelID) && factor.is_selfloop_everywhere(labelID)) {
						continue;
					}
					cout << "Checking label " << label << endl;

					vector<int> prev_states_implying_this_neg;
					vector<int> next_states_implying_this_neg;

					for (int mode = 0; mode < 2; mode++){
						bool m = mode == 0;
						// source
						for (int s = 0; s < factor.get_size(); s++){
							// check whether it is false
							bool isFalse = true;
							BDD testBDD = _manager->bddVar(label + num_factor_vars);
							if (!m) testBDD = !testBDD;
							for (int ss = 0; ss < factor.get_size(); ss++){
								if (testBDD * allPossiblePaths[s][ss] != _manager->bddZero()){
									isFalse = false;
									break;
								}
							}

							if (isFalse){
								cout << "Relevant label " << label << " is constantly " << (m?"false":"true") << " for source state " << s << " in factor " << fac << endl;

								if (m) {prev_state_implies_neg_label[s].push_back(label);prev_states_implying_this_neg.push_back(s);}
								else   prev_state_implies_pos_label[s].push_back(label);
							}
						}

						// target
						for (int ss = 0; ss < factor.get_size(); ss++){
							// check whether it is false
							bool isFalse = true;
							BDD testBDD = _manager->bddVar(label + num_factor_vars);
							if (!m) testBDD = !testBDD;
							for (int s = 0; s < factor.get_size(); s++){
								if (testBDD * allPossiblePaths[s][ss] != _manager->bddZero()){
									isFalse = false;
									break;
								}
							}

							if (isFalse){
								cout << "Relevant label " << label << " is constantly " << (m?"false":"true") << " for target state " << ss << " in factor " << fac << endl;
								if (m) {next_state_implies_neg_label[ss].push_back(label);next_states_implying_this_neg.push_back(ss);}
								else   next_state_implies_pos_label[ss].push_back(label);
							}
						}
					}
				

					// trying to find cases where a state implies a specific label
					// We have to have at least that all other states imply that the label is false

					// if equal this label would never be executable!!
					assert(int(prev_states_implying_this_neg.size()) != factor.get_size());

					// if all but one state implies that the label is not there, then the label implies the remaining state
					if (int(prev_states_implying_this_neg.size()) + 1 == factor.get_size()){
						// find missing state. We have inserted them in order
						bool found = false;
						for (int i = 0; i < int(prev_state_implies_neg_label.size()); i++){
							if (i != prev_states_implying_this_neg[i]){
								pos_label_implies_prev_state[label].push_back(i);
								cout << "Source state " << i << " in factor " << fac << " implies label " << label << endl;
								found = true;
								break;
							}
						}
						// then it must be the last state
						if (! found){
							pos_label_implies_prev_state[label].push_back(factor.get_size()-1);
							cout << "Source state " << factor.get_size()-1 << " in factor " << fac << " implies label " << label << endl;
						}
					}
					if (int(next_states_implying_this_neg.size()) + 1 == factor.get_size()){
						// find missing state. We have inserted them in order
						bool found = false;
						for (int i = 0; i < int(next_states_implying_this_neg.size()); i++){
							if (i != next_states_implying_this_neg[i]){
								pos_label_implies_next_state[label].push_back(i);
								cout << "Target state " << i << " in factor " << fac << " implies label " << label << endl;
								found = true;
								break;
							}
						}
						// then it must be the last state
						if (! found){
							pos_label_implies_next_state[label].push_back(factor.get_size()-1);
							cout << "Target state " << factor.get_size()-1 << " in factor " << fac << " implies label " << label << endl;
						}
					}



					// cross-implication between labels
					for(int otherLabel = label+1; otherLabel < fts->get_num_labels(); otherLabel++){
						break;
						task_representation::LabelID otherLabelID (otherLabel);
						if (!factor.is_relevant_label(otherLabelID) && factor.is_selfloop_everywhere(otherLabelID)) {
							continue;
						}

						// check whether it is possible for these two to appear together in any transition
						BDD testBDD = _manager->bddVar(label + num_factor_vars) * _manager->bddVar(otherLabel + num_factor_vars);
						bool isFalse = true;
						for (int s = 0; s < factor.get_size(); s++){
							for (int ss = 0; ss < factor.get_size(); ss++){
								if (testBDD * allPossiblePaths[s][ss] != _manager->bddZero()){
									isFalse = false;
									break;
								}
							}
							if (isFalse == false) break;
						}

						if (isFalse){
							cout << "In Factor " << fac << " labels " << label << " and " << otherLabel << " cannot appear together." << endl;
						}
					}
				}


				// 2. Step: now we try to simplify the overall BDDs
			
				for (const auto & [s,forbiddenLabels] : prev_state_implies_neg_label){
					// for the BDDs starting at state s, we don't have to assert any more that the forbidden labels
					// are actually false.
					//
					// Effectively, they represent the fact that all states in which "forbidden label" appears, are forbidden.
					// To we can remove these states from the set of states to be forbidden
					for (const int & forbiddenLabel : forbiddenLabels){
						BDD forcedBDD = !_manager->bddVar(forbiddenLabel + num_factor_vars);
						BDD forbiddenBDD = !forcedBDD;
						// TODO forced BDD must be added as a constraint

						cout << "Factor " << fac << " forbidding label " << forbiddenLabel << " from state " << s << endl;
						for (int ss = 0; ss < factor.get_size(); ss++){
							BDD old = pathsToForbit[s][ss];
							pathsToForbit[s][ss] = (pathsToForbit[s][ss] * forcedBDD).ExistAbstract(forbiddenBDD); 
							cout << "BDD changed from " << old.nodeCount() << " to " << pathsToForbit[s][ss].nodeCount() << endl;
							//string name = "dots/forbid-"+to_string(fac) + "-" + to_string(s)+ "-" + to_string(forbiddenLabel)+ "-" + to_string(ss)+"-old.dot";
							//bdd_to_dot(old, name);
							//name = "dots/forbid-"+to_string(fac) + "-" + to_string(s)+ "-" + to_string(forbiddenLabel)+ "-" + to_string(ss)+"-new.dot";
							//bdd_to_dot(pathsToForbit[s][ss], name);
						}
					}
				}
			}

			cout << "Factor Overall: before limiting " << summedSizeBefore << " after limiting " << summedSizeAfter  << " all transitions: " << allTrans << " possible 1-step transitions: " << possibleSingleTrans <<  endl;
		}

		//int overallVar = bdd_to_cnf(allTransitionsBDD.getNode());
		//cout << "Overall :" << "T" << overallVar << endl;
		// TODO for debugging
		if (bddCovering) exit(0);
		
		BDD stateCube = _manager->bddOne();
		for (int i = 0; i < num_factor_vars; i++) stateCube *= _manager->bddVar(i);

		if (bddCutting){
			// fixpoint algorithm. Will break from the inside if no BDD changes.
			int round = 0;
			bool anyUpdate = true;
			while (anyUpdate) {	
				cout << "Propagation Round " << round << endl;
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

				//cout << "Data structures prepared " << endl;
	
				// 2. Go over all pairs of factors	
				for (int facS = 0; facS < fts->get_size(); facS++){
					const task_representation::TransitionSystem & factorSource = fts->get_ts(facS);
					for (int facT = 0; facT < fts->get_size(); facT++){
						const task_representation::TransitionSystem & factorTarget = fts->get_ts(facT);
						if (facS == facT) continue;
					
						// cube for the variables that need to be abstracted away
						BDD cube = _manager->bddOne();
						//cout << "Propagate from " << facS << " to " << facT << " Round: " << round << " labels: " << fts->get_num_labels() << endl;
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
						//cout << "Mask build" << endl;

						// no shared variables
						if (!foundRemainingVariable) continue;

						//string name = "dots/bef"+to_string(facS) + "-" + to_string(facT)+".dot";
						//bdd_to_dot(any_transition_per_factor[facS], name);
						BDD constraintsOverLabelsRelevantForTarget = any_transition_per_factor[facS].ExistAbstract(cube);
						//cout << "Projected onto mask" << endl;
						//name = "dots/aft"+to_string(facS) + "-" + to_string(facT)+".dot";
						//bdd_to_dot(constraintsOverLabelsRelevantForTarget, name);

						// if the relevant BDD is 1, then there is nothing to propagate.
						if (constraintsOverLabelsRelevantForTarget == _manager->bddOne()) continue;
						// actually propagate
						if (!combineAllBDDsIntoOne){
							//cout << "Apply to " << factorTarget.get_size() * factorTarget.get_size()<< endl;
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
							//cout << "Done" << endl;
						
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

    cout << "SAT init time: " << sat_init_timer << endl;
}

vector<vector<int>> BDDSATEncoding::generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */){
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

vector<int> BDDSATEncoding::generateLabelVars(__attribute__((unused)) void* solver, sat_capsule & capsule/* , int timestep */){
	vector<int> labelVars(fts->get_num_labels());
	for(int label = 0 ; label < fts->get_num_labels() ; label++){
		int labelVar = capsule.new_variable();
		labelVars[label] = labelVar;
		DEBUG(capsule.registerVariable(labelVar,"Label:"+to_string(label)));
		//cout << labelVar << endl;
	}
	if (forceAtLeastOneAction)
		atLeastOne(solver, capsule, labelVars);
	return labelVars;
}


void BDDSATEncoding::encode(int currentLength, int stepTimeLimit) {
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

	vector<vector<int>> previousStateVars = generateStateVars(solver, capsule/* , 0 */);
	allTimesStateVars.push_back(previousStateVars);

	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		assertYes(solver, previousStateVars[ts][fts->get_ts(ts).get_init_state()]);
	}

	vector<int> labelVars;
	vector<vector<int>> nextStateVars;
	
	// determine the in-degree of nodes in the BDD for better encoding.
	for(int fac = 0 ; fac < fts->get_size() ; fac++){
		if (combineAllBDDsIntoOne){
			bdd_in_degree(transition_BDDs_per_factor[fac].getNode());
		} else {
			const task_representation::TransitionSystem & factor = fts->get_ts(fac);
			for (int s = 0; s < factor.get_size(); s++){
				for (int ss = 0; ss < factor.get_size(); ss++){
					bdd_in_degree(transition_BDDs_per_factor_per_state_pair[fac][s][ss].getNode());
				}
			}
		}
	}


	for(int timestep = 1 ; timestep <= currentLength ; timestep++){
		labelVars = generateLabelVars(solver, capsule/* , int timestep */);
		allTimesLabelVars.push_back(labelVars);
		nextStateVars = generateStateVars(solver, capsule/* , timestep */);
		allTimesStateVars.push_back(nextStateVars);

		// BDD-based encoding
		for(int fac = 0 ; fac < fts->get_size() ; fac++){
			tseitsinVars.clear();
			if (combineAllBDDsIntoOne){
				vector<int> __no_conditions;
				bdd_to_cnf(transition_BDDs_per_factor[fac].getNode(), __no_conditions, previousStateVars[fac], labelVars, nextStateVars[fac], solver, capsule);
			} else {
				const task_representation::TransitionSystem & factor = fts->get_ts(fac);
				for (int s = 0; s < factor.get_size(); s++){
					for (int ss = 0; ss < factor.get_size(); ss++){
						//// edge case: it can happen that this transition is impossible under the chosen order
						//if (transition_BDDs_per_factor_per_state_pair[fac][s][ss] == _manager->bddZero()){
						//	impliesNot(solver,previousStateVars[fac][s], nextStateVars[fac][ss]);
						//	continue;
						//}
						//if (transition_BDDs_per_factor_per_state_pair[fac][s][ss] == _manager->bddOne()){
						//	// Nothing to encode, this transition is always allowed
						//	continue;
						//}

						// Providing the previous and next state here is useless -- they will not be accessed anyway.
						// But the function API requires them.
						vector<int> conditions = {previousStateVars[fac][s], nextStateVars[fac][ss]};
						bdd_to_cnf(transition_BDDs_per_factor_per_state_pair[fac][s][ss].getNode(), conditions, previousStateVars[fac], labelVars, nextStateVars[fac], solver, capsule);
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
				return;
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
			if(ipasir_val(solver, allTimesLabelVars[timestep-1][label]) <= 0){
				continue;
			}else{
				selectedLabels.push_back(label);
				timesteps_with_labels.insert(timestep);
				cout << "Label : " << label << endl;
			}
		}

		if(selectedLabels.size() > 1){
			for(size_t l = 0 ; l < selectedLabels.size() - 1 ; l++){
				vector<int> intermediateState;
				for(int ts = 0 ; ts < fts->get_size() ; ts++){
					// TODO what to do here
					//if(isAlwaysSelfLoop(ts, selectedLabels[l])){
					//	intermediateState.push_back(statesPerTimestep.back()[ts]);
					//}else{
					//	intermediateState.push_back(stateReconstructor[ts]);
					//}
				}
				statesPerTimestep.push_back(intermediateState);
			}
		}

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
		// For the BDD-based encoding, we need to reconstruct the plan via search (labels are non-deterministic)
		// What we have: the labels to be applied in which order (in selectedLabels) and the previous and next overall state
		// We know how many intermediate state there are *and*
		// that the determination of the intermediate states is independent between all factors.
		// So we can reconstruct the visited states per factor
		if (selectedLabels.size()){ // if we don't execute any label, we don't have to extract a new state.
			// the labels are sorted in their natural order -- that is from 1 to L, but we might have used a different label ordering.
			// we need to re-order them
		

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
}
	

bool BDDSATEncoding::bdd_state_reconstruction_dfs(int fac, std::vector<std::vector<int>> & reconstructedStates, int depth, std::vector<int> & plan, std::set<std::pair<int,int>> & visited){
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

};
