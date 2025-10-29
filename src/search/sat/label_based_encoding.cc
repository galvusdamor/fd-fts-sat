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
	useOnesInLastDimension(opts.get<bool>("use_ones_in_last_dimension")),
	usePositiveOneForEmpty(opts.get<bool>("use_positive_one")),
	encoding(encoding_type(opts.get_enum("encoding")))
	 {
	statisticsPrinted = false;
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
    	"use_ones_in_last_dimension",
    	"if in the last dimension implication (source + label -> target), there is only one possible target use the positive edge for encoding instead of the negative edge",
    	"true");

	parser.add_option<bool>(
    	"use_positive_one",
    	"when encoding empty row/col/pillars, use special encoding if there is only one 1 (then positive encoding instead of negative)",
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
	std::shared_ptr<sat_capsule> capsule,
	const std::shared_ptr<FTSTask> & _fts,
	bool _statisticsPrinted,
	bool _useLabelGroups,
	bool _useSelfloopOptimisation,
	bool _useEmptyRows,
	bool _useEmptyCols,
	bool _useEmptyPillars,
	bool _useOnesInLastDimension,
	bool _usePositiveOneForEmpty,
	bool forceAtLeastOneAction,
	const encoding_type & _encoding,
	const std::vector<shared_ptr<FTSMatrix>> & fts_matrix): SATEncoding(capsule,_fts, forceAtLeastOneAction),
	statisticsPrinted(_statisticsPrinted),
	useLabelGroups(_useLabelGroups),
	useSelfloopOptimisation(_useSelfloopOptimisation),
	useEmptyRows(_useEmptyRows),
	useEmptyCols(_useEmptyCols),
	useEmptyPillars(_useEmptyPillars),
	useOnesInLastDimension(_useOnesInLastDimension),
	usePositiveOneForEmpty(_usePositiveOneForEmpty),
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
        fts_matrices.push_back(make_shared<FTSMatrix>(*ts));
    }

    cout << "SAT init time: " << sat_init_timer << endl;
}



unique_ptr<SATEncoding> LabelBasedEncodingFactory::createEncodingInstance(std::shared_ptr<sat_capsule> capsule){
	bool oldStatisticsPrinted = statisticsPrinted;
	statisticsPrinted = true;
	return make_unique<LabelBasedEncoding>(capsule,fts,oldStatisticsPrinted,useLabelGroups,useSelfloopOptimisation,
			useEmptyRows,useEmptyCols,useEmptyPillars,useOnesInLastDimension,usePositiveOneForEmpty,forceAtLeastOneAction,encoding,
			fts_matrices);
}




vector<vector<int>> LabelBasedEncoding::generateStateVars(/* , int timestep */) const {
	vector<vector<int>> stateVars(fts->get_size());
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			int stateVar = sat->new_variable();
			stateVars[ts].push_back(stateVar);
			DEBUG(sat->registerVariable(stateVar,"TS:"+to_string(ts)+";Val:"+to_string(states)));
		}
		sat->atMostOne(stateVars[ts]);
		sat->atLeastOne(stateVars[ts]);
	}
	return stateVars;
}

vector<int> LabelBasedEncoding::generateLabelVars(/* , int timestep */) const {
	vector<int> labelVars(fts->get_num_labels());
	for(int label = 0 ; label < fts->get_num_labels() ; label++){
		int labelVar = sat->new_variable();
		labelVars[label] = labelVar;
		DEBUG(sat->registerVariable(labelVar,"Label:"+to_string(label)));
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
				int lab_group = sat->new_variable();
				DEBUG(sat->registerVariable(lab_group,"LabelGroup:"+to_string(lg)));
				labelGroupVars[ts][lg].push_back(lab_group);
				vector<int> labels;
				for(int label : fts_matrix->get_labels_in_label_group(lg)){
					sat->implies(labelVars[label], lab_group);
					labels.push_back(labelVars[label]);
				}
				sat->impliesOr(lab_group, labels);
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
				int helperVar = sat->new_variable();
				DEBUG(sat->registerVariable(helperVar, "Helpers"));
				helperVars[ts][states].push_back(helperVar);
			}
		}
	}
	return helperVars;
}


