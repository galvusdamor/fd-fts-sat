#ifndef SEARCH_ALGORITHMS_INCREMENTAL_SAT_SEARCH
#define SEARCH_ALGORITHMS_INCREMENTAL_SAT_SEARCH

#include "../search_engine.h"
#include "../sat/sat_encoder.h"
#include "../sat/sat_encoding.h"
#include "../task_representation/transition_system.h"
#include "../task_representation/label_equivalence_relation.h"


namespace plugins {
class Feature;
}


namespace sat_search{

class LengthStrategy;

enum incremental_strategy {
	GOAL,
	INIT,
	MIDDLE
};


class IncrementalSATSearch : public SearchEngine {
	const std::shared_ptr<LengthStrategy> length_strategy;
	const std::shared_ptr<SATEncodingFactory> encoding_factory;
	const incremental_strategy inc_strategy;

	std::shared_ptr<task_representation::FTSTask> fts;

	// we keep *one* solver instance for incremental solving
	std::shared_ptr<sat_capsule> capsule;
	std::shared_ptr<SATEncoding> encoding;
	std::vector<std::pair<int,int>> time_step_order_after_init; // for plan extraction
	std::list<std::pair<int,int>> time_step_order_before_goal; // for plan extraction

	// for iteration
	int stepNumber;
	int currentLength;
	int currentGenerated;
	int currentLastAfterInit;
	int currentFirstBeforeGoal;

protected:
    virtual void initialize() override;
	virtual SearchStatus step() override;

public:
    explicit IncrementalSATSearch(const options::Options &opts);
    virtual ~IncrementalSATSearch() = default;

    virtual void print_statistics() const override;
};

extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
