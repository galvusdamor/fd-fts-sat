#include <chrono>
#include <thread>
#include <atomic>
#include <semaphore>

#include "rintanen_search.h"

#include "../utils/logging.h"
#include "../utils/timer.h"
#include "../utils/markup.h"
#include "../utils/system.h"
#include "../utils/memory.h"
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
	std::binary_semaphore run_mutex;
	std::string identifier;

	SAT_Call_Data(std::shared_ptr<sat_capsule> _capsule, sat_search::RintanenSATSearch* _search, const std::shared_ptr<SAT_Scheduler> & _scheduler, std::unique_ptr<sat_search::SATEncoding> _encoding,
			int _iterationNr, int _timesteps) : capsule(_capsule), search(_search), scheduler(_scheduler), encoding(std::move(_encoding)), iterationNr(_iterationNr), timesteps(_timesteps), terminated(false), run_mutex(0){
		// mutex gets initialised as a member, and it is locked (0) initially
		identifier = "Step " + utils::pad_int(iterationNr,2) + " for " + utils::pad_int(timesteps,4) + " timesteps: ";
	}

};


// To access data relevant to the run, we then need to access a **global** data structure. This is sadly unavoidable.
std::map<void*, std::shared_ptr<SAT_Call_Data>> sat_solver_to_data;


struct SAT_Scheduler{
	const size_t maximum_number_of_parallel_calls;
	std::chrono::milliseconds schedule_interval;
	const int memory_limit_mbs;

	// maps step number to the SAT call instance of that step
	map<int, std::shared_ptr<SAT_Call_Data>> currentInstances;
	int nextStepNumber;
	int previousLength;
	std::binary_semaphore done_mutex;
	bool plannerTerminated;
	bool planFound;
	// time at which the scheduler was last called	
	std::chrono::system_clock::time_point t_last_schedule;
	
	// store an educated guess on memory usage
	int educated_guess_memory_in_mb;
	int reserved_memory_in_mb;
	

	SAT_Scheduler(size_t max_parallel_calls, int scheduler_interval_seconds, int _memory_limit_mbs) :
		maximum_number_of_parallel_calls(max_parallel_calls),
		schedule_interval(scheduler_interval_seconds * 1000),
		memory_limit_mbs(_memory_limit_mbs),
		nextStepNumber(0), previousLength(-1), done_mutex(0), plannerTerminated(false), planFound(false) {
		// done_mutex is is initially locked
		t_last_schedule = std::chrono::system_clock::now();
		educated_guess_memory_in_mb = -1; // we don't have one yet.
		reserved_memory_in_mb = -1;
		//educated_guess_memory_in_mb = 1024; // fixed value as implementation does not work
		//utils::reserve_extra_memory_padding(educated_guess_memory_in_mb);
	}
	
	void terminate_planner(){
		plannerTerminated = true; // planner will terminate		
		// wake up any running instance, they will terminate immediately
		for (auto& [_nr, instance]: currentInstances)
			instance->run_mutex.release();
		// allow the main algorithm to run
		done_mutex.release();
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

		// the call that is currently active
		std::shared_ptr<SAT_Call_Data> current_call = sat_solver_to_data[solver];
		const int current_call_step = current_call->iterationNr;

		//cout << "Hi Scheduler! I " << solver << " am " << (finished?"":"not ") << "finished." <<
		//	(finished? (foundPlans?" I found a plan." : " I did not find a plan.") : "") << 
		//	endl;
		
		// one of three things can have happened:
		// 1. The current schedule ran out of time, then schedule the next one
		// 2. The current schedule proved UNSAT, then clean-up and schedule the next one
		// 3. The current schedule proved SAT, then we need to terminate the planner
		
		// case 3: terminate all threads and go
		if (finished && foundPlans){
			// reporting already done by thread
			//cout << current_call->identifier << "found plan" << endl;
			planFound = foundPlans; // memorise that we found a plan s.t. the search can return the right status code
			terminate_planner();
			return plannerTerminated;
		}
		

		// case 2: clean the data structures
		if (finished){
			// reporting already done by thread
			//cout << current_call->identifier << "UNSAT" << endl;
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
					instance->run_mutex.release();
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
			if (currentInstances.size() < maximum_number_of_parallel_calls &&
					current_call->search->create_next_length_run(current_call->scheduler)){
				// next call was created, so this is our next call
				assert(currentInstances.size() >= 1);
				next_call = (--currentInstances.end())->second;
			} else {
				// next call could not or should not be created -- because there is no next call
				if (currentInstances.size() == 0){
					// there are no calls left, so terminate
					terminate_planner();
					return plannerTerminated;
				} else {
					next_call = currentInstances.begin()->second;	
				}
			}
		} else {
			next_call = nextStep->second;
		}

		// set the time the scheduler was invoked last to now
		t_last_schedule = std::chrono::system_clock::now();
		// wake the next call up
		next_call->run_mutex.release();	
		// let this call sleep	
		current_call->run_mutex.acquire();


		// if we exceeded our expectation, we need to mark more memory as taken.
		int current_memory = std::ceil(utils::get_peak_memory_in_kb() / 1024);
		if (current_memory > reserved_memory_in_mb) reserved_memory_in_mb = current_memory;

		//cout << current_call->identifier << "run" << endl;

		// HI! We just work up. So we first need to check whether we got terminated!
		return plannerTerminated || current_call->terminated;
	}

};