void LabelBasedEncoding::encode_sequential(const vector<int> & labelVars){
	sat->atMostOne(labelVars);
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
 		sat->atMostOne(non_parallelisable_labels);
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
					sat->andImplies(labelVars[labelsWithEffect[l]], nextStateVars[ts][states], topHelperVars[ts][states][l]);
					if(!tss.hasSelfLoopOnValue(states, labelsWithEffect[l+1])){
						sat->implies(topHelperVars[ts][states][l], -labelVars[labelsWithEffect[l+1]]);
					}
					if(!tss.hasSelfLoopOnValue(states, labelsWithEffect[l])){
						sat->implies(bottomHelperVars[ts][states][l], -labelVars[labelsWithEffect[l]]);
					}
				}
				if(l > 0){
					sat->andImplies(labelVars[labelsWithEffect[l]], nextStateVars[ts][states], bottomHelperVars[ts][states][l-1]);
				}
				if(l > 0 && l < labelsWithEffect.size()-1){
					sat->implies(topHelperVars[ts][states][l-1], topHelperVars[ts][states][l]);
					sat->implies(bottomHelperVars[ts][states][l], bottomHelperVars[ts][states][l-1]);
				}
			}
		}
	}
}

void LabelBasedEncoding::encode_transition(const vector<vector<int>> & previousStateVars,
	const vector<vector<vector<int>>> & labelGroupVars, const vector<vector<int>> & nextStateVars){
	// for statistics
	int cnt_0_label_target = 0;
	int cnt_0_label_source = 0;
	int cnt_0_source_target = 0;
	int cnt_0_label_source_target = 0;
	int cnt_1_label_target = 0;
	int cnt_1_label_source = 0;
	int cnt_1_source_label = 0;
	int cnt_1_source_target = 0;
	int cnt_1_target_source = 0;
	int cnt_1_target_label = 0;
	int cnt_1_label_source_target = 0;
	
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		const auto &fts_matrix = fts_matrices[ts];
		const TransitionSystem & tss = fts->get_ts(ts);
		const int numLabelGroups = fts_matrix->get_num_label_groups();
		const int numStates = fts->get_ts(ts).get_size();
		vector<int> labelGroupsWithActualTransitions;

		int selfLoopAuxVar = 0; // for later use

		// 1. Step: if desired, handle self-loops separately	
		if(useSelfloopOptimisation) {
			for(int lg = 0 ; lg < numLabelGroups ; lg++) {
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
						sat->impliesOr(var, preconditions);
						sat->impliesOr(var, effects);
					}
					continue;
				}
				// if we reach this point, the label group has actual transitions
				for(const int var : labelGroupVars[ts][lg]){
					labelGroupsWithActualTransitions.push_back(var);
				}
			}
			// frame axioms for self-loops. If no label with an actual transition was executed, enforce frame axiom
			selfLoopAuxVar = sat->new_variable();
			DEBUG(sat->registerVariable(selfLoopAuxVar,"selfLoopAuxVar"));
			sat->impliesOr(-selfLoopAuxVar, labelGroupsWithActualTransitions);
			// Note: these clauses are equivalent to: 
			// s + p -> n = -s v -p v n = p & -n -> -s
			// -s + p -> -n = s v -p v -n = p & n -> s
			// Since one of the p's is always true, they *force* s to be true of we remain in the same state.
			for(int states = 0 ; states < numStates ; states++){
				sat->andImplies(selfLoopAuxVar, previousStateVars[ts][states], nextStateVars[ts][states]);
				sat->andImplies(-selfLoopAuxVar, previousStateVars[ts][states], -nextStateVars[ts][states]);
			}
		}


		// data structures to memorise which zero's were already covered.
		// X_covered_Y[x] means: given a fixed x, list all y's for which all zero's in the remaining dimension z have already been forbidden by the encoding.
		// That is, we have encoded that x -> -y
		vector<set<int>> label_covered_targets(numLabelGroups);
		vector<set<int>> label_covered_sources(numLabelGroups);
		vector<set<int>> source_covered_targets(numStates);


		//////////////
		// 2. Step: encode optimised parts of the encoding
		//
		// 2.a: Rows. If we ignore targets, which source/label pairs can we encode compactly?
		if (useEmptyRows){
			if (usePositiveOneForEmpty){
				// try to cover ones in the label -> source direction
				for(int lg = 0 ; lg < numLabelGroups ; lg++) {
					// representative label of this group
					int label = fts_matrix->get_labels_in_label_group(lg)[0];
					// self-loop: has been encoded before
					if (useSelfloopOptimisation && tss.isAlwaysSelfLoop(label)) continue;

					if (fts_matrix->get_possible_sources_for_label(lg).size() <= oneEncodingThreshold){
						vector<int> allOnes;
						for (int source : fts_matrix->get_possible_sources_for_label(lg))
							allOnes.push_back(previousStateVars[ts][source]);

						sat->orImpliesOr(labelGroupVars[ts][lg], allOnes);
						cnt_1_label_source++;
						// we have now encode all 0's from this label to all impossible sources
						for(int source : fts_matrix->get_impossible_sources_for_label(lg)) label_covered_sources[lg].insert(source);
					}
				}

				// try to cover ones in the source -> label direction
				for(int src = 0 ; src < numStates ; src++){
					int self_loop_deduction = 0;
					if (useSelfloopOptimisation) self_loop_deduction = fts_matrix->get_always_self_loop_labels_for_state(src).size();
					if (size_t(fts_matrix->get_possible_labels_for_source(src).size() - self_loop_deduction) <= oneEncodingThreshold){	
					
						// which labels can be executed in this source?
						vector<int> allOnes;
						for (int lg : fts_matrix->get_possible_labels_for_source(src)){
							// ignore labels that are always self-loops if we optimised them out
							if (useSelfloopOptimisation && fts_matrix->get_always_self_loop_labels_for_state(src).contains(lg)) continue;
							for (const int l : labelGroupVars[ts][lg])
								allOnes.push_back(l);
						}

						// if there are always self-loop labels in this state, it can happen that we actually do one of these self-loops
						// if so, the selfLoopAuxVar must be true indicating that we actually execute a self-loop
						if (useSelfloopOptimisation && fts_matrix->get_always_self_loop_labels_for_state(src).size() >= 1)
							allOnes.push_back(selfLoopAuxVar);

						sat->impliesOr(previousStateVars[ts][src], allOnes);
						cnt_1_source_label++;
						
						// we have now encode all 0's from this source label to all impossible labels
						// but only, if we know that forcing one of the labels actually makes executing any other labels impossible
						if (encoding == SEQUENTIAL)
							for (int lg : fts_matrix->get_impossible_labels_for_source(src)) label_covered_sources[lg].insert(src);
						// TODO: can be slightly stronger: if all of the labels in allOnes have *no* self loops, then we actually forbid other non-self-loops (as two non-self-loop transitions are impossible. But this might be a product of encoding their pre/effs)
					}
				}
			}


			// check whether there are any 0's between label and source we have not covered yet.
			for(int lg = 0 ; lg < numLabelGroups ; lg++) {
				int label = fts_matrix->get_labels_in_label_group(lg)[0];
				// self-loop: has been encoded before
				if (useSelfloopOptimisation && tss.isAlwaysSelfLoop(label)) continue;
				for(int source : fts_matrix->get_impossible_sources_for_label(lg)){
					// check if impossible source is already covered
					if (label_covered_sources[lg].contains(source)) continue; 
					// if not, encode it
					sat->orImpliesNot(labelGroupVars[ts][lg], previousStateVars[ts][source]);
					cnt_0_label_source++;
					label_covered_sources[lg].insert(source);
				}
			}
		}


		// 2.b: Cols. If we ignore sources, which target/label pairs can we encode compactly?
		if (useEmptyCols){
			if (usePositiveOneForEmpty){
				// try to cover ones in the label -> target direction
				for(int lg = 0 ; lg < numLabelGroups ; lg++) {
					// representative label of this group
					int label = fts_matrix->get_labels_in_label_group(lg)[0];
					// self-loop: has been encoded before
					if (useSelfloopOptimisation && tss.isAlwaysSelfLoop(label)) continue;

					if (fts_matrix->get_possible_targets_for_label(lg).size() <= oneEncodingThreshold){
						vector<int> allOnes;
						for (int target : fts_matrix->get_possible_targets_for_label(lg))
							allOnes.push_back(nextStateVars[ts][target]);

						sat->orImpliesOr(labelGroupVars[ts][lg], allOnes);
						cnt_1_label_target++;
						// we have now encode all 0's from this label to all impossible targets
						for(int target : fts_matrix->get_impossible_targets_for_label(lg)) label_covered_targets[lg].insert(target);
					}
				}

				// try to cover ones in the target -> label direction
				for(int target = 0 ; target < numStates ; target++){
					int self_loop_deduction = 0;
					if (useSelfloopOptimisation) self_loop_deduction = fts_matrix->get_always_self_loop_labels_for_state(target).size();
					if (size_t(fts_matrix->get_possible_labels_for_target(target).size() - self_loop_deduction) <= oneEncodingThreshold){	
						
						// which labels can be executed in this target?
						vector<int> allOnes;
						for (int lg : fts_matrix->get_possible_labels_for_target(target)){
							// ignore labels that are always self-loops if we optimised them out
							if (useSelfloopOptimisation && fts_matrix->get_always_self_loop_labels_for_state(target).contains(lg)) continue;
							for (const int l : labelGroupVars[ts][lg])
								allOnes.push_back(l);
						}

						// if there are always self-loop labels in this state, it can happen that we actually do one of these self-loops
						// if so, the selfLoopAuxVar must be true indicating that we actually execute a self-loop
						if (useSelfloopOptimisation && fts_matrix->get_always_self_loop_labels_for_state(target).size() >= 1)
							allOnes.push_back(selfLoopAuxVar);

						sat->impliesOr(nextStateVars[ts][target], allOnes);
						cnt_1_target_label++;
						// we have now encode all 0's from this source label to all impossible labels
						// but only, if we know that forcing one of the labels actually makes executing any other labels impossible
						if (encoding == SEQUENTIAL)
							for (int lg : fts_matrix->get_impossible_labels_for_target(target)) label_covered_targets[lg].insert(target);
					}
				}
			}


			// check whether there are any 0's between label and target we have not covered yet.
			for(int lg = 0 ; lg < numLabelGroups ; lg++) {
				int label = fts_matrix->get_labels_in_label_group(lg)[0];
				// self-loop: has been encoded before
				if (useSelfloopOptimisation && tss.isAlwaysSelfLoop(label)) continue;
				for(int target : fts_matrix->get_impossible_targets_for_label(lg)){
					// check if impossible target is already covered
					if (label_covered_targets[lg].contains(target)) continue; 
					// if not, encode it
					sat->orImpliesNot(labelGroupVars[ts][lg], nextStateVars[ts][target]);
					cnt_0_label_target++;
					label_covered_targets[lg].insert(target);
				}
			}
		}

		// 2.c: Pillars. If we ignore the labels, can we encode source target transitions compactly
		if (useEmptyPillars){ 
			if (usePositiveOneForEmpty){
				for(int src = 0 ; src < numStates ; src++){
					// if the state has a self-loop, we accept one more possible successor.
					int self_loop_deduction = 0;
					if (fts_matrix->get_self_loop_labels_for_state(src).size() >= 1) self_loop_deduction = 1;
					
					if (size_t(fts_matrix->get_possible_targets_for_source(src).size() - self_loop_deduction) <= oneEncodingThreshold){
						vector<int> allOnes;
						for (int target : fts_matrix->get_possible_targets_for_source(src))
							allOnes.push_back(nextStateVars[ts][target]);

						sat->impliesOr(previousStateVars[ts][src], allOnes);
						cnt_1_source_target++;
						for(int target : fts_matrix->get_impossible_targets_for_source(src)) source_covered_targets[src].insert(target);
					}
				}

				for(int target = 0 ; target < numStates ; target++){
					// if the state has a self-loop, we accept one more possible predecessor.
					int self_loop_deduction = 0;
					if (fts_matrix->get_self_loop_labels_for_state(target).size() >= 1) self_loop_deduction = 1;
					
					if (size_t(fts_matrix->get_possible_sources_for_target(target).size() - self_loop_deduction) <= oneEncodingThreshold){
						vector<int> allOnes;
						for (int src : fts_matrix->get_possible_sources_for_target(target))
							allOnes.push_back(previousStateVars[ts][src]);

						sat->impliesOr(nextStateVars[ts][target], allOnes);
						cnt_1_target_source++;
						for (int src : fts_matrix->get_impossible_sources_for_target(target)) source_covered_targets[src].insert(target);
					}
				}
			}

			for(int src = 0 ; src < numStates ; src++){
				for(int target : fts_matrix->get_impossible_targets_for_source(src)){
					if (source_covered_targets[src].contains(target)) continue;

					sat->implies(previousStateVars[ts][src], -nextStateVars[ts][target]);
					cnt_0_source_target++;
					source_covered_targets[src].insert(target);
				}
			}
		}

		//////////////
		// 3. Step: encode any transition that has not otherwise been covered yet.
		// As a heuristic, we always do this in the order label -> source -> target
		for(int lg = 0 ; lg < numLabelGroups ; lg++) {
			int label = fts_matrix->get_labels_in_label_group(lg)[0];
			// self-loop: has been encoded before
			if(useSelfloopOptimisation && tss.isAlwaysSelfLoop(label)) continue;

			for(int src = 0 ; src < numStates ; src++){
				// Given label lg, if we already asserted the implication lg -> -src, we have nothing to do.
				if (label_covered_sources[lg].contains(src)) continue;

				// decision: if label+source can yield only one state encode positively
				// TODO: maybe also do this if there is more than one possible target, but few compared to the non-possible targets
				if(useOnesInLastDimension && fts_matrix->get_targets_for_source_and_label(lg,src).size() == 1) {
					int target = *fts_matrix->get_targets_for_source_and_label(lg,src).begin();

					// for this label group, all other targets are impossible	
					if (int(label_covered_targets[lg].size()) == numStates - 1) {
						assert(label_covered_targets[lg].contains(target) == false);
						continue;
					}

					if (int(source_covered_targets[src].size()) == numStates - 1){
						assert(source_covered_targets[src].contains(target) == false);
						continue;
					}
				
					sat->orAndImplies(labelGroupVars[ts][lg], previousStateVars[ts][src], nextStateVars[ts][target]);
					cnt_1_label_source_target++;
				} else {
					// if there are multiple ones, we encode negatively instead
					// -- i.e. we forbid a transition to non-possible targets
					for(int target = 0 ; target < numStates ; target++) {
						// check if impossibility to transition to target has been encoded otherwise before
						if (label_covered_targets[lg].contains(target)) continue;
						if (source_covered_targets[src].contains(target)) continue;
					
						// if the transition src+lg->target is impossible, then we need to encode that the transition is forbidden	
						if(!fts_matrix->get_targets_for_source_and_label(lg,src).contains(target)){
							sat->orAndImplies(labelGroupVars[ts][lg], previousStateVars[ts][src], -nextStateVars[ts][target]);
							cnt_0_label_source_target++;
						}
					}
				}
			}
		}
	}

	if (!statisticsPrinted){
		statisticsPrinted = true;
		cout << "0_label_target       : " << cnt_0_label_target        << endl;
		cout << "0_label_source       : " << cnt_0_label_source        << endl;
		cout << "0_source_target      : " << cnt_0_source_target       << endl;
		cout << "0_label_source_target: " << cnt_0_label_source_target << endl;
		cout << "1_label_target       : " << cnt_1_label_target        << endl;
		cout << "1_label_source       : " << cnt_1_label_source        << endl;
		cout << "1_source_label       : " << cnt_1_source_label        << endl;
		cout << "1_source_target      : " << cnt_1_source_target       << endl;
		cout << "1_target_source      : " << cnt_1_target_source       << endl;
		cout << "1_target_label       : " << cnt_1_target_label        << endl;
		cout << "1_label_source_target: " << cnt_1_label_source_target << endl;
	}
}


