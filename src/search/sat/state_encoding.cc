#include <chrono>
#include <thread>
#include <ctime>
#include <atomic>

#include "state_encoding.h"

#include "../utils/logging.h"
#include "../utils/timer.h"
#include "ipasir.h"
#include "sat_encoder.h"


using namespace std;
using namespace task_representation;


namespace sat_search {

StateEncoding::StateEncoding(
	std::shared_ptr<sat_capsule> capsule,
	const std::shared_ptr<FTSTask> & _fts,
    bool _forceAtLeastOneAction): SATEncoding(capsule,_fts, _forceAtLeastOneAction)
{
}


vector<vector<int>> StateEncoding::generateStateVars(/* , int timestep */) const {
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



void StateEncoding::encodeStateEquals(int fromTime, int toTime, bool retractable){
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


void StateEncoding::encodeInit(int fromTime, bool retractable){
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


void StateEncoding::encodeGoal(int toTime, bool retractable){
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

};
