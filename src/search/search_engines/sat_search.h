#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "../search_engine.h"
#include "../sat/sat_encoder.h"
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
	const bool useLabelGroups;
	const bool useSelfloopOptimisation;
	const bool useEmptyRows;
	const bool useEmptyCols;
	const bool useEmptyPillars;
	const bool no_selfloop_SATvars = false;//TODO: What is this???? Is this an option??
	const encoding_type encoding;
	bool continueAfterFirstPlan;
	const std::shared_ptr<LengthStrategy> length_strategy;

	std::shared_ptr<task_representation::FTSTask> fts;

    std::vector<std::vector<int>> relevantLabels;
	std::vector<std::vector<int>> labelsWithoutOnlySelfLoops;
	std::map<int, std::map<int, std::vector<int>>> labelsWithEffectOnValue;
	std::map<int, std::map<int, std::set<int>>> empty_rows, empty_cols;
	std::map<int, std::map<int, std::map<int, std::set<int>>>> ones_per_row/* , ones_per_column */;
	std::map<int, std::vector<std::vector<int>>> labelProjection;
	std::map<int, std::map<int, std::vector<int>>> empty_projected_cells_per_row;//ts->row->cols
	std::map<int,std::vector<std::vector<int>>> labelGroups;

	// for iteration
	int stepNumber;
	int currentLength;

protected:
    virtual void initialize() override;
	virtual SearchStatus step() override;

	bool isIrrelevantLabel(int ts, int label);
	bool containsSelfLoops(int ts, int label);
	virtual bool isAlwaysSelfLoop(int ts, int label);
	bool hasMixedTransitions(int ts, int label);
	int findPreviousValidAuxVar(const std::vector<int> &auxVars, int label);
	bool hasSelfLoopOnValue(int ts, int value, int label);

    std::vector<std::vector<int>> generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */);
    std::map<int, std::map<int, std::vector<std::pair<task_representation::Transition, int>>>> generateTransitionVars(void* solver, sat_capsule &capsule);
    std::map<int, std::map<int, std::vector<int>>> generateAuxVars(sat_capsule &capsule);
    std::vector<int> generateLabelVars(void* solver, sat_capsule & capsule/* , int timestep */);
	std::map<int, std::vector<int>> generateLabelGroupVars(void* solver, sat_capsule & capsule, const std::vector<int> &labelVars/* , int timestep */);
	std::map<int, std::map<int, std::vector<int>>> generateHelperVars(sat_capsule & capsule/* , int timestep */);
    std::map<int, std::map<int, std::vector<int>>> getApplicableLabels();
    std::map<int, std::map<int, std::map<int, std::vector<int>>>> getSuccessorStates(const std::map<int, std::map<int, std::vector<int>>> & applicableLabels);

	void encode_sequential(void* solver, sat_capsule & capsule, std::vector<int> & labelVars);
	void encode_self_loop_parallel(void* solver, sat_capsule & capsule, std::vector<int> & labelVars);
	void encode_chains_parallel(void* solver, sat_capsule & capsule, std::vector<int> & labelVars, std::vector<std::vector<int>> & nextStateVars);
	void encode_transition(void* solver, sat_capsule & capsule, std::vector<std::vector<int>> & previousStateVars, std::vector<int> & labelVars, std::map<int, std::vector<int>> &labelGroupVars, std::vector<std::vector<int>> & nextStateVars);


public:
    explicit SATSearch(const options::Options &opts);
    virtual ~SATSearch() = default;

    virtual void print_statistics() const override;
};

extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