void LabelBasedEncoding::encode_frame_axioms(const vector<vector<int>> & previousStateVars,
	const vector<vector<vector<int>>> & labelGroupVars, const vector<vector<int>> & nextStateVars){
	
	// ensure that if the state changes *some* action is executed.
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		const auto &fts_matrix = fts_matrices[ts];
		const int numLabelGroups = fts_matrix->get_num_label_groups();
		const int numStates = fts->get_ts(ts).get_size();


		vector<int> labelGroups;
		for(int lg = 0 ; lg < numLabelGroups ; lg++) {
			for(const int var : labelGroupVars[ts][lg]){
				labelGroups.push_back(var);
			}
		}
		
		// frame axioms. If label was executed, enforce frame axiom
		int someLabelExecuted = sat->new_variable();
		DEBUG(sat->registerVariable(someLabelExecuted,"someLabelExecuted_ts=" + to_string(ts)));
		sat->impliesOr(someLabelExecuted, labelGroups);
		for(int states = 0 ; states < numStates ; states++){
			sat->andImplies(previousStateVars[ts][states], - nextStateVars[ts][states], someLabelExecuted);
		}
	}
}

void LabelBasedEncoding::encodeStateEquals(int fromTime, int toTime, bool retractable){
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

	int assumptionVariable = 0;
	if (retractable) {
		assumptionVariable = sat->new_variable();
		ipasir_assume(sat->solver,assumptionVariable); // for now assumption must true as to force
	}

	// assert equality between these two states
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		const int numStates = fts->get_ts(ts).get_size();
		const vector<int> & preVar = previousStateVars[ts];
		const vector<int> & nextVar = nextStateVars[ts];
	
		for (int state = 0; state < numStates; state ++){
			if (retractable){
				sat->andImplies(assumptionVariable,preVar[state], nextVar[state]);
				sat->andImplies(assumptionVariable,nextVar[state], preVar[state]);
			} else {
				sat->implies(preVar[state], nextVar[state]);
				sat->implies(nextVar[state], preVar[state]);
			}	
		}
	}
}


