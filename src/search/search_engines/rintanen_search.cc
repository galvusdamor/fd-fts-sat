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
	void kissat_set_external_scheduler(kissat * solver, bool (*scheduler) (void *));
}


struct SAT_Scheduler;



struct SAT_Call_Data{
	std::shared_ptr<sat_capsule> capsule;
	sat_search::RintanenSATSearch* search;
	std::shared_ptr<SAT_Scheduler> scheduler;
	std::unique_ptr<sat_search::SATEncoding> encoding;
	const int iterationNr;
	const int timesteps;
	bool terminated; // set to true if *this* call needs to be terminated
	std::mutex run_mutex;

	SAT_Call_Data(std::shared_ptr<sat_capsule> _capsule, sat_search::RintanenSATSearch* _search, const std::shared_ptr<SAT_Scheduler> & _scheduler, std::unique_ptr<sat_search::SATEncoding> _encoding,
			int _iterationNr, int _timesteps) : capsule(_capsule), search(_search), scheduler(_scheduler), encoding(std::move(_encoding)), iterationNr(_iterationNr), timesteps(_timesteps), terminated(false){
		// mutex gets initialised as a member
		// mutex starts in locked state
		run_mutex.lock();
	}

};


// To access data relevant to the run, we then need to access a **global** data structure. This is sadly unavoidable.
std::map<void*, std::shared_ptr<SAT_Call_Data>> sat_solver_to_data;


struct SAT_Scheduler{
	// maps step number to the SAT call instance of that step
	map<int, std::shared_ptr<SAT_Call_Data>> currentInstances;
	const size_t maximum_number_of_parallel_calls = 5;
	int nextStepNumber;
	int previousLength;
	std::mutex done_mutex;
	bool plannerTerminated;
	bool planFound;
	// time at which the scheduler was last called	
	std::chrono::system_clock::time_point t_last_schedule;
	std::chrono::milliseconds schedule_interval{1s};

	SAT_Scheduler() : nextStepNumber(0), previousLength(-1), plannerTerminated(false), planFound(false) {
		done_mutex.lock();
		t_last_schedule = std::chrono::system_clock::now();
	}
	
	void terminate_planner(){
		plannerTerminated = true; // planner will terminate		
		// wake up any running instance, they will terminate immediately
		for (auto& [_nr, instance]: currentInstances)
			instance->run_mutex.unlock();
		// allow the main algorithm to run
		done_mutex.unlock();
	}


	// this function gets called from within a length tread
	bool runScheduler(void * solver, bool finished, bool foundPlans){
		// check whether my time has elapsed
		if (! finished){
			auto t_now = std::chrono::system_clock::now();
			std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_last_schedule);
		
			// my time has not elapsed yet.
			if (elapsed < schedule_interval) return plannerTerminated;
		}


		cout << "Hi Scheduler! I " << solver << " am " << (finished?"":"not ") << " finished." <<
			(finished? (foundPlans?"I found a plan." : "I did not find a plan.") : "") << 
			endl;
		
		// one of three things can have happened:
		// 1. The current schedule ran out of time, then schedule the next one
		// 2. The current schedule proved UNSAT, then clean-up and schedule the next one
		// 3. The current schedule proved SAT, then we need to terminate the planner
		
		// case 3: terminate all threads and go
		if (finished && foundPlans){
			planFound = foundPlans; // memorise that we found a plan s.t. the search can return the right status code
			terminate_planner();
			return plannerTerminated;
		}

		// the call that is currently active
		std::shared_ptr<SAT_Call_Data> current_call = sat_solver_to_data[solver];
		const int current_call_step = current_call->iterationNr;

		// case 2: clean the data structures
		if (finished){
			assert(!foundPlans);
			// this run finished and proved UNSAT

			// mark all instances less then myself as terminated
			vector<int> toErase;
			for (auto& [nr, instance]: currentInstances){
				if (nr <= current_call_step) {
					toErase.push_back(nr);
					// set this instance to state terminated
					instance->terminated = true;
					// wake this instance up. It will terminate immediately.
					instance->run_mutex.unlock();
					sat_solver_to_data.erase(instance->capsule->solver);
				}
			}
			
			// remove all calls from the list of current instances 
			for (const int nr : toErase)
				currentInstances.erase(nr);
		}


