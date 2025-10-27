#ifndef SEARCH_ALGORITHMS_RINTANEN_SAT_SEARCH
#define SEARCH_ALGORITHMS_RINTANEN_SAT_SEARCH

#include "../search_engine.h"
#include "../sat/sat_encoder.h"
#include "../sat/sat_encoding.h"
#include "../task_representation/transition_system.h"
#include "../task_representation/label_equivalence_relation.h"


namespace plugins {
class Feature;
}

struct SAT_Scheduler;


namespace sat_search{

class LengthStrategy;
enum encoding_type {
	SEQUENTIAL,
	SELF_LOOP_PARALLEL,
	CHAINS_PARALLEL
};


class RintanenSATSearch : public SearchEngine, std::enable_shared_from_this<RintanenSATSearch>{
	const std::shared_ptr<LengthStrategy> length_strategy;
	const std::shared_ptr<SATEncodingFactory> encoding_factory;

protected:
    virtual void initialize() override;
	virtual SearchStatus step() override;

public:
    explicit RintanenSATSearch(const options::Options &opts);
    virtual ~RintanenSATSearch() = default;

    // needs to be public as accessed by thread
	using SearchEngine::check_goal_and_set_plan;
	std::shared_ptr<task_representation::FTSTask> fts;
	
	// returns true if there is a next run that could be generated
	bool create_next_length_run(std::shared_ptr<SAT_Scheduler> global_scheduler);
    
	virtual void print_statistics() const override;
};

extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
