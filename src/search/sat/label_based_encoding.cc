#include <chrono>
#include <thread>
#include <ctime>
#include <atomic>

#include "label_based_encoding.h"

#include "fts_matrix.h"
#include "../utils/logging.h"
#include "../utils/timer.h"
#include "ipasir.h"
#include "length_strategy.h"
#include "sat_encoder.h"
#include "../task_utils/label_order_finder.h"

#include "../options/option_parser.h"
#include "../options/options.h"
#include "../options/plugin.h"


using namespace std;
using namespace task_representation;



namespace sat_search {
LabelBasedEncodingFactory::LabelBasedEncodingFactory(const options::Options &opts): SATEncodingFactory(opts.get<bool>("force_at_least_one_action")),
	useLabelGroups(opts.get<bool>("use_label_group")),
	useSelfloopOptimisation(opts.get<bool>("use_self_loop_optimisation")),
	useEmptyRows(opts.get<bool>("use_empty_rows")),
	useEmptyCols(opts.get<bool>("use_empty_cols")),
	useEmptyPillars(opts.get<bool>("use_empty_pillars")),
	encoding(encoding_type(opts.get_enum("encoding")))
	 {
	if (encoding != SEQUENTIAL && useSelfloopOptimisation == false){
		cerr << "Parallel label-based encodings may only be used together with the self-loop optimisation" << endl;
		assert(false);
	}
}



static shared_ptr<SATEncodingFactory> _parse_label_based_sat_factory(options::OptionParser &parser) {
	vector<string> base_encoding;
	vector<string> base_encoding_doc;
	base_encoding.push_back("SEQUENTIAL");
	base_encoding_doc.push_back("sequential encoding");
	base_encoding.push_back("SELF_LOOP_PARALLEL");
	base_encoding_doc.push_back("self loop parallelism");
	base_encoding.push_back("CHAINS_PARALLEL");
	base_encoding_doc.push_back( "chains parallelism");
	parser.add_enum_option("encoding",
	                       base_encoding,
	                       "base encoding to be used",
	                       "SEQUENTIAL",
	                       base_encoding_doc);

	parser.add_option<bool>(
    	"use_label_group",
    	"use label group optimisation",
    	"false");

	parser.add_option<bool>(
    	"use_self_loop_optimisation",
    	"use optimisation for self loops",
    	"false");

	parser.add_option<bool>(
    	"use_empty_rows",
    	"use separate encoding for empty rows",
    	"false");

	parser.add_option<bool>(
    	"use_empty_cols",
    	"use separate encoding for empty cols",
    	"false");

	parser.add_option<bool>(
    	"use_empty_pillars",
    	"use separate encoding for empty pillars",
    	"false");

	parser.add_option<bool>(
    	"force_at_least_one_action",
    	"force that every time step contains at least one action",
    	"false");

    options::Options opts = parser.parse();
    if (parser.dry_run())
        return nullptr;
    else
        return make_shared<LabelBasedEncodingFactory>(opts);
}

static options::PluginShared<SATEncodingFactory> _plugin_label_based_sat_factory("label_sat", _parse_label_based_sat_factory);



LabelBasedEncoding::LabelBasedEncoding(
	sat_capsule & capsule,
	const std::shared_ptr<FTSTask> & _fts,
	bool _useLabelGroups,
	bool _useSelfloopOptimisation,
	bool _useEmptyRows,
	bool _useEmptyCols,
	bool _useEmptyPillars,
	bool forceAtLeastOneAction,
	const encoding_type & _encoding,
	const std::vector<shared_ptr<FTSMatrix>> & fts_matrix): SATEncoding(capsule,_fts, forceAtLeastOneAction),
	useLabelGroups(_useLabelGroups),
	useSelfloopOptimisation(_useSelfloopOptimisation),
	useEmptyRows(_useEmptyRows),
	useEmptyCols(_useEmptyCols),
	useEmptyPillars(_useEmptyPillars),
	encoding(_encoding),
	fts(_fts),
	fts_matrices(fts_matrix)
{
}

void LabelBasedEncodingFactory::initialize() {
	utils::Timer sat_init_timer;
	cout << "Initialising" << fts << endl;
	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

    for (const auto & ts : fts->get_transition_systems()) {
        fts_matrices.push_back(make_shared<FTSMatrix>(*ts, useEmptyRows, useEmptyCols, useEmptyPillars));
    }

    cout << "SAT init time: " << sat_init_timer << endl;
}



unique_ptr<SATEncoding> LabelBasedEncodingFactory::createEncodingInstance(sat_capsule & capsule){
	return make_unique<LabelBasedEncoding>(capsule,fts,useLabelGroups,useSelfloopOptimisation,
			useEmptyRows,useEmptyCols,useEmptyPillars,forceAtLeastOneAction,encoding,
			fts_matrices);
}




vector<vector<int>> LabelBasedEncoding::generateStateVars(/* , int timestep */) const {
	vector<vector<int>> stateVars(fts->get_size());
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			int stateVar = sat.new_variable();
			stateVars[ts].push_back(stateVar);
			DEBUG(sat.registerVariable(stateVar,"TS:"+to_string(ts)+";Val:"+to_string(states)));
		}
		sat.atMostOne(stateVars[ts]);
		sat.atLeastOne(stateVars[ts]);
	}
	return stateVars;
}

