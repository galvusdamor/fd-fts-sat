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
	oneEncodingThreshold(size_t(opts.get<int>("one_encoding_threshold"))),
	oneEncodingThresholdPercent(opts.get<int>("one_encoding_threshold_percent")),
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

	parser.add_option<int>(
    	"one_encoding_threshold",
    	"if ones in src/target/label is less or equal to this number, use positive encoding instead of negative one",
    	"1");

	parser.add_option<int>(
    	"one_encoding_threshold_percent",
    	"if percentage of ones in src/target/label is less or equal to this number, use positive encoding instead of negative one",
    	"0");

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
	size_t _oneEncodingThreshold,
	int _oneEncodingThresholdPercent,
	const encoding_type & _encoding,
	const std::vector<shared_ptr<FTSMatrix>> & fts_matrix): CommonEncoding(capsule,_fts, forceAtLeastOneAction, _useLabelGroups, _useSelfloopOptimisation),
	statisticsPrinted(_statisticsPrinted),
	useEmptyRows(_useEmptyRows),
	useEmptyCols(_useEmptyCols),
	useEmptyPillars(_useEmptyPillars),
	useOnesInLastDimension(_useOnesInLastDimension),
	usePositiveOneForEmpty(_usePositiveOneForEmpty),
	oneEncodingThreshold(_oneEncodingThreshold),
	oneEncodingThresholdPercent(_oneEncodingThresholdPercent),
	encoding(_encoding),
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
			useEmptyRows,useEmptyCols,useEmptyPillars,useOnesInLastDimension,usePositiveOneForEmpty,forceAtLeastOneAction,
			oneEncodingThreshold,oneEncodingThresholdPercent,
			encoding,fts_matrices);
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

/* void LabelBasedEncoding::encode_chains_parallel(const vector<int> & labelVars, const vector<vector<int>> & nextStateVars){
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
} */

void LabelBasedEncoding::encode_chains_parallel(const vector<int> & labelVars, const vector<vector<int>> & nextStateVars){
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		const TransitionSystem & tss = fts->get_ts(ts);
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){

			const auto& labelsWithEffect = fts_matrices[ts]->get_not_always_selfloop_labels_reaching_target(states);
            const size_t N = labelsWithEffect.size();
            if(N < 2) continue; // chains need at least 2 labels

            // Each time-step has one SAT variable: the label var
            vector<vector<int>> eventVars(N);
            for(size_t l = 0; l < N; l++)
                eventVars[l] = {labelVars[labelsWithEffect[l]]};

            // requirers[l]: label l can be forbidden by a prior aux var
            // — only when it has no self-loop on this state
            vector<set<int>> requirers(N);
            for(size_t l = 0; l < N; l++)
                if(!tss.hasSelfLoopOnValue(states, labelsWithEffect[l]))
                    requirers[l].insert(0);

            // opposers[l]: all labels activate the chain
            vector<set<int>> opposers(N);
            for(size_t l = 0; l < N; l++)
                opposers[l].insert(0);

            sat->compute_guarded_forall_chains(opposers, requirers, eventVars, nextStateVars[ts][states]);

		}
	}
}

bool LabelBasedEncoding::is_below_threshold(int ts, size_t ones_to_consider){
	if (ones_to_consider <= oneEncodingThreshold) return true;
	
	// use integer math here.
	const int numStates = fts->get_ts(ts).get_size();
	if (int(ones_to_consider) * 100 <= numStates * oneEncodingThresholdPercent) return true;
	return false;	
}