		// case 1 or 2: we need to determine what the next call to be scheduled is 

		// find the next step in the list of current calls
		auto nextStep = currentInstances.upper_bound(current_call_step);
		
		
		std::shared_ptr<SAT_Call_Data> next_call; // = sat_solver_to_data[solver];

		// current step was the largest step
		if (nextStep == currentInstances.end()){
			// do we need to generate a next step?
			// TODO: needs to take memory limits into account!
			if (currentInstances.size() < maximum_number_of_parallel_calls){
				if (current_call->search->create_next_length_run(current_call->scheduler)){
					// next call was created, so this is our next call
					assert(currentInstances.size() >= 1);
					next_call = (--currentInstances.end())->second;
				} else {
					// next call could not be created -- because there is no next call
					if (currentInstances.size() == 0){
						// there are no calls left, so terminate
						terminate_planner();
						return plannerTerminated;
					} else {
						next_call = currentInstances.begin()->second;	
					}
				}
			}
		} else {
			next_call = nextStep->second;
		}

		// set the time the scheduler was invoked last to now
		t_last_schedule = std::chrono::system_clock::now();
		// wake the next call up
		next_call->run_mutex.unlock();	
		// let this call sleep	
		current_call->run_mutex.lock();

		// HI! We just work up. So we first need to check whether we got terminated!
		return plannerTerminated || current_call->terminated;
	}

};



// Call-back function to reach the scheduler. Thus must be a pure C function, as it is executed by the SAT solver.
// Functions passed to the SAT solver need to be pure C functions.
extern "C" {
// call-back function for the SAT solver. SAT solver provides pointer to itself to identify who it is.
bool rintanen_scheduler_callback(void * solver){
	// we have been deleted. Terminate immediately
	if (sat_solver_to_data.count(solver) == 0) return true; 

	// call the actual scheduler -> since we call from within the SAT solver, we are definitely not finished yet!
	return sat_solver_to_data[solver]->scheduler->runScheduler(solver, false, false);
}

}


namespace sat_search {
RintanenSATSearch::RintanenSATSearch(const options::Options &opts): SearchEngine(opts),
	length_strategy(opts.get<shared_ptr<LengthStrategy>>("length_strategy")),
	encoding_factory(opts.get<shared_ptr<SATEncodingFactory>>("encoder")),
	fts(g_main_task) {

	kissat_quietMode = opts.get<bool>("solver_quiet");

}

void RintanenSATSearch::initialize() {
	utils::Timer sat_init_timer;
	cout << "Initialising" << endl;
	cout << "My FTS task has " << fts->get_size() << " systems and " << fts->get_num_labels() << " labels." << endl;

	encoding_factory->initialize();

    cout << "SAT init time: " << sat_init_timer << endl;
}

struct length_runner {
	std::shared_ptr<SAT_Call_Data> call;

	length_runner(std::shared_ptr<SAT_Call_Data> _call) : call(_call) {}
	length_runner(length_runner const& other) : call(other.call) {}
	length_runner(length_runner&& other) : call(other.call) {}