vector<int> LabelBasedEncoding::generateLabelVars(/* , int timestep */) const {
	vector<int> labelVars(fts->get_num_labels());
	for(int label = 0 ; label < fts->get_num_labels() ; label++){
		int labelVar = sat.new_variable();
		labelVars[label] = labelVar;
		DEBUG(sat.registerVariable(labelVar,"Label:"+to_string(label)));
		//cout << labelVar << endl;
	}
	return labelVars;
}

vector<vector<vector<int>>> LabelBasedEncoding::generateLabelGroupVars(const vector<int> &labelVars/* , int timestep */) const{
	vector<vector<vector<int>>> labelGroupVars(fts->get_size());
	for(int ts = 0 ; ts < fts->get_size(); ts++){
		const auto & fts_matrix = fts_matrices[ts];
		labelGroupVars[ts].resize(fts_matrix->get_num_label_groups());
		for(int lg = 0 ; lg < fts_matrix->get_num_label_groups(); lg++){
			if (useLabelGroups) {
				// if the label group has only one member then always use the variable of that label itself.
				if(fts_matrix->get_labels_in_label_group(lg).size() == 1){
					labelGroupVars[ts][lg].push_back(labelVars[fts_matrix->get_labels_in_label_group(lg)[0]]);
					continue;
				}
				int lab_group = sat.new_variable();
				DEBUG(sat.registerVariable(lab_group,"LabelGroup:"+to_string(lg)));
				labelGroupVars[ts][lg].push_back(lab_group);
				vector<int> labels;
				for(int label : fts_matrix->get_labels_in_label_group(lg)){
					sat.implies(labelVars[label], lab_group);
					labels.push_back(labelVars[label]);
				}
				sat.impliesOr(lab_group, labels);
			} else {
				for(int label : fts_matrix->get_labels_in_label_group(lg)) {
					labelGroupVars[ts][lg].push_back(labelVars[label]);
				}
			}
		}
	}
	return labelGroupVars;
}

map<int, map<int, vector<int>>> LabelBasedEncoding::generateHelperVars(/* , int timestep */) const{
	map<int, map<int, vector<int>>> helperVars;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			int num_helper_vars = fts_matrices[ts]->get_not_always_selfloop_labels_reaching_target(states).size() - 1;
			for(int h = 0 ; h < num_helper_vars ; h++){
				int helperVar = sat.new_variable();
				DEBUG(sat.registerVariable(helperVar, "Helpers"));
				helperVars[ts][states].push_back(helperVar);
			}
		}
	}
	return helperVars;
}


void LabelBasedEncoding::encode_sequential(const vector<int> & labelVars){
	sat.atMostOne(labelVars);
}

void LabelBasedEncoding::encode_self_loop_parallel(const vector<int> & labelVars){
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		const TransitionSystem & tss = fts->get_ts(ts);
 		vector<int> non_parallelisable_labels;
 		for(int label = 0 ; label < fts->get_num_labels() ; label++){
 			if(!tss.isAlwaysSelfLoop(label)){
 				non_parallelisable_labels.push_back(labelVars[label]);
 			}
 		}
 		sat.atMostOne(non_parallelisable_labels);
 	}
}

