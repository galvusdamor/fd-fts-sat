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

CommonEncoding::CommonEncoding(
	std::shared_ptr<sat_capsule> capsule,
	const std::shared_ptr<FTSTask> & _fts,
    bool _forceAtLeastOneAction, 
	bool _useLabelGroups,
	bool _useSelfloopOptimisation): LabelEncoding(capsule,_fts, _forceAtLeastOneAction, _useLabelGroups, _useSelfloopOptimisation)
{
}


void CommonEncoding::encode(int fromTime, int toTime){
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

    generateAdditionalVariables(fromTime);

	// 2. Step encode at least one action constraint if necessary	
	if (forceAtLeastOneAction)sat->atLeastOne(labelVars);
    
    // 3. frame axioms - encoding decides if necessary
    encode_frame_axioms(previousStateVars,nextStateVars, fromTime);
    
	// 4. Step encode the state transition.
	encode_transition(previousStateVars,nextStateVars,fromTime);


}

// run plan extraction
std::tuple<PlanState,std::vector<PlanState>,std::vector<int>,std::set<int>> CommonEncoding::extractSolution(int initTime,
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
				if (selectedLabels.size() == 1){
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
		}

		// intermediate states
		// code in this if is dependent on encode. Rest is common to all encodings
		if(selectedLabels.size() > 1){
			vector<vector<int>> intermediateStates = extractIntermediateStates(selectedLabels, statesPerTimestep.back(), stateReconstructor, labelTimestep);
			for(vector<int> & state : intermediateStates){
				statesPerTimestep.push_back(state);
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