// Call-back function to reach the scheduler. Thus must be a pure C function, as it is executed by the SAT solver.
// Functions passed to the SAT solver need to be pure C functions.
extern "C" {
// call-back function for the SAT solver. SAT solver provides pointer to itself to identify who it is.
bool rintanen_scheduler_callback(void * solver){
	// call the actual scheduler -> since we call from within the SAT solver, we are definitely not finished yet!
	return sat_solver_to_data[solver]->scheduler->runScheduler(solver, false, false);
}

}


namespace sat_search {
RintanenSATSearch::RintanenSATSearch(const options::Options &opts): SearchEngine(opts),
	encoding_factory(opts.get<shared_ptr<SATEncodingFactory>>("encoder")),
	max_parallel_calls(size_t(opts.get<int>("max_parallel_calls"))),
	scheduler_interval_seconds(opts.get<int>("scheduler_interval")),
	memory_limit_mbs(opts.get<int>("memory_limit_mb")),
	fts(g_main_task),
	length_strategy(opts.get<shared_ptr<LengthStrategy>>("length_strategy")) {

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
		cout << call->identifier << "started" << endl;
		// try to acquire the mutex. Will cause this thread to wait until it is allowed to run
		call->run_mutex.acquire();
		int memory_before_formula = utils::get_peak_memory_in_kb();
		
		// actually create and run the encoding!
		std::vector<std::pair<int,int>> time_step_order; // for plan extraction
		// encode all state transitions
		for(int timestep = 1 ; timestep <= call->timesteps ; timestep++){
			call->encoding->encode(timestep,timestep+1);
			time_step_order.push_back({timestep,timestep+1});
			
			// if formula generation takes a long time, we check whether we need to call the scheduler here
			// the first generations need to happen uninterrupted (to measure memory usage)
			if (call->iterationNr > 0 || call->scheduler->educated_guess_memory_in_mb == -1){
				if (call->scheduler->runScheduler(call->capsule->solver,false,false)) return;
			}
		}
		call->encoding->encodeInit(1);
		call->encoding->encodeGoal(call->timesteps + 1);

		int memory_after_formula = utils::get_peak_memory_in_kb();
		int memory_usage = memory_after_formula - memory_before_formula;
		if (call->scheduler->educated_guess_memory_in_mb == -1 && memory_usage != 0){
			int mbs_per_timestep = std::ceil(double(memory_usage) / call->timesteps / 1024) * 3; // safety factor; 1000 is for kb in mb
			cout << call->identifier << "estimated memory usage: " << mbs_per_timestep << endl;
			call->scheduler->educated_guess_memory_in_mb = mbs_per_timestep;
			call->scheduler->reserved_memory_in_mb = std::ceil(double(memory_after_formula) / 1024) + 500; // 50 MB of buffer

			//auto nextLengthOpt = call->search->length_strategy->get_next_length(call->scheduler->nextStepNumber,call->scheduler->previousLength);
			//if (nextLengthOpt){
			//	int nextLength = nextLengthOpt.value();
			//	int mbs_for_next_call = call->scheduler->educated_guess_memory_in_mb * nextLength;
			//	cout << "Reserving " << mbs_for_next_call << " for " << nextLength << endl;
			//	utils::maybe_reserve_extra_memory_padding(mbs_for_next_call);
			//}
		}


		cout << call->identifier << call->capsule->get_number_of_clauses() << " clauses " << call->capsule->number_of_variables << " variables" << endl;
		cout << call->identifier << "Currently reserved: " << call->scheduler->reserved_memory_in_mb << " MB. Actual: " <<  std::ceil(double(memory_after_formula) / 1024) << endl;


		// start calling the solver	
		int solverState = ipasir_solve(call->capsule->solver);
		//cout << call->identifier << "SAT solver state: " << solverState << endl;

		if (solverState == 10){
			// run plan extraction
			auto [goalState, states, labels, timesteps_with_labels] = call->encoding->extractSolution(1,time_step_order);
			// set the plan and run FTS extraction		
			call->search->check_goal_and_set_plan(goalState, states, std::move(labels), call->search->fts);

			cout << call->identifier
					<< "SAT"
					<< " clauses " << call->capsule->get_number_of_clauses() << " vars " << call->capsule->number_of_variables
					<< " labels " << labels.size() << " timesteps with label " << timesteps_with_labels.size()
					<< " compression " << double(labels.size()) / timesteps_with_labels.size()
					<< endl;
			
			ipasir_release(call->capsule->solver);
			// finished and found a plan; we will terminate anyway now, so ignore the return value
			call->scheduler->runScheduler(call->capsule->solver,true,true);
		} else {
			cout << call->identifier
					<< "UNSAT"
					<< " clauses " << call->capsule->get_number_of_clauses() << " vars " << call->capsule->number_of_variables
					<< endl;
			ipasir_release(call->capsule->solver);
			// finished and did not find a plan; we will terminate anyway now, so ignore the return value
			call->scheduler->runScheduler(call->capsule->solver,true,false);
		}

		if (call->scheduler->educated_guess_memory_in_mb != -1){
			// release my memory for scheduler
			int this_needed_memory = call->scheduler->educated_guess_memory_in_mb * call->timesteps;

			call->scheduler->reserved_memory_in_mb -= this_needed_memory;
			cout << call->identifier << "freeing " << this_needed_memory << " MB. Currently reserved: " << call->scheduler->reserved_memory_in_mb<< endl;
		}


		// erase myself from the map
		sat_solver_to_data.erase(call->capsule->solver);
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

	// guesstimate whether this will exhaust the memory limit
	if (global_scheduler->educated_guess_memory_in_mb != -1){
		// if we have a guess on the per-step memory usage use it.
		
		string identifier = "Step " + utils::pad_int(myStepNumber,2) + " for " + utils::pad_int(currentLength,4) + " timesteps: ";
		// how much memory is needed for this call?
		int this_needed_memory = global_scheduler->educated_guess_memory_in_mb * currentLength;

		if (global_scheduler->reserved_memory_in_mb + this_needed_memory > global_scheduler->memory_limit_mbs){
			int current_memory = utils::get_peak_memory_in_kb();
			cout << identifier << "not generating due to memory limit. Reserved " << global_scheduler->reserved_memory_in_mb << "MB. Actual " << std::ceil(double(current_memory) / 1024) << "MB" << endl;
			return false;
		}

		global_scheduler->reserved_memory_in_mb += this_needed_memory;
		cout << identifier << "reserved " << this_needed_memory << " MB. Currently reserved: " << global_scheduler->reserved_memory_in_mb<< endl;

		//// if the extra memory padding is gone, we likely cannot generate this formula any more
		//if (!utils::extra_memory_padding_is_reserved()){
		//	cout << identifier << "not generating due to memory limit" << endl;
		//	return false;
		//}

		//// release the current memory padding which should be enough for me
		//utils::release_extra_memory_padding();

		//// memory padding needed for next call
		//auto nextLengthOpt = length_strategy->get_next_length(myStepNumber+1,currentLength);
		//if (nextLengthOpt){
		//	int nextLength = nextLengthOpt.value();
		//	int mbs_for_next_call = global_scheduler->educated_guess_memory_in_mb * nextLength;
		//	cout << identifier << "reserving " << mbs_for_next_call << " MiB for " << nextLength << endl;
		//	utils::maybe_reserve_extra_memory_padding(mbs_for_next_call);
		//}
	}
	
	global_scheduler->nextStepNumber++; // increase the global next step number
	global_scheduler->previousLength = currentLength;

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
	cout << "Starting Rintanen's algorithms C with SAT solver " << ipasir_signature() << endl;

	std::shared_ptr<SAT_Scheduler> global_scheduler = make_shared<SAT_Scheduler>(max_parallel_calls,scheduler_interval_seconds, memory_limit_mbs);

	// create the first length run of the SAT planner and start it!
	// it will wait immediately as the mutex starts in locked state
	create_next_length_run(global_scheduler);

	// start the first run
	assert(global_scheduler->currentInstances.size() == 1);
	global_scheduler->currentInstances.begin()->second->run_mutex.release();

	// now wait until the global scheduler has finished its work
	global_scheduler->done_mutex.acquire();

	if (global_scheduler->planFound)
		return SOLVED;
	else
		return FAILED;
}

void RintanenSATSearch::print_statistics() const{
	statistics.print_detailed_statistics();
}

};