	void operator() () {
		cout << "Starting SAT call Nr. " << call->iterationNr << " for " << call->timesteps << " timesteps." << endl;
		// try to acquire the mutex. Will cause this thread to wait until it is allowed to run
		call->run_mutex.lock();
		
		// actually create and run the encoding!
		std::vector<std::pair<int,int>> time_step_order; // for plan extraction
		// encode all state transitions
		for(int timestep = 1 ; timestep <= call->timesteps ; timestep++){
			call->encoding->encode(timestep,timestep+1);
			time_step_order.push_back({timestep,timestep+1});
			// if formula generation takes a long time, we check whether we need to call the scheduler here
			if (call->scheduler->runScheduler(call->capsule->solver,false,false)) return;
		}
		call->encoding->encodeInit(1);
		call->encoding->encodeGoal(call->timesteps + 1);

		cout << "Formula has " << call->capsule->get_number_of_clauses() << " clauses and " << call->capsule->number_of_variables << " variables." << endl;

		// start calling the solver	
		int solverState = ipasir_solve(call->capsule->solver);
		cout << "SAT solver state: " << solverState << endl;

		if (solverState == 10){
			// run plan extraction
			auto [goalState, states, labels, timesteps_with_labels] = call->encoding->extractSolution(1,time_step_order);
			// set the plan and run FTS extraction		
			call->search->check_goal_and_set_plan(goalState, states, std::move(labels), call->search->fts);

			cout << "STEP " << call->iterationNr << " length " << call->timesteps
					<< " SAT "
					<< " clauses " << call->capsule->get_number_of_clauses() << " vars " << call->capsule->number_of_variables
					<< " labels " << labels.size() << " timesteps with label " << timesteps_with_labels.size()
					<< " compression " << double(labels.size()) / timesteps_with_labels.size()
					<< endl;
			
			ipasir_release(call->capsule->solver);
			// finished and found a plan; we will terminate anyway now, so ignore the return value
			call->scheduler->runScheduler(call->capsule->solver,true,true);
		} else {
			cout << "STEP " << call->iterationNr << " length " << call->timesteps
					<< " UNSAT "
					<< " clauses " << call->capsule->get_number_of_clauses() << " vars " << call->capsule->number_of_variables
					<< endl;
			ipasir_release(call->capsule->solver);
			// finished and did not find a plan; we will terminate anyway now, so ignore the return value
			call->scheduler->runScheduler(call->capsule->solver,true,false);
		}

	}
};


bool RintanenSATSearch::create_next_length_run(std::shared_ptr<SAT_Scheduler> global_scheduler){
	// which iteration is this?
	int myStepNumber = global_scheduler->nextStepNumber;
	int previousLength = global_scheduler->previousLength;

	int currentLength;
	if (myStepNumber == 0){
		// if we are the first step, get the first length from the strategy
		currentLength = length_strategy->get_first_length();
	} else {
		// otherwise try to get the next one
		auto next_length = length_strategy->get_next_length(myStepNumber, previousLength);
		if (!next_length) return false;

		currentLength = next_length.value();
	}
	
	global_scheduler->nextStepNumber++; // increase the global next step number
	global_scheduler->previousLength = currentLength;

	cout << "Launching Step " << myStepNumber << " with length " << currentLength << endl;

	// prepare the data structures for this length
	void* solver = ipasir_init();
	kissat_set_external_scheduler((kissat*)solver,rintanen_scheduler_callback);
	shared_ptr<sat_capsule> capsule = make_shared<sat_capsule>(solver);
	// create encoding object
	std::unique_ptr<SATEncoding> thisEncoding = encoding_factory->createEncodingInstance(capsule);

	//// scheduler information
	std::shared_ptr<SAT_Call_Data> this_call_data = make_shared<SAT_Call_Data>(capsule,this,global_scheduler,std::move(thisEncoding),myStepNumber,currentLength);
	// register this new run with the scheduler
	global_scheduler->currentInstances[myStepNumber] = this_call_data;
	// memorise call-backs
	sat_solver_to_data[solver] = this_call_data;

	// start the next thread and immediately detach (thread will stop itself)
	length_runner runner(this_call_data);
	std::thread thread_for_runner(runner);
	thread_for_runner.detach();

	// next length was created
	return true;
}

SearchStatus RintanenSATSearch::step() {
    utils::Timer step_timer;
	cout << "HI doing step! SAT: " << ipasir_signature() << endl; // << " starting at " << t_start << endl;

	std::shared_ptr<SAT_Scheduler> global_scheduler = make_shared<SAT_Scheduler>();

	// create the first length run of the SAT planner and start it!
	// it will wait immediately as the mutex starts in locked state
	create_next_length_run(global_scheduler);

	// start the first run
	assert(global_scheduler->currentInstances.size() == 1);
	global_scheduler->currentInstances.begin()->second->run_mutex.unlock();

	// now wait until the global scheduler has finished its work
	global_scheduler->done_mutex.lock();

	if (global_scheduler->planFound)
		return SOLVED;
	else
		return FAILED;
}

void RintanenSATSearch::print_statistics() const{
	statistics.print_detailed_statistics();
}

};
