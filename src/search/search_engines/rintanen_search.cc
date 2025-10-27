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
	sat_capsule capsule;
	std::shared_ptr<SAT_Scheduler> scheduler;
	std::unique_ptr<sat_search::SATEncoding> encoding;
	const int iterationNr;
	const int timesteps;
	std::mutex run_mutex;

	SAT_Call_Data(sat_capsule _capsule, const std::shared_ptr<SAT_Scheduler> & _scheduler, std::unique_ptr<sat_search::SATEncoding> _encoding,
			int _iterationNr, int _timesteps) : capsule(_capsule), scheduler(_scheduler), encoding(std::move(_encoding)), iterationNr(_iterationNr), timesteps(_timesteps){
		// mutex gets initialised as a member
		// mutex starts in locked state
		run_mutex.lock();
	}

};



struct SAT_Scheduler{
	map<int, std::shared_ptr<SAT_Call_Data>> currentInstances;
	int nextStepNumber;
	int previousLength;
	std::mutex done_mutex;
	bool planFound;

	void runScheduler(void * solver, bool finished, bool foundPlans){
		cout << "Hi Scheduler! I " << solver << " am " << (finished?"":"not ") << " finished." <<
			(finished? (foundPlans?"I found a plan." : "I did not find a plan.") : "") << 
			endl;	
	}

	SAT_Scheduler() : nextStepNumber(0), previousLength(-1), planFound(false) {
		done_mutex.lock();
	}
};



// Call-back function to reach the scheduler. Thus must be a pure C function, as it is executed by the SAT solver.
// Functions passed to the SAT solver need to be pure C functions.
// To access data relevant to the run, we then need to access a **global** data structure. This is sadly unavoidable.
std::map<void*, std::shared_ptr<SAT_Call_Data>> sat_solver_to_data;
extern "C" {
// call-back function for the SAT solver. SAT solver provides pointer to itself to identify who it is.
void rintanen_scheduler_callback(void * solver){
	// call the actual scheduler -> since we call from within the SAT solver, we are definitely not finished yet!
	sat_solver_to_data[solver]->scheduler->runScheduler(solver, false, false);
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
	//std::chrono::system_clock::time_point t_start;
	std::shared_ptr<SAT_Call_Data> call;
	std::shared_ptr<task_representation::FTSTask> fts;
	std::shared_ptr<RintanenSATSearch> search;

	length_runner(std::shared_ptr<SAT_Call_Data> _call, std::shared_ptr<task_representation::FTSTask> _fts, std::shared_ptr<RintanenSATSearch> _search) : call(_call), fts(_fts), search(_search) {}
	length_runner(length_runner const& other) : call(other.call), fts(other.fts), search(other.search) {}
	length_runner(length_runner&& other ) : call(other.call), fts(other.fts), search(other.search) {}


	void operator() () {
		// try to acquire the mutex. Will cause this thread to wait until it is allowed to run
		call->run_mutex.lock();
		
		// actually create and run the encoding!
		std::vector<std::pair<int,int>> time_step_order; // for plan extraction
		// encode all state transitions
		for(int timestep = 1 ; timestep <= call->timesteps ; timestep++){
			call->encoding->encode(timestep,timestep+1);
			time_step_order.push_back({timestep,timestep+1});
			// if formula generation takes a long time, we check whether we need to call the scheduler here
			call->scheduler->runScheduler(call->capsule.solver,false,false);
		}
		call->encoding->encodeInit(1);
		call->encoding->encodeGoal(call->timesteps + 1);

		cout << "Formula has " << call->capsule.get_number_of_clauses() << " clauses and " << call->capsule.number_of_variables << " variables." << endl;

		// start calling the solver	
		int solverState = ipasir_solve(call->capsule.solver);
		cout << "SAT solver state: " << solverState << endl;

		if (solverState == 10){
			// run plan extraction
			auto [goalState, states, labels, timesteps_with_labels] = call->encoding->extractSolution(1,time_step_order);
			// set the plan and run FTS extraction		
			search->check_goal_and_set_plan(goalState, states, std::move(labels), fts);

			cout << "STEP " << call->iterationNr << " length " << call->timesteps
					<< " SAT "
					<< " clauses " << call->capsule.get_number_of_clauses() << " vars " << call->capsule.number_of_variables
					<< " labels " << labels.size() << " timesteps with label " << timesteps_with_labels.size()
					<< " compression " << double(labels.size()) / timesteps_with_labels.size()
					<< endl;
			
			ipasir_release(call->capsule.solver);
			// finished and found a plan
			call->scheduler->runScheduler(call->capsule.solver,true,true);
		} else {
			cout << "STEP " << call->iterationNr << " length " << call->timesteps
					<< " UNSAT "
					<< " clauses " << call->capsule.get_number_of_clauses() << " vars " << call->capsule.number_of_variables
					<< endl;
			ipasir_release(call->capsule.solver);
			// finished and did not find a plan
			call->scheduler->runScheduler(call->capsule.solver,true,false);
		}





	    //std::chrono::milliseconds delay(time_in_ms);
	    //while(!stop) {
	    //  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	    //  auto t_now = std::chrono::system_clock::now();
	    //  std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t_now - t_start);
	    //  if (stop) break;
		//  if(delay <= elapsed) {
	    //      ipasir_terminate(solver);
		//	  cout << "SAT solver exceeded time limit. Terminating." << endl;
		//	  return;
	    //  }
	    //}
	}
};


bool RintanenSATSearch::create_next_length_run(std::shared_ptr<SAT_Scheduler> global_scheduler){
	// which iteration is this?
	int myStepNumber = global_scheduler->nextStepNumber;
	int previousLength = global_scheduler->previousLength;
	global_scheduler->nextStepNumber++; // increase the global next step number

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
	
	global_scheduler->previousLength = currentLength;

	cout << "Launching Step " << myStepNumber << " with length " << currentLength << endl;

	// prepare the data structures for this length
	void* solver = ipasir_init();
	kissat_set_external_scheduler((kissat*)solver,rintanen_scheduler_callback);
	sat_capsule capsule(solver);
	// create encoding object
	std::unique_ptr<SATEncoding> thisEncoding = encoding_factory->createEncodingInstance(capsule);

	//// scheduler information
	std::shared_ptr<SAT_Call_Data> this_call_data = make_shared<SAT_Call_Data>(capsule,global_scheduler,std::move(thisEncoding),myStepNumber,currentLength);
	global_scheduler->currentInstances[currentLength] = this_call_data;
	sat_solver_to_data[solver] = this_call_data;

	// start the next thread and immediately detach (thread will stop itself) TODO: hopefully???
	length_runner runner(this_call_data,fts,shared_from_this());
	std::thread thread_for_runner(runner);
	thread_for_runner.detach();

	// next length was created
	return true;
}

SearchStatus RintanenSATSearch::step() {
    utils::Timer step_timer;
	//auto t_start = std::chrono::system_clock::now();
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