void LabelBasedEncoding::encode_chains_parallel(const vector<int> & labelVars, const vector<vector<int>> & nextStateVars){
	map<int, map<int, vector<int>>> topHelperVars = generateHelperVars();
	map<int, map<int, vector<int>>> bottomHelperVars = generateHelperVars();

	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		const TransitionSystem & tss = fts->get_ts(ts);
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){

			const auto & labelsWithEffect = fts_matrices[ts]->get_not_always_selfloop_labels_reaching_target(states);
			for(size_t l = 0 ; l < labelsWithEffect.size() ; l++){
				if(l < labelsWithEffect.size()-1){
					sat.andImplies(labelVars[labelsWithEffect[l]], nextStateVars[ts][states], topHelperVars[ts][states][l]);
					if(!tss.hasSelfLoopOnValue(states, labelsWithEffect[l+1])){
						sat.implies(topHelperVars[ts][states][l], -labelVars[labelsWithEffect[l+1]]);
					}
					if(!tss.hasSelfLoopOnValue(states, labelsWithEffect[l])){
						sat.implies(bottomHelperVars[ts][states][l], -labelVars[labelsWithEffect[l]]);
					}
				}
				if(l > 0){
					sat.andImplies(labelVars[labelsWithEffect[l]], nextStateVars[ts][states], bottomHelperVars[ts][states][l-1]);
				}
				if(l > 0 && l < labelsWithEffect.size()-1){
					sat.implies(topHelperVars[ts][states][l-1], topHelperVars[ts][states][l]);
					sat.implies(bottomHelperVars[ts][states][l], bottomHelperVars[ts][states][l-1]);
				}
			}
		}
	}
}

void LabelBasedEncoding::encode_transition(const vector<vector<int>> & previousStateVars,
	const vector<vector<vector<int>>> & labelGroupVars, const vector<vector<int>> & nextStateVars){
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		const auto &fts_matrix = fts_matrices[ts];
		const TransitionSystem & tss = fts->get_ts(ts);
		vector<int> labelGroupsWithActualTransitions;

		// 1. Step: if desired, handle self-loops separately	
		if(useSelfloopOptimisation) {
			for(int lg = 0 ; lg < fts_matrix->get_num_label_groups() ; lg++) {
				// representative label of this group
				int label = fts_matrix->get_labels_in_label_group(lg)[0];

				if(tss.isIrrelevantLabel(label)) continue;
				if(tss.isAlwaysSelfLoop(label)){
					auto transitions = tss.get_transitions_with_label(label);
					vector<int> preconditions;
					vector<int> effects;
					for(Transition t : transitions){
						preconditions.push_back(previousStateVars[ts][t.src]);
						effects.push_back(nextStateVars[ts][t.target]);
					}

					for(const int var : labelGroupVars[ts][lg]){
						sat.impliesOr(var, preconditions);
						sat.impliesOr(var, effects);
					}
					continue;
				}
				// if we reach this point, the label group has actual transitions
				for(const int var : labelGroupVars[ts][lg]){
					labelGroupsWithActualTransitions.push_back(var);
				}
			}
			// frame axioms for self-loops. If no label with an actual transition was executed, enforce frame axiom
			int selfLoopAuxVar = sat.new_variable();
			DEBUG(sat.registerVariable(selfLoopAuxVar,"selfLoopAuxVar"));
			sat.impliesOr(-selfLoopAuxVar, labelGroupsWithActualTransitions);
			for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
				sat.andImplies(selfLoopAuxVar, previousStateVars[ts][states], nextStateVars[ts][states]);
				sat.andImplies(-selfLoopAuxVar, previousStateVars[ts][states], -nextStateVars[ts][states]);
			}
		}

		
		//////////////
		// 2. Step: encode optimised parts of the encoding
		//
		// 2.a: first dimension is label 
		for(int lg = 0 ; lg < fts_matrix->get_num_label_groups() ; lg++) {
			// representative label of this group
			int label = fts_matrix->get_labels_in_label_group(lg)[0];
			// self-loop: has been encoded before
			if(useSelfloopOptimisation && tss.isAlwaysSelfLoop(label)) continue;

			for(int neg_prec : fts_matrix->get_impossible_sources_for_label(lg)){
				for(const int var : labelGroupVars[ts][lg]){
					sat.impliesNot(var, previousStateVars[ts][neg_prec]);
				}
			}
			for(int neg_eff : fts_matrix->get_impossible_targets_for_label(lg)){
				for(const int var : labelGroupVars[ts][lg]){
					sat.impliesNot(var, nextStateVars[ts][neg_eff]);
				}
			}
		}

		// 2.b: first dimension is source 
		for(int src = 0 ; src < fts->get_ts(ts).get_size() ; src++){
			for(int t : fts_matrix->get_impossible_targets_for_source(src)){
				sat.implies(previousStateVars[ts][src], -nextStateVars[ts][t]);
			}
		}
		
		
		//////////////
		// 3. Step: encode any transition that has not otherwise been covered yet.
		// As a heuristic, we always do this in the order label -> source -> target
		for(int lg = 0 ; lg < fts_matrix->get_num_label_groups() ; lg++) {
			int label = fts_matrix->get_labels_in_label_group(lg)[0];
			// self-loop: has been encoded before
			if(useSelfloopOptimisation && tss.isAlwaysSelfLoop(label)) continue;

			for(int src = 0 ; src < fts->get_ts(ts).get_size() ; src++){
				if(fts_matrix->get_impossible_sources_for_label(lg).contains(src)) continue;

				// decision: if label+source can yield only one state encode positively
				// TODO: maybe also do this if there is more than one possible target, but few compared to the non-possible targets
				if(fts_matrix->get_targets_for_source_and_label(lg,src).size() == 1) {
					int t = *fts_matrix->get_targets_for_source_and_label(lg,src).begin();
					for(const int var : labelGroupVars[ts][lg]){
						sat.andImplies(var, previousStateVars[ts][src], nextStateVars[ts][t]);
					}
				} else {
					// if there are multiple ones, we encode negatively instead
					// -- i.e. we forbid a transition to non-possible targets
					for(int target = 0 ; target < fts->get_ts(ts).get_size() ; target++) {
						// check if impossibility to transition to target has been encoded otherwise before
						if(fts_matrix->get_impossible_targets_for_label(lg).contains(target)) continue;
						if(fts_matrix->get_impossible_targets_for_source(src).contains(target)) continue;
					
						// if the transition src+lg->target is impossible, then we need to encode that the transition is forbidden	
						if(!fts_matrix->get_targets_for_source_and_label(lg,src).contains(target)){
							for(const int var_label : labelGroupVars[ts][lg]){
								sat.andImplies(var_label, previousStateVars[ts][src], -nextStateVars[ts][target]);
							}
						}
					}
				}
			}
		}
	}
}

