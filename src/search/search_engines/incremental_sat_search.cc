#include <chrono>
#include <thread>
#include <atomic>

#include "incremental_sat_search.h"

#include "../utils/logging.h"
#include "../utils/timer.h"
#include "../sat/ipasir.h"
#include "../sat/length_strategy.h"
#include "../sat/sat_encoder.h"
#include "../task_utils/label_order_finder.h"

#include "../options/options.h"


using namespace std;
using namespace task_representation;

extern bool kissat_quietMode;

namespace sat_search {
IncrementalSATSearch::IncrementalSATSearch(const options::Options &opts): SearchEngine(opts),
	length_strategy(opts.get<shared_ptr<LengthStrategy>>("length_strategy")),
	encoding_factory(opts.get<shared_ptr<SATEncodingFactory>>("encoder")),
	inc_strategy(incremental_strategy(opts.get_enum("strategy"))),
	fts(g_main_task),
	stepNumber(0), currentLength (length_strategy->get_first_length()) {

	kissat_quietMode = opts.get<bool>("solver_quiet");

}

void IncrementalSATSearch::initialize() {
	utils::Timer sat_init_timer;
	cout << "Initialising with " << ipasir_signature() << endl;
	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

	encoding_factory->initialize();
	
	// for incremental solving, we keep one SAT solver and use it for all SAT calls
	void* solver = ipasir_init();
	capsule = make_shared<sat_capsule>(solver);
	// create encoding object
	encoding = encoding_factory->createEncodingInstance(capsule);	

	stepNumber = 0;
	currentLength = length_strategy->get_first_length();
	currentGenerated = 0;
	currentLastAfterInit = 1;
	currentFirstBeforeGoal = -1;

	// depending on the strategy, we might need to initialise init, goal or both
	if (inc_strategy == GOAL || inc_strategy == MIDDLE){
		// then init is fixed
		encoding->encodeInit(1,false);
	}

	if (inc_strategy == INIT || inc_strategy == MIDDLE){
		// then goal is fixed and we grow before it
		encoding->encodeGoal(-1,false);
	}

    cout << "SAT init time: " << sat_init_timer << endl;
}


SearchStatus IncrementalSATSearch::step() {
    utils::Timer step_timer;

	// how many time-steps are missing?
	int stepsToGenerate = currentLength - currentGenerated;
	int newStepsAfterInit;
	int newStepsBeforeGoal;

	if (inc_strategy == GOAL){
		newStepsAfterInit = stepsToGenerate;
		newStepsBeforeGoal = 0;
	} else if (inc_strategy == INIT){
		newStepsAfterInit = 0;
		newStepsBeforeGoal = stepsToGenerate;
	} else {
		assert(inc_strategy == MIDDLE);
		newStepsBeforeGoal = 0;
		newStepsAfterInit = 0;

		cout << currentGenerated << " " << stepsToGenerate << endl;
		// if current length is odd, we add the first step before the goal (as the last one would be added after init)
		if (currentGenerated % 2 == 1){
			newStepsBeforeGoal++;
			stepsToGenerate--;
		}

		if (stepsToGenerate % 2 == 1){
			newStepsAfterInit++;
			stepsToGenerate--;	
		}

		newStepsAfterInit += stepsToGenerate / 2;
		newStepsBeforeGoal += stepsToGenerate / 2;
	}

	currentGenerated += newStepsBeforeGoal + newStepsAfterInit;

	cout << "Generating " << newStepsAfterInit << " new steps after init and " << newStepsBeforeGoal << " new steps before goal" << endl;

	// generate new time-steps happening after init
	while (newStepsAfterInit --> 0){
		// add one more step after init
		encoding->encode(currentLastAfterInit,currentLastAfterInit+1);
		time_step_order_after_init.push_back({currentLastAfterInit,currentLastAfterInit+1});
		currentLastAfterInit++;		
	}

	// generate new time-steps happening before goal
	while (newStepsBeforeGoal --> 0){
		// add one more step before goal
		encoding->encode(currentFirstBeforeGoal-1,currentFirstBeforeGoal);
		time_step_order_before_goal.push_front({currentFirstBeforeGoal-1,currentFirstBeforeGoal});
		currentFirstBeforeGoal--;
	}

	if (inc_strategy == GOAL){
		// assert the goal at the current location
		encoding->encodeGoal(currentLastAfterInit,true);
	} else if (inc_strategy == INIT){
		encoding->encodeInit(currentFirstBeforeGoal,true);
	} else {
		encoding->encodeStateEquals(currentLastAfterInit,currentFirstBeforeGoal,true);
	}


	//DEBUG(capsule->printVariables());

	cout << "Formula has " << capsule->get_number_of_clauses() << " clauses and " << capsule->number_of_variables << " variables." << endl;

	// start calling the solver	
	int solverState;

	solverState = ipasir_solve(capsule->solver);
 
	cout << "SAT solver state: " << solverState << endl;

	if (solverState == 10){
		// add timesteps from the back to the front list for plan extraction
		for (auto x : time_step_order_before_goal)
			time_step_order_after_init.push_back(x);

		// determine where the initial state sits
		int initTime;
		if (inc_strategy == GOAL || inc_strategy == MIDDLE)
			initTime = 1;
		else{
			assert(inc_strategy == INIT);
			initTime = currentFirstBeforeGoal;
		}

		// run plan extraction
		auto [goalState, states, labels, timesteps_with_labels] = encoding->extractSolution(initTime,time_step_order_after_init);
		// set the plan and run FTS extraction		
		check_goal_and_set_plan(goalState, states, std::move(labels), fts);

		ipasir_release(capsule->solver);
		
		cout << "STEP " << stepNumber << " length " << currentLength
				<< " SAT time " << step_timer
				<< " clauses " << capsule->get_number_of_clauses() << " vars " << capsule->number_of_variables
				<< " labels " << labels.size() << " timesteps with label " << timesteps_with_labels.size()
				<< " compression " << double(labels.size()) / timesteps_with_labels.size()
				<< endl;
		return SOLVED;
	} else {
		cout << "STEP " << stepNumber << " length " << currentLength
				<< " UNSAT time " << step_timer
				<< " clauses " << capsule->get_number_of_clauses() << " vars " << capsule->number_of_variables
				<< endl;
	}

	stepNumber++;
	auto next_length = length_strategy->get_next_length(stepNumber, currentLength);

	if (!next_length) {
		ipasir_release(capsule->solver);
		return FAILED;
	}

	currentLength = next_length.value();
	return IN_PROGRESS;
}

void IncrementalSATSearch::print_statistics() const{
	statistics.print_detailed_statistics();
}

};
