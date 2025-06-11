#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "../search_engine.h"
#include "../option_parser.h"
#include "../plugin.h"
#include "sat_encoder.h"
#include "../task_representation/transition_system.h"

namespace plugins {
class Feature;
}


namespace sat_search{

class SATSearch : public SearchEngine {
private: 
	int planLength;
	std::shared_ptr<task_representation::FTSTask> fts;
    std::vector<std::vector<int>> np_labels;
    std::vector<int> labelOrder;

	int currentLength;

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;
    virtual std::vector<std::vector<int>> generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */);
    virtual std::map<int, std::map<int, std::vector<std::pair<task_representation::Transition, int>>>> generateTransitionVars(void* solver, sat_capsule &capsule);
    virtual std::map<int, std::map<int, std::vector<int>>> generateAuxVars(sat_capsule &capsule);
    virtual std::vector<int> generateLabelVars(void* solver, sat_capsule & capsule/* , int timestep */);
    virtual std::map<int, std::map<int, std::vector<int>>> getApplicableLabels();
    virtual std::map<int, std::map<int, std::map<int, std::vector<int>>>> getSuccessorStates(std::map<int, std::map<int, std::vector<int>>> applicableLabels);

public:
    explicit SATSearch(const Options &opts);
    virtual ~SATSearch() = default;

    virtual void print_statistics() const override;
};

extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
