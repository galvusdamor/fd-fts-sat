#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

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
enum encoding_type {
	SEQUENTIAL,
	SELF_LOOP_PARALLEL,
	CHAINS_PARALLEL
};


class SATSearch : public SearchEngine {
	const int stepTimeLimit;
	bool continueAfterFirstPlan;
	const std::shared_ptr<LengthStrategy> length_strategy;
	const std::shared_ptr<SATEncodingFactory> encoding_factory;

	std::shared_ptr<task_representation::FTSTask> fts;

	// for iteration
	int stepNumber;
	int currentLength;

protected:
    virtual void initialize() override;
	virtual SearchStatus step() override;

public:
    explicit SATSearch(const options::Options &opts);
    virtual ~SATSearch() = default;

    virtual void print_statistics() const override;
};

extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
