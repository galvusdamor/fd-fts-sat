#include <chrono>
#include <thread>
#include <ctime>
#include <atomic>

#include "sat_search.h"

#include "../utils/logging.h"
#include "../utils/timer.h"
#include "../sat/ipasir.h"
#include "../sat/length_strategy.h"
#include "../sat/sat_encoder.h"
#include "../task_utils/label_order_finder.h"

#include "../options/options.h"


using namespace std;
using namespace task_representation;

bool kissat_quietMode;

extern "C"{
	void ipasir_terminate (void * solver);
}

namespace sat_search {
SATSearch::SATSearch(const options::Options &opts): SearchEngine(opts),
	stepTimeLimit(opts.get<int>("step_time_limit")),
	continueAfterFirstPlan(opts.get<bool>("continue_after_first_plan")),
	length_strategy(opts.get<shared_ptr<LengthStrategy>>("length_strategy")),
	encoding_factory(opts.get<shared_ptr<SATEncodingFactory>>("encoder")),
	fts(g_main_task),
	stepNumber(0), currentLength (length_strategy->get_first_length()) {

	kissat_quietMode = opts.get<bool>("solver_quiet");

}

void SATSearch::initialize() {
	utils::Timer sat_init_timer;
	cout << "Initialising" << endl;
	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

	encoding_factory->initialize();
	
	stepNumber = 0;
	currentLength = length_strategy->get_first_length();

    cout << "SAT init time: " << sat_init_timer << endl;
}

struct solver_timer {
	std::atomic_bool & stop;
	void* solver; 
	int time_in_ms;
	std::chrono::system_clock::time_point t_start;
	
	solver_timer(void* _solver, int _time_in_ms,std::atomic_bool& _stop, std::chrono::system_clock::time_point _t_start) :
		stop(_stop), solver(_solver), time_in_ms(_time_in_ms), t_start(_t_start) {}
	solver_timer(solver_timer const& other) : stop(other.stop), solver(other.solver), time_in_ms(other.time_in_ms), t_start(other.t_start) {}
	solver_timer(solver_timer&& other ) : stop(other.stop), solver(other.solver), time_in_ms(other.time_in_ms), t_start(other.t_start) {}


	void operator() () {
	    std::chrono::milliseconds delay(time_in_ms);
	    while(!stop) {
	      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	      auto t_now = std::chrono::system_clock::now();
	      std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_start);
	      if (stop) break;
		  if(delay <= elapsed) {
	          ipasir_terminate(solver);
			  cout << "SAT solver exceeded time limit. Terminating." << endl;
			  return;
	      }
	    }
	}
};


SearchStatus SATSearch::step() {
    utils::Timer step_timer;
	auto t_start = std::chrono::system_clock::now();
	cout << "HI doing step! SAT: " << ipasir_signature() << endl; // << " starting at " << t_start << endl;

	void* solver = ipasir_init();

	sat_capsule capsule(solver);

	// create encoding object
	SATEncoding * thisEncoding = encoding_factory->createEncodingInstance(capsule);

	std::vector<std::pair<int,int>> time_step_order; // for plan extraction
	// encode all state transitions
	for(int timestep = 1 ; timestep <= currentLength ; timestep++){
		thisEncoding->encode(timestep,timestep+1);
		time_step_order.push_back({timestep,timestep+1});
	}
	thisEncoding->encodeInit(1);
	thisEncoding->encodeGoal(currentLength + 1);


	//DEBUG(capsule.printVariables());

	cout << "Formula has " << capsule.get_number_of_clauses() << " clauses and " << capsule.number_of_variables << " variables." << endl;

	// start calling the solver	
	int solverState;

	if (stepTimeLimit == -1){
		solverState = ipasir_solve(solver);
	} else {
		std::atomic_bool stop(false);
		solver_timer timer(solver,stepTimeLimit * 1000,stop,t_start);
	    std::thread thread_for_timer(timer);
		
		solverState = ipasir_solve(solver);
		
		// Stop it
	    timer.stop = true;
		thread_for_timer.join();
	}
 
	cout << "SAT solver state: " << solverState << endl;

	if (solverState == 10){
		// run plan extraction
		auto [goalState, states, labels, timesteps_with_labels] = thisEncoding->extractSolution(time_step_order);
		// set the plan and run FTS extraction		
		check_goal_and_set_plan(goalState, states, std::move(labels), fts);

		ipasir_release(solver);
		
		cout << "STEP " << stepNumber << " length " << currentLength
				<< " SAT time " << step_timer
				<< " clauses " << capsule.get_number_of_clauses() << " vars " << capsule.number_of_variables
				<< " labels " << labels.size() << " timesteps with label " << timesteps_with_labels.size()
				<< " compression " << double(labels.size()) / timesteps_with_labels.size()
				<< endl;
		if (!continueAfterFirstPlan)
			return SOLVED;
	} else {
		cout << "STEP " << stepNumber << " length " << currentLength
				<< " UNSAT time " << step_timer
				<< " clauses " << capsule.get_number_of_clauses() << " vars " << capsule.number_of_variables
				<< endl;
		ipasir_release(solver);
	}

	stepNumber++;
	auto next_length = length_strategy->get_next_length(stepNumber, currentLength);

	if (!next_length) {
		return FAILED;
	}

	currentLength = next_length.value();
	return IN_PROGRESS;
}

void SATSearch::print_statistics() const{
	statistics.print_detailed_statistics();
}

};