void LabelBasedEncoding::encodeInit(int fromTime){
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		sat.assertYes(allTimesStateVars[fromTime][ts][fts->get_ts(ts).get_init_state()]);
	}
}


void LabelBasedEncoding::encodeGoal(int toTime){
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		vector<int> goals = fts->get_ts(ts).get_goal_states();
		vector<int> goalStateVars;
		for(int goal : goals){
			goalStateVars.push_back(allTimesStateVars[toTime][ts][goal]);
		}
		sat.atLeastOne(goalStateVars);
	}
}


void LabelBasedEncoding::encode(int fromTime, int toTime){
    //utils::Timer step_timer;  // needed later to stop the encoding if we want to schedule a different instance
	//auto t_start = std::chrono::system_clock::now();

	/// Step 1: generate variables (some might already exist)
	// generate state vars if necessary for from time
	auto preStateVarFind = allTimesStateVars.find(fromTime);
	const vector<vector<int>> & previousStateVars = (preStateVarFind == allTimesStateVars.end()) ? 
		(allTimesStateVars[fromTime] = generateStateVars()):
		preStateVarFind->second;

	// generate state vars if necessary for next time
	auto nextStateVarFind = allTimesStateVars.find(toTime);
	const vector<vector<int>> & nextStateVars = (nextStateVarFind == allTimesStateVars.end()) ? 
		allTimesStateVars[toTime] = generateStateVars():
		nextStateVarFind->second;

	// generate label vars (must be generated before)
	assert(!allTimesLabelVars.contains(fromTime));
	const vector<int> & labelVars = generateLabelVars();
	allTimesLabelVars[fromTime] = labelVars;

	// label group vars: one variable if we useSelfloopOptimisation, otherwise all variables for all labels of each group
	const vector<vector<vector<int>>> & labelGroupVars = generateLabelGroupVars(labelVars);

	/// 2. Step encode the state transition.
	encode_transition(previousStateVars,labelGroupVars,nextStateVars);

	// 3. Step encode at least one action constraint if necessary	
	if (forceAtLeastOneAction) {
		sat.atLeastOne(labelVars);
	}

	// 4. Step encode the restrictions on which actions are allowed in parallel as per the encoding
	if(encoding == SEQUENTIAL)
		encode_sequential(labelVars);
	else if(encoding == SELF_LOOP_PARALLEL)
		encode_self_loop_parallel(labelVars);
	else if(encoding == CHAINS_PARALLEL)
		encode_chains_parallel(labelVars, nextStateVars);

}


