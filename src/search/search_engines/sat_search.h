#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "../search_algorithm.h"

namespace plugins {
class Feature;
}


namespace sat_search{

class SATSearch : public SearchAlgorithm {
private: 
	int planLength;


protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    explicit SATSearch(const plugins::Options &opts);
    virtual ~SATSearch() = default;

    virtual void print_statistics() const override;
}

extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
