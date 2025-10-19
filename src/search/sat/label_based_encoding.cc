#include <chrono>
#include <thread>
#include <ctime>
#include <atomic>

#include "label_based_encoding.h"

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
	bool _forceAtLeastOneAction,
	const encoding_type & _encoding,
	const std::vector<std::vector<std::vector<int>>> &_labelGroups,
	const std::vector<std::vector<std::vector<int>>> &_labelProjection,
	const std::vector<std::vector<std::set<int>>> &_empty_rows,
	const std::vector<std::vector<std::set<int>>> &_empty_cols,
	const std::vector<std::vector<std::vector<int>>> &_labelsWithEffectOnValue,
	const std::vector<std::vector<std::vector<int>>> &_empty_projected_cells_per_row,
	const std::vector<std::vector<std::vector<std::set<int>>>> &_ones_per_row
		): SATEncoding(capsule,_fts,_forceAtLeastOneAction),
	useLabelGroups(_useLabelGroups),
	useSelfloopOptimisation(_useSelfloopOptimisation),
	useEmptyRows(_useEmptyRows),
	useEmptyCols(_useEmptyCols),
	useEmptyPillars(_useEmptyPillars),
	encoding(_encoding),
	fts(_fts),
	labelGroups(_labelGroups),
	labelProjection(_labelProjection),
	empty_rows(_empty_rows),
	empty_cols(_empty_cols),
	labelsWithEffectOnValue(_labelsWithEffectOnValue),
	empty_projected_cells_per_row(_empty_projected_cells_per_row),
	ones_per_row(_ones_per_row)
{
}

void LabelBasedEncodingFactory::initialize() {
	utils::Timer sat_init_timer;
	cout << "Initialising" << fts << endl;
	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

	labelGroups.resize(fts->get_size());
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


	labelProjection.resize(fts->get_size());
	empty_rows.resize(fts->get_size());
	empty_cols.resize(fts->get_size());
	labelsWithEffectOnValue.resize(fts->get_size());
	empty_projected_cells_per_row.resize(fts->get_size());
	ones_per_row.resize(fts->get_size());
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		const TransitionSystem & tss = fts->get_ts(ts);
		set<int> values;
		labelProjection[ts] = vector<vector<int>> (fts->get_ts(ts).get_size(), vector<int>(fts->get_ts(ts).get_size(), 0));
		labelsWithEffectOnValue[ts].resize(fts->get_ts(ts).get_size());
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			values.insert(states);
			labelProjection[ts][states][states] = 1;
			for(int label = 0 ; label < fts->get_num_labels() ; label++){
				if(tss.isAlwaysSelfLoop(label)) continue;
				auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
				for(Transition t : transitions){
					if(t.target == states){
						labelsWithEffectOnValue[ts][states].push_back(label);
						break;
					}
				}
			}
		}
		empty_rows[ts].resize(labelGroups[ts].size());
		empty_cols[ts].resize(labelGroups[ts].size());
		ones_per_row[ts].resize(labelGroups[ts].size());
		for(size_t lg = 0 ; lg < labelGroups[ts].size() ; lg++){
			ones_per_row[ts][lg].resize(fts->get_ts(ts).get_size());
			if (useEmptyRows) empty_rows[ts][lg] = values;
			if (useEmptyCols) empty_cols[ts][lg] = values;
			int label = labelGroups[ts][lg][0];
			if(useSelfloopOptimisation && (tss.isIrrelevantLabel(label) || tss.isAlwaysSelfLoop(label))) continue;
			auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
			for(Transition t : transitions){
				if (useEmptyRows) empty_rows[ts][lg].erase(t.src);
				if (useEmptyCols) empty_cols[ts][lg].erase(t.target);
				ones_per_row[ts][lg][t.src].insert(t.target);
				//ones_per_column[ts][lg][t.target].insert(t.src);
				labelProjection[ts][t.src][t.target] = 1;
			}
		}

		if (useEmptyPillars){
			empty_projected_cells_per_row[ts].resize(fts->get_ts(ts).get_size());
			for(int src = 0 ; src < fts->get_ts(ts).get_size() ; src++){
				for(int target = 0 ; target < fts->get_ts(ts).get_size() ; target++){
					if(labelProjection[ts][src][target] == 0)
						empty_projected_cells_per_row[ts][src].push_back(target);
				}
			}
		}
	}
	

    cout << "SAT init time: " << sat_init_timer << endl;
}