void LabelBasedEncoding::encode_transition_semantics(const vector<vector<int>> & previousStateVars,
	const vector<vector<vector<int>>> & labelGroupVars, const int someLabelExecutedVar, const vector<vector<int>> & nextStateVars){

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

	if (forceAtLeastOneAction || useSelfloopOptimisation) assert(someLabelExecutedVar == 0);
	else assert(someLabelExecutedVar > 0);
	// no label was executed it (\neg some label was executed)
	
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		const auto &fts_matrix = fts_matrices[ts];
		const TransitionSystem & tss = fts->get_ts(ts);
		const int numLabelGroups = fts_matrix->get_num_label_groups();
		const int numStates = fts->get_ts(ts).get_size();

		int executedNonSelfLoopLabel = 0; // for later use

		// 1. Step: if desired, handle self-loops separately
		if(useSelfloopOptimisation) {
			vector<int> labelGroupsWithActualTransitions;
			
			for(int lg = 0 ; lg < numLabelGroups ; lg++) {
				// representative label of this group
				int label = fts_matrix->get_labels_in_label_group(lg)[0];

				if(tss.isIrrelevantLabel(label)) continue;
				if(tss.isAlwaysSelfLoop(label)){
					auto transitions = tss.get_transitions_with_label(label);
				
					// self-loop transitions force conditions on the previous state and next state
					// Semantics: these always self-loop-vars are executed either before or after the "main" transition 
					vector<int> preconditions;
					vector<int> effects;
					for(Transition t : transitions){
						assert(t.src == t.target); // must be a self-loop
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
				for(const int var : labelGroupVars[ts][lg])
					labelGroupsWithActualTransitions.push_back(var);
			}
			// frame axioms for self-loops. If no label with an actual transition was executed, enforce frame axiom
			int executedTransitionIsSelfLoop = sat->new_variable();
			DEBUG(sat->registerVariable(executedTransitionIsSelfLoop,"executedTransitionIsSelfLoop@" + to_string(ts)));
			sat->impliesOr(-executedTransitionIsSelfLoop, labelGroupsWithActualTransitions);
			// if we are in sequential encoding *and*


			// Note: these clauses are equivalent to: 
			// s + p -> n = -s v -p v n = p & -n -> -s
			// -s + p -> -n = s v -p v -n = p & n -> s
			// Since one of the p's is always true, they *force* s to be true of we remain in the same state.
			for(int states = 0 ; states < numStates ; states++){
				sat->andImplies(executedTransitionIsSelfLoop, previousStateVars[ts][states], nextStateVars[ts][states]);
				sat->andImplies(-executedTransitionIsSelfLoop, previousStateVars[ts][states], -nextStateVars[ts][states]);
			}

			// create a variable that is true if and only if we executed one label has is not always a self-loop
			if (usePositiveOneForEmpty){
				executedNonSelfLoopLabel = sat->new_variable();
				DEBUG(sat->registerVariable(executedNonSelfLoopLabel,"executedNonSelfLoopLabel@" + to_string(ts)));
				sat->impliesOr(executedNonSelfLoopLabel,labelGroupsWithActualTransitions);
				sat->orImplies(labelGroupsWithActualTransitions,executedNonSelfLoopLabel);
				// connection between vars (might help SAT solver)
				sat->implies(-executedTransitionIsSelfLoop, executedNonSelfLoopLabel);
			}
		} else {
			// if we don't optimise self-loops, we did execute a "non-self-loop" label, if we executed some label
			// as here we treat all labels as if they were non-self-loop  labels
			executedNonSelfLoopLabel = someLabelExecutedVar;
		}

		int executedNoActualTransition = -executedNonSelfLoopLabel;


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

					if (is_below_threshold(ts,fts_matrix->get_possible_sources_for_label(lg).size())){
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
					if (is_below_threshold(ts,size_t(fts_matrix->get_possible_labels_for_source(src).size() - self_loop_deduction))){	
					
						// which labels can be executed in this source?
						vector<int> allOnes;
						for (int lg : fts_matrix->get_possible_labels_for_source(src)){
							// ignore labels that are always self-loops if we optimised them out
							if (useSelfloopOptimisation && fts_matrix->get_always_self_loop_labels_for_state(src).contains(lg)) continue;
							for (const int l : labelGroupVars[ts][lg])
								allOnes.push_back(l);
						}

						// allOnes now contains the labelVars for all non-always-self-loop labels that can transition from src
						// But: it could be that
						// (1) we want to execute only always-self-loop labels or
						// (2) we want to execute *no* label at all (if allowed)
						// this means the other option is that we do not execute any non-always-self-loop label
						if (forceAtLeastOneAction == false || useSelfloopOptimisation == true)
							allOnes.push_back(executedNoActualTransition);

						if (encoding == SEQUENTIAL || encoding == SELF_LOOP_PARALLEL)
							for (int lg : fts_matrix->get_impossible_labels_for_source(src)) label_covered_sources[lg].insert(src);
						// if encoding == CHAINS_PARALLEL, the parallelism restriction does not force at most one non-always-self-loop label 
						// to be executed. Thus the disjunction over the actual transitions does not
	
						sat->impliesOr(previousStateVars[ts][src], allOnes);
						cnt_1_source_label++;
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

					if (is_below_threshold(ts,fts_matrix->get_possible_targets_for_label(lg).size())){
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
					if (is_below_threshold(ts,size_t(fts_matrix->get_possible_labels_for_target(target).size() - self_loop_deduction))){
						
						// which labels can be executed in this target?
						vector<int> allOnes;
						for (int lg : fts_matrix->get_possible_labels_for_target(target)){
							// ignore labels that are always self-loops if we optimised them out
							if (useSelfloopOptimisation && fts_matrix->get_always_self_loop_labels_for_state(target).contains(lg)) continue;
							for (const int l : labelGroupVars[ts][lg]){
								allOnes.push_back(l);
							}
						}

						// allOnes now contains the labelVars for all non-always-self-loop labels that can transition from src
						// But: it could be that
						// (1) we want to execute only always-self-loop labels or
						// (2) we want to execute *no* label at all (if allowed)
						// this means the other option is that we do not execute any non-always-self-loop label
						if (forceAtLeastOneAction == false || useSelfloopOptimisation == true)
							allOnes.push_back(executedNoActualTransition);
				
						if (encoding == SEQUENTIAL || encoding == SELF_LOOP_PARALLEL)
							for (int lg : fts_matrix->get_impossible_labels_for_target(target)) label_covered_targets[lg].insert(target);
					
						sat->impliesOr(nextStateVars[ts][target], allOnes);
						cnt_1_target_label++;
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
					
					if (is_below_threshold(ts,size_t(fts_matrix->get_possible_targets_for_source(src).size() - self_loop_deduction))){
						vector<int> allOnes;
						for (int target : fts_matrix->get_possible_targets_for_source(src))
							allOnes.push_back(nextStateVars[ts][target]);

						// If there is no self-loop we can also stay in the state by not executing any action
						// The fact that we have to stay in the same state is encoded by other means: either frame axioms or self-loop
						if (self_loop_deduction == 0 && forceAtLeastOneAction == false)
							allOnes.push_back(executedNoActualTransition);

						sat->impliesOr(previousStateVars[ts][src], allOnes);
						cnt_1_source_target++;
						for(int target : fts_matrix->get_impossible_targets_for_source(src)) source_covered_targets[src].insert(target);
					}
				}

				for(int target = 0 ; target < numStates ; target++){
					// if the state has a self-loop, we accept one more possible predecessor.
					int self_loop_deduction = 0;
					if (fts_matrix->get_self_loop_labels_for_state(target).size() >= 1) self_loop_deduction = 1;
					
					if (is_below_threshold(ts,size_t(fts_matrix->get_possible_sources_for_target(target).size() - self_loop_deduction))){
						vector<int> allOnes;
						for (int src : fts_matrix->get_possible_sources_for_target(target))
							allOnes.push_back(previousStateVars[ts][src]);
						
						// If there is no self-loop we can also stay in the state by not executing any action
						// The fact that we have to stay in the same state is encoded by other means: either frame axioms or self-loop
						if (self_loop_deduction == 0 && forceAtLeastOneAction == false)
							allOnes.push_back(executedNoActualTransition);

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


void LabelBasedEncoding::encode_frame_axioms(const vector<vector<int>> & previousStateVars, const vector<vector<int>> & nextStateVars, int fromTime){
	// if use self-loop optimisation frame axioms are unnecessary, as we already have the executedTransitionIsSelfLoop variable
	if (forceAtLeastOneAction) return;
	if (useSelfloopOptimisation) return;

	int someLabelExecuted = someLabelExecutedPerTime[fromTime];
	sat->impliesOr(someLabelExecuted, allTimesLabelVars[fromTime]);
	sat->orImplies(allTimesLabelVars[fromTime], someLabelExecuted);
	
	// if we did not stay in the same state, one non-self-loop label had to be executed
	// ensure that if the state changes *some* action is executed.
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		const int numStates = fts->get_ts(ts).get_size();
		
		// frame axioms. If label was executed, enforce frame axiom
		for(int states = 0 ; states < numStates ; states++){
			// write both flanks TODO: one is technically sufficient
			sat->andImplies(previousStateVars[ts][states], -nextStateVars[ts][states], someLabelExecuted);
			sat->andImplies(-previousStateVars[ts][states], nextStateVars[ts][states], someLabelExecuted);
		}
	}

}


void LabelBasedEncoding::generateAdditionalVariables(int fromTime/*, int toTime*/){
	// label group vars: one variable if we useSelfloopOptimisation, otherwise all variables for all labels of each group
	allTimesLabelGroupVars[fromTime] = generateLabelGroupVars(allTimesLabelVars[fromTime]);

	if (forceAtLeastOneAction == false && useSelfloopOptimisation == false){
		// variable is used by frame axioms and the transition semantics
		int someLabelExecuted = sat->new_variable();
		someLabelExecutedPerTime[fromTime] = someLabelExecuted;
		DEBUG(sat->registerVariable(someLabelExecuted,"someLabelExecuted"));
	}
}


void LabelBasedEncoding::encode_transition(const vector<vector<int>> & previousStateVars, const vector<vector<int>> & nextStateVars, int fromTime/*, int toTime*/){
	int someLabelExecutedVar = someLabelExecutedPerTime[fromTime];
	const vector<vector<vector<int>>> & labelGroupVars = allTimesLabelGroupVars[fromTime];
	encode_transition_semantics(previousStateVars,labelGroupVars,someLabelExecutedVar,nextStateVars);

	// 4. Step encode the restrictions on which actions are allowed in parallel as per the encoding
	if(encoding == SEQUENTIAL)
		encode_sequential(allTimesLabelVars[fromTime]);
	else if(encoding == SELF_LOOP_PARALLEL)
		encode_self_loop_parallel(allTimesLabelVars[fromTime]);
	else if(encoding == CHAINS_PARALLEL)
		encode_chains_parallel(allTimesLabelVars[fromTime], nextStateVars);

}



std::vector<std::vector<int>> LabelBasedEncoding::extractIntermediateStates(std::vector<int> & selectedLabels, std::vector<int> & currentLastState, std::vector<int> & nextState, int /*labelTimestep*/){
	vector<vector<int>> intermediateStates;
	for(size_t l = 0 ; l < selectedLabels.size() - 1 ; l++){
		vector<int> intermediateState;
		for(int ts = 0 ; ts < fts->get_size() ; ts++){
			const TransitionSystem & tss = fts->get_ts(ts);
			if(tss.isAlwaysSelfLoop(selectedLabels[l])){
				if (intermediateStates.size() == 0)
					intermediateState.push_back(currentLastState[ts]);
				else
					intermediateState.push_back(intermediateStates.back()[ts]);
			}else{
				intermediateState.push_back(nextState[ts]);
			}
		}
		intermediateStates.push_back(intermediateState);
	}

	return intermediateStates;
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
				if (selectedLabels.size() == 1)
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
		// code in this if is dependent on encode. Rest is common to all encodings
		if(selectedLabels.size() > 1){
			vector<vector<int>> intermediateStates = extractIntermediateStates(selectedLabels, statesPerTimestep.back(), stateReconstructor, labelTimestep);
			for(vector<int> & state : intermediateStates)
				statesPerTimestep.push_back(state);
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
