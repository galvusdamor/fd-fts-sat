#include <chrono>
#include <thread>
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
    cout << "Entire preparation time until now: " << utils::g_timer << endl;
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

	shared_ptr<sat_capsule> capsule = make_shared<sat_capsule>(solver);

	// create encoding object
	auto thisEncoding = encoding_factory->createEncodingInstance(capsule);


	//vector<int> planToAssert = {8, 1260, 1389, 2596, 1825, 3334, 3485, 114, 1139, 1499, 2877, 3620, 3212, 3485, 115, 1166, 1510, 2865, 2224, 3237, 3485, 135, 1230, 1527, 2503, 3470, 3301, 3485, 2022, 2124, 3519, 3553, 20, 1190, 1400, 2458, 1753, 3260, 3485, 3655, 21, 1209, 1403, 2530, 1772, 3283, 3485, 208, 1116, 1610, 2734, 1677, 3183, 3485, 220, 1312, 1614, 2590, 1876, 3379, 3485, 3, 1174, 1383, 2548, 1739, 3244, 3485, 5, 1212, 1389, 2517, 2043, 3276, 3485, 2056}; 
	//vector<int> planToAssert = {133, 12, 158, 15, 143, 13, 168, 16, 153, 14, 190, 18, 3, 51, 4, 62, 191, 19, 110, 10, 2, 39, 0, 23, 6, 82, 8, 92, 129, 12, 1, 31, 2, 39, 182, 18, 113, 10, 8, 92, 152, 14, 7, 89, 4, 62, 182, 18, 0, 23, 8, 92, 129, 12, 121, 11, 2, 39, 141, 13, 5, 72, 3, 51, 129, 12, 1, 31, 2, 39}; 
	//vector<int> planToAssert = {24, 5, 26, 6, 22, 4, 3, 19, 0, 8, 31, 7, 2, 14, 3, 19}; 
	//cout << "Plan to Assert Length: " << planToAssert.size() << endl;

	std::vector<std::pair<int,int>> time_step_order; // for plan extraction
	// encode all state transitions
	for(int timestep = 1 ; timestep <= currentLength ; timestep++){
		thisEncoding->encode(timestep,timestep+1);
		//set<int> singleLabel = {planToAssert[timestep - 1]};
		//thisEncoding->assertLabelsAtTime(timestep,singleLabel);
		time_step_order.push_back({timestep,timestep+1});
	}
	thisEncoding->encodeInit(1,false);
	thisEncoding->encodeGoal(currentLength + 1,false);


	//DEBUG(capsule->printVariables());

	cout << "Formula has " << capsule->get_number_of_clauses() << " clauses and " << capsule->number_of_variables << " variables." << endl;

	// start calling the solver	
	int solverState;
    
	utils::Timer sat_timer;
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
	sat_timer.stop();
 
	cout << "SAT solver state: " << solverState << endl;

	if (solverState == 10){
		// run plan extraction
		auto [goalState, states, labels, timesteps_with_labels] = thisEncoding->extractSolution(1,time_step_order);
		// set the plan and run FTS extraction		
		check_goal_and_set_plan(goalState, states, std::move(labels), fts);

		ipasir_release(solver);
		
		cout << "STEP " << stepNumber << " length " << currentLength
				<< " STEP time " << step_timer << " of that SAT time " << sat_timer
				<< " clauses " << capsule->get_number_of_clauses() << " vars " << capsule->number_of_variables
				<< " labels " << labels.size() << " timesteps with label " << timesteps_with_labels.size()
				<< " compression " << double(labels.size()) / timesteps_with_labels.size()
				<< endl;
		if (!continueAfterFirstPlan)
			return SOLVED;
	} else {
		cout << "STEP " << stepNumber << " length " << currentLength
				<< " STEP time " << step_timer << " of that UNSAT time " << sat_timer
				<< " clauses " << capsule->get_number_of_clauses() << " vars " << capsule->number_of_variables
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