// run plan extraction
std::tuple<PlanState,std::vector<PlanState>,std::vector<int>,std::set<int>> LabelBasedEncoding::extractSolution(int initTime,
	std::vector<std::pair<int,int>> time_step_order){
	vector<vector<int>> statesPerTimestep;
	// extract the initial state
	vector<int> stateReconstructor;
	for(size_t ts = 0 ; ts < allTimesStateVars[initTime].size() ; ts++){
		for(size_t state = 0 ; state < allTimesStateVars[initTime][ts].size() ; state++){
			if(ipasir_val(sat.solver, allTimesStateVars[initTime][ts][state]) > 0){
				stateReconstructor.push_back(state);
				break;
			}
		}
	}
	statesPerTimestep.push_back(stateReconstructor);
	stateReconstructor.clear();
	set<int> timesteps_with_labels;	
	
	vector<vector<int>> labelsPerTimestep;

	// iterate over the time-steps in order given by the main algorithm (it might have used strange numbering)
	for(auto [labelTimestep,stateAfterTimestep] : time_step_order){
		cout << "Time " << labelTimestep << endl;
		vector<int> selectedLabels;
		for(size_t label = 0 ; label < allTimesLabelVars[labelTimestep].size() ; label++){
			if(ipasir_val(sat.solver, allTimesLabelVars[labelTimestep][label]) <= 0){
				continue;
			}else{
				selectedLabels.push_back(label);
				timesteps_with_labels.insert(labelTimestep);
				cout << "Label : " << label << endl;
				for(int ts = 0 ; ts < fts->get_size() ; ts++){
					for(size_t state = 0 ; state < allTimesStateVars[stateAfterTimestep][ts].size() ; state++){
						if(ipasir_val(sat.solver, allTimesStateVars[stateAfterTimestep][ts][state]) > 0){
							stateReconstructor.push_back(state);
							break;
						}
					}
				}
			}
		}

		// intermediate states
		if(selectedLabels.size() > 1){
			for(size_t l = 0 ; l < selectedLabels.size() - 1 ; l++){
				vector<int> intermediateState;
				for(int ts = 0 ; ts < fts->get_size() ; ts++){
					const TransitionSystem & tss = fts->get_ts(ts);
					if(tss.isAlwaysSelfLoop(selectedLabels[l])){
						intermediateState.push_back(statesPerTimestep.back()[ts]);
					}else{
						intermediateState.push_back(stateReconstructor[ts]);
					}
				}
				statesPerTimestep.push_back(intermediateState);
			}
		}

		if (selectedLabels.size() > 0){
			vector<int> notRepeated(stateReconstructor.begin(), stateReconstructor.begin() + fts->get_size());
			statesPerTimestep.push_back(notRepeated);
			stateReconstructor.clear();
			notRepeated.clear();

			labelsPerTimestep.push_back(selectedLabels);
		}
	}


	vector<int> GS = statesPerTimestep.back();
	PlanState goalState = PlanState(std::move(GS));
	vector<PlanState> states;
	for(size_t s = 0 ; s < statesPerTimestep.size() ; s++){
		states.push_back(PlanState(std::move(statesPerTimestep[s])));
	}

	vector<int> labels;
	for(const auto & labels_in_t : labelsPerTimestep){
		for(int l : labels_in_t){
			labels.push_back(l);
		}
	}

	cout << "Total states: " << states.size() << endl;
	cout << "Total labels: " << labels.size() << endl;
	cout << "Total timesteps with label: " << timesteps_with_labels.size() << endl;

	return make_tuple(goalState,states,labels,timesteps_with_labels);	
}

};