unique_ptr<SATEncoding> LabelBasedEncodingFactory::createEncodingInstance(sat_capsule & capsule){
	return make_unique<LabelBasedEncoding>(capsule,fts,useLabelGroups,useSelfloopOptimisation,
			useEmptyRows,useEmptyCols,useEmptyPillars,forceAtLeastOneAction,encoding,
			labelGroups,labelProjection,empty_rows,empty_cols,
			labelsWithEffectOnValue, empty_projected_cells_per_row,
			ones_per_row);
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

vector<vector<int>> LabelBasedEncoding::generateLabelGroupVars(const vector<int> &labelVars/* , int timestep */) const{
	vector<vector<int>> labelGroupVars(fts->get_size());
	for(int ts = 0 ; ts < fts->get_size(); ts++){
		for(size_t lg = 0 ; lg < labelGroups[ts].size() ; lg++){
			if(labelGroups[ts][lg].size() == 1){
				labelGroupVars[ts].push_back(-1);
				continue;
			}
			int lab_group = sat.new_variable();
			DEBUG(sat.registerVariable(lab_group,"LabelGroup:"+to_string(lg)));
			labelGroupVars[ts].push_back(lab_group);
			vector<int> labels;
			for(int label : labelGroups[ts][lg]){
				sat.implies(labelVars[label], lab_group);
				labels.push_back(labelVars[label]);
			}
			sat.impliesOr(lab_group, labels);
		}
	}
	return labelGroupVars;
}

map<int, map<int, vector<int>>> LabelBasedEncoding::generateHelperVars(/* , int timestep */) const{
	map<int, map<int, vector<int>>> helperVars;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			for(size_t h = 1 ; h < labelsWithEffectOnValue[ts][states].size() ; h++){
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
			for(size_t l = 0 ; l < labelsWithEffectOnValue[ts][states].size() ; l++){
				if(l < labelsWithEffectOnValue[ts][states].size()-1){
					sat.andImplies(labelVars[labelsWithEffectOnValue[ts][states][l]], nextStateVars[ts][states], topHelperVars[ts][states][l]);
					if(!tss.hasSelfLoopOnValue(states, labelsWithEffectOnValue[ts][states][l+1])){
						sat.implies(topHelperVars[ts][states][l], -labelVars[labelsWithEffectOnValue[ts][states][l+1]]);
					}
					if(!tss.hasSelfLoopOnValue(states, labelsWithEffectOnValue[ts][states][l])){
						sat.implies(bottomHelperVars[ts][states][l], -labelVars[labelsWithEffectOnValue[ts][states][l]]);
					}
				}
				if(l > 0){
					sat.andImplies(labelVars[labelsWithEffectOnValue[ts][states][l]], nextStateVars[ts][states], bottomHelperVars[ts][states][l-1]);
				}
				if(l > 0 && l < labelsWithEffectOnValue[ts][states].size()-1){
					sat.implies(topHelperVars[ts][states][l-1], topHelperVars[ts][states][l]);
					sat.implies(bottomHelperVars[ts][states][l], bottomHelperVars[ts][states][l-1]);
				}
			}
		}
	}
}

