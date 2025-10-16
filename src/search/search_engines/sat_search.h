#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "../search_engine.h"
#include "../option_parser.h"
#include "../plugin.h"
#include "sat_encoder.h"
#include "../task_representation/transition_system.h"
#include "../task_representation/label_equivalence_relation.h"

#include "../task_utils/label_order_finder.h"


namespace plugins {
class Feature;
}

namespace label_order_finder {
	class LabelOrderFinder;
}

namespace sat_search{

enum encoding_type {
	SEQUENTIAL,
	SELF_LOOP_PARALLEL,
	CHAINS_PARALLEL
};


class SATSearch : public SearchEngine {
private:
	std::shared_ptr<label_order_finder::LabelOrderFinder> label_order_finder;

	int stepTimeLimit;

	// global limit
	int planLength;

	// for iteration	
	int stepNumber;
	int currentLength;
	int start_length;
	double multiplier;
	
	bool length_by_iteration; 
	int maximum_iteration;

	bool no_selfloop_SATvars;

	encoding_type encoding;

	bool useLabelGroups;
	bool useSelfloopOptimisation;
	bool useEmptyRows;
	bool useEmptyCols;
	bool useEmptyPillars;
	
	bool forceAtLeastOneAction;
	
	std::shared_ptr<task_representation::FTSTask> fts;

    std::vector<int> labelOrder;
    std::vector<std::vector<int>> relevantLabels;
	std::vector<std::vector<int>> labelsWithoutOnlySelfLoops;
	std::map<int, std::map<int, std::vector<int>>> labelsWithEffectOnValue;
	std::map<int, std::map<int, std::set<int>>> empty_rows, empty_cols;
	std::map<int, std::map<int, std::map<int, std::set<int>>>> ones_per_row/* , ones_per_column */;
	std::map<int, std::vector<std::vector<int>>> labelProjection;
	std::map<int, std::map<int, std::vector<int>>> empty_projected_cells_per_row;//ts->row->cols

	std::map<int,std::vector<std::vector<int>>> labelGroups;

protected:
    virtual void initialize() override;
	bool isIrrelevantLabel(int ts, int label);
	bool containsSelfLoops(int ts, int label);
	virtual bool isAlwaysSelfLoop(int ts, int label);
	bool hasMixedTransitions(int ts, int label);
	int findPreviousValidAuxVar(std::vector<int> &auxVars, int label);
	bool hasSelfLoopOnValue(int ts, int value, int label);
    virtual SearchStatus step() override;
    virtual std::vector<std::vector<int>> generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */);
    virtual std::map<int, std::map<int, std::vector<std::pair<task_representation::Transition, int>>>> generateTransitionVars(void* solver, sat_capsule &capsule);
    virtual std::map<int, std::map<int, std::vector<int>>> generateAuxVars(sat_capsule &capsule);
    virtual std::vector<int> generateLabelVars(void* solver, sat_capsule & capsule/* , int timestep */);
	std::map<int, std::vector<int>> generateLabelGroupVars(void* solver, sat_capsule & capsule, std::vector<int> &labelVars/* , int timestep */);
	std::map<int, std::map<int, std::vector<int>>> generateHelperVars(sat_capsule & capsule/* , int timestep */);
    virtual std::map<int, std::map<int, std::vector<int>>> getApplicableLabels();
    virtual std::map<int, std::map<int, std::map<int, std::vector<int>>>> getSuccessorStates(std::map<int, std::map<int, std::vector<int>>> applicableLabels);


	void encode_sequential(void* solver, sat_capsule & capsule, std::vector<int> & labelVars);
	void encode_self_loop_parallel(void* solver, sat_capsule & capsule, std::vector<int> & labelVars);
	void encode_chains_parallel(void* solver, sat_capsule & capsule, std::vector<int> & labelVars, std::vector<std::vector<int>> & nextStateVars);
	void encode_transition(void* solver, sat_capsule & capsule, std::vector<std::vector<int>> & previousStateVars, std::vector<int> & labelVars, std::map<int, std::vector<int>> &labelGroupVars, std::vector<std::vector<int>> & nextStateVars);


public:
    explicit SATSearch(const Options &opts);
    virtual ~SATSearch() = default;

    virtual void print_statistics() const override;
};

extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
