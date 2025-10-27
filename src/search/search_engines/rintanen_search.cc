#include <chrono>
#include <thread>
#include <atomic>

#include "rintanen_search.h"

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

extern "C"{
	void ipasir_terminate (void * solver);

	typedef struct kissat kissat;
	void kissat_set_external_scheduler(kissat * solver, void (*scheduler) (void *));
}


struct SAT_Scheduler;



struct SAT_Call_Data{
	void * solver;
	std::shared_ptr<SAT_Scheduler> scheduler;
	std::unique_ptr<sat_search::SATEncoding> encoding;
	const int iterationNr;
	const int timesteps;
	//std::mutex run_mutex;

	SAT_Call_Data(void * _solver, const std::shared_ptr<SAT_Scheduler> & _scheduler, std::unique_ptr<sat_search::SATEncoding> _encoding,
			int _iterationNr, int _timesteps) : solver(_solver), scheduler(_scheduler), encoding(std::move(_encoding)), iterationNr(_iterationNr), timesteps(_timesteps){
		// mutex gets initialised as a member
	}

};



struct SAT_Scheduler{
	map<int, std::shared_ptr<SAT_Call_Data>> currentInstances;


	void runScheduler(){
		cout << "Hi Scheduler!" << endl;	
	}

	SAT_Scheduler(){}
};




// Call-back function to reach the scheduler. Thus must be a pure C function, as it is executed by the SAT solver.
// Functions passed to the SAT solver need to be pure C functions.
// To access data relevant to the run, we then need to access a **global** data structure. This is sadly unavoidable.
std::map<void*, std::shared_ptr<SAT_Call_Data>> sat_solver_to_data;
extern "C" {
// call-back function for the SAT solver. SAT solver provides pointer to itself to identify who it is.
void rintanen_scheduler_callback(void * solver){
	// call the actual scheduler
	sat_solver_to_data[solver]->scheduler->runScheduler();	
}

}


namespace sat_search {
RintanenSATSearch::RintanenSATSearch(const options::Options &opts): SearchEngine(opts),
	stepTimeLimit(opts.get<int>("step_time_limit")),
	continueAfterFirstPlan(opts.get<bool>("continue_after_first_plan")),
	length_strategy(opts.get<shared_ptr<LengthStrategy>>("length_strategy")),
	encoding_factory(opts.get<shared_ptr<SATEncodingFactory>>("encoder")),
	fts(g_main_task),
	stepNumber(0), currentLength (length_strategy->get_first_length()) {

	kissat_quietMode = opts.get<bool>("solver_quiet");

}

void RintanenSATSearch::initialize() {
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


SearchStatus RintanenSATSearch::step() {
    utils::Timer step_timer;
	auto t_start = std::chrono::system_clock::now();
	cout << "HI doing step! SAT: " << ipasir_signature() << endl; // << " starting at " << t_start << endl;

	std::shared_ptr<SAT_Scheduler> global_scheduler = make_shared<SAT_Scheduler>();

	void* solver = ipasir_init();
	kissat_set_external_scheduler((kissat*)solver,rintanen_scheduler_callback);
	sat_capsule capsule(solver);
	// create encoding object
	std::unique_ptr<SATEncoding> thisEncoding = encoding_factory->createEncodingInstance(capsule);


	//// scheduler information
	std::shared_ptr<SAT_Call_Data> this_call_data = make_shared<SAT_Call_Data>(solver,global_scheduler,std::move(thisEncoding),0,currentLength);
	global_scheduler->currentInstances[currentLength] = this_call_data;
	sat_solver_to_data[solver] = this_call_data;


	cout << "After Generation " << endl;
	if (this_call_data->encoding) cout << "Object" << endl; else cout << "Empty" << endl;


	std::vector<std::pair<int,int>> time_step_order; // for plan extraction
	// encode all state transitions
	for(int timestep = 1 ; timestep <= currentLength ; timestep++){
		this_call_data->encoding->encode(timestep,timestep+1);
		time_step_order.push_back({timestep,timestep+1});
	}
	this_call_data->encoding->encodeInit(1);
	this_call_data->encoding->encodeGoal(currentLength + 1);


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
		auto [goalState, states, labels, timesteps_with_labels] = this_call_data->encoding->extractSolution(1,time_step_order);
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

void RintanenSATSearch::print_statistics() const{
	statistics.print_detailed_statistics();
}

};