void LabelBasedEncoding::encode_transition(const vector<vector<int>> & previousStateVars, const vector<int> & labelVars, const vector<vector<int>> & labelGroupVars, const vector<vector<int>> & nextStateVars){
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		const TransitionSystem & tss = fts->get_ts(ts);
		vector<int> labelGroupsWithActualTransitions;
		for(size_t lg = 0 ; lg < labelGroups[ts].size() ; lg++){
			int label = labelGroups[ts][lg][0];

			if(!useLabelGroups){
				if(useSelfloopOptimisation){
					if(tss.isIrrelevantLabel(label)) continue;
					if(tss.isAlwaysSelfLoop(label)){
						auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
						vector<int> preconditions;
						vector<int> effects;
						for(Transition t : transitions){
							preconditions.push_back(previousStateVars[ts][t.src]);
							effects.push_back(nextStateVars[ts][t.target]);
						}
						for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
							sat.impliesOr(labelVars[labelGroups[ts][lg][l]], preconditions);
							sat.impliesOr(labelVars[labelGroups[ts][lg][l]], effects);
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
						sat.impliesNot(labelVars[labelGroups[ts][lg][l]], previousStateVars[ts][neg_prec]);
					}
				}
				for(int neg_eff : empty_cols[ts][lg]){
					for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
						sat.impliesNot(labelVars[labelGroups[ts][lg][l]], nextStateVars[ts][neg_eff]);
					}
				}
				for(int src = 0 ; src < fts->get_ts(ts).get_size() ; src++){
					if(empty_rows[ts][lg].contains(src)) continue;
					for(int t : empty_projected_cells_per_row[ts][src]){
						sat.implies(previousStateVars[ts][src], -nextStateVars[ts][t]);
					}
					if(ones_per_row[ts][lg][src].size() == 1){
						int t = *ones_per_row[ts][lg][src].begin();
						for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
							sat.andImplies(labelVars[labelGroups[ts][lg][l]], previousStateVars[ts][src], nextStateVars[ts][t]);
						}
						continue;
					}
					for(int target = 0 ; target < fts->get_ts(ts).get_size() ; target++){
						if(empty_cols[ts][lg].contains(target)) continue;
						if(useEmptyCols && labelProjection[ts][src][target] == 0) continue;
						if(!ones_per_row[ts][lg][src].contains(target)){
							for(size_t l = 0 ; l < labelGroups[ts][lg].size() ; l++){
								sat.andImplies(labelVars[labelGroups[ts][lg][l]], previousStateVars[ts][src], -nextStateVars[ts][target]);
							}
						}
					}
				}
			}else{
				if(useSelfloopOptimisation){
					if(tss.isIrrelevantLabel(label)) continue;
					if(tss.isAlwaysSelfLoop(label)){
						auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
						vector<int> preconditions;
						vector<int> effects;
						for(Transition t : transitions){
							preconditions.push_back(previousStateVars[ts][t.src]);
							effects.push_back(nextStateVars[ts][t.target]);
						}
						if(labelGroupVars[ts][lg] == -1){
							sat.impliesOr(labelVars[labelGroups[ts][lg][0]], preconditions);
							sat.impliesOr(labelVars[labelGroups[ts][lg][0]], effects);
						}else{
							sat.impliesOr(labelGroupVars[ts][lg], preconditions);
							sat.impliesOr(labelGroupVars[ts][lg], effects);
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
						sat.impliesNot(labelVars[labelGroups[ts][lg][0]], previousStateVars[ts][neg_prec]);
					}else{
						sat.impliesNot(labelGroupVars[ts][lg], previousStateVars[ts][neg_prec]);
					}
				}
				for(int neg_eff : empty_cols[ts][lg]){
					if(labelGroupVars[ts][lg] == -1){
						sat.impliesNot(labelVars[labelGroups[ts][lg][0]], nextStateVars[ts][neg_eff]);
					}else{
						sat.impliesNot(labelGroupVars[ts][lg], nextStateVars[ts][neg_eff]);
					}
				}
				for(int src = 0 ; src < fts->get_ts(ts).get_size() ; src++){
					if(empty_rows[ts][lg].contains(src)) continue;
					for(int t : empty_projected_cells_per_row[ts][src]){
						sat.implies(previousStateVars[ts][src], -nextStateVars[ts][t]);
					}
					if(ones_per_row[ts][lg][src].size() == 1){
						int t = *ones_per_row[ts][lg][src].begin();
						if(labelGroupVars[ts][lg] == -1){
							sat.andImplies(labelVars[labelGroups[ts][lg][0]], previousStateVars[ts][src], nextStateVars[ts][t]);
							continue;
						}
						sat.andImplies(labelGroupVars[ts][lg], previousStateVars[ts][src], nextStateVars[ts][t]);
						continue;
					}
					for(int target = 0 ; target < fts->get_ts(ts).get_size() ; target++){
						if(empty_cols[ts][lg].contains(target)) continue;
						if(useEmptyRows && labelProjection[ts][src][target] == 0) continue;
						if(!ones_per_row[ts][lg][src].contains(target)){
							if(labelGroupVars[ts][lg] == -1){
								sat.andImplies(labelVars[labelGroups[ts][lg][0]], previousStateVars[ts][src], -nextStateVars[ts][target]);
							}else{
								sat.andImplies(labelGroupVars[ts][lg], previousStateVars[ts][src], -nextStateVars[ts][target]);
							}
						}
					}
				}
			}
		}

		if(useSelfloopOptimisation){
			int selfLoopAuxVar = sat.new_variable();
			DEBUG(sat.registerVariable(selfLoopAuxVar,"selfLoopAuxVar"));
			sat.impliesOr(-selfLoopAuxVar, labelGroupsWithActualTransitions);
			for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
				sat.andImplies(selfLoopAuxVar, previousStateVars[ts][states], nextStateVars[ts][states]);
				sat.andImplies(-selfLoopAuxVar, previousStateVars[ts][states], -nextStateVars[ts][states]);
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
		for(size_t goal = 0 ; goal < goals.size() ; goal++){
			goalStateVars.push_back(allTimesStateVars[toTime][ts][goals[goal]]);
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

	// label group vars are only needed if we use label vars for encoding
	const vector<vector<int>> __emptyVec;
	const vector<vector<int>> & labelGroupVars = (useLabelGroups)?
		generateLabelGroupVars(labelVars/* , int timestep */):
		__emptyVec;
	

	/// 2. Step encode the state transition.
	encode_transition(previousStateVars,labelVars,labelGroupVars,nextStateVars);

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
std::tuple<PlanState,std::vector<PlanState>,std::vector<int>,std::set<int>> LabelBasedEncoding::extractSolution(int initTime, std::vector<std::pair<int,int>> time_step_order){
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
	for(size_t o = 0 ; o < labelsPerTimestep.size() ; o++){
		for(size_t o1 = 0 ; o1 < labelsPerTimestep[o].size() ; o1++){
			labels.push_back(labelsPerTimestep[o][o1]);
		}
	}

	cout << "Total states: " << states.size() << endl;
	cout << "Total labels: " << labels.size() << endl;
	cout << "Total timesteps with label: " << timesteps_with_labels.size() << endl;

	return make_tuple(goalState,states,labels,timesteps_with_labels);	
}

};