void LabelBasedEncoding::encodeInit(int fromTime, bool retractable){
	// time-step for init might not exist yet
	auto initVarFind = allTimesStateVars.find(fromTime);
	const vector<vector<int>> & initStateVars = (initVarFind == allTimesStateVars.end()) ? 
		(allTimesStateVars[fromTime] = generateStateVars()):
		initVarFind->second;

	int assumptionVariable = 0;
	if (retractable) {
		assumptionVariable = sat->new_variable();
		ipasir_assume(sat->solver,assumptionVariable); // for now assumption must true as to force
	}

	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		int initVar = initStateVars[ts][fts->get_ts(ts).get_init_state()];
		if (retractable){
			sat->implies(assumptionVariable,initVar);
		} else {
			sat->assertYes(initVar);
		}
	}
}


void LabelBasedEncoding::encodeGoal(int toTime, bool retractable){
	// time-step for goal might not exist yet
	auto goalVarFind = allTimesStateVars.find(toTime);
	const vector<vector<int>> & goalStateVars = (goalVarFind == allTimesStateVars.end()) ? 
		(allTimesStateVars[toTime] = generateStateVars()):
		goalVarFind->second;

	int assumptionVariable = 0;
	if (retractable) {
		assumptionVariable = sat->new_variable();
		ipasir_assume(sat->solver,-assumptionVariable); // for now assumption must be false
	}

	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		vector<int> goals = fts->get_ts(ts).get_goal_states();
		vector<int> goalVars;
		for(int goal : goals){
			goalVars.push_back(goalStateVars[ts][goal]);
		}
		if (retractable) goalVars.push_back(assumptionVariable);
		sat->atLeastOne(goalVars);
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
		sat->atLeastOne(labelVars);
	} else if (!useSelfloopOptimisation){
		// if we don't force at least one action and we don't have self-loop optimisation, we need frame axioms
		encode_frame_axioms(previousStateVars,labelGroupVars,nextStateVars);
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
			if(ipasir_val(sat->solver, allTimesStateVars[initTime][ts][state]) > 0){
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
			if(ipasir_val(sat->solver, allTimesLabelVars[labelTimestep][label]) <= 0){
				continue;
			}else{
				selectedLabels.push_back(label);
				timesteps_with_labels.insert(labelTimestep);
				cout << "Label : " << label << endl;
				for(int ts = 0 ; ts < fts->get_size() ; ts++){
					for(size_t state = 0 ; state < allTimesStateVars[stateAfterTimestep][ts].size() ; state++){
						if(ipasir_val(sat->solver, allTimesStateVars[stateAfterTimestep][ts][state]) > 0){
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


	// manual checking of FTS plan
	for (size_t i = 0; i < labels.size(); i++){
		for(int ts = 0 ; ts < fts->get_size() ; ts++){
			int from = states[i][ts];
			int to = states[i+1][ts];
		
			const TransitionSystem & tss = fts->get_ts(ts);
			auto transitions = tss.get_transitions_with_label(labels[i]);
			bool good = false;
            for (const Transition &t: transitions) {
				if (t.src == from && t.target == to) good = true; 
			}

			if (!good){
				cout << "Execution of FTS plan failed at label nr. " << i << " being " << labels[i] << endl;
				cout << "Formula wanted to transition in ts " << ts << " from " << from << " to " << to << " but this is impossible" << endl;
				cout << "Possible transitions are: " << endl;
            	for (const Transition &t: transitions)
					cout << "\t" << t.src << " -> " << t.target << endl;
				assert(false);
			} else {
				//cout << "Execution of FTS label nr. " << i << " being " << labels[i] << endl;
				//cout << "Transition in ts " << ts << " from " << from << " to " << to << endl;
			}
		}
	}



	return make_tuple(goalState,states,labels,timesteps_with_labels);	
}

};
