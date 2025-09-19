#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "../search_engine.h"
#include "../option_parser.h"
#include "../plugin.h"
#include "sat_encoder.h"
#include "../task_representation/transition_system.h"
#include "../task_representation/label_equivalence_relation.h"

#include "../task_utils/label_order_finder.h"

struct BlockInfo {
    std::vector<int> empty_rows;
    std::vector<int> empty_columns;
    std::map<int, std::vector<int>> extra_ones_per_row;    // rows with 1's outside block
    std::map<int, std::vector<int>> extra_ones_per_column; // columns with 1's outside block
};



namespace plugins {
class Feature;
}

namespace label_order_finder {
	class LabelOrderFinder;
}

namespace sat_search{

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

	bool do_R2_encoding;
	bool no_selfloop_SATvars;
	bool computing_block;


	bool sequential = false;
	bool selfloopParallelism = false;
	bool chainsParallelism = false;

	bool useLabelGroups = false;
	bool useSelfloopOptimisation = false;

	bool basic_per_row = false;
	bool eliminating_rnc_and_pairs = false;

	bool forceAtLeastOneAction;
	
	std::shared_ptr<task_representation::FTSTask> fts;

    std::vector<int> labelOrder;
    std::vector<std::vector<int>> relevantLabels;
	std::map<int, std::map<int, BlockInfo>> labelBasedEncodingInfo;
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
	BlockInfo find_largest_block(const std::vector<std::vector<int>>& filled_columns_per_row);
	bool hasSelfLoopOnValue(int ts, int value, int label);
	virtual void checkSolution(std::vector<std::vector<std::vector<int>>> &allTimesStateVars, std::vector<std::vector<int>> &allTimesLabelVars, 
		std::vector<std::map<int, std::map<int, std::vector<std::pair<task_representation::Transition, int>>>>> &allTimesTransitionVars, int length, void* solver);
    virtual SearchStatus step() override;
    virtual std::vector<std::vector<int>> generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */);
    virtual std::map<int, std::map<int, std::vector<std::pair<task_representation::Transition, int>>>> generateTransitionVars(void* solver, sat_capsule &capsule);
    virtual std::map<int, std::map<int, std::vector<int>>> generateAuxVars(sat_capsule &capsule);
    virtual std::vector<int> generateLabelVars(void* solver, sat_capsule & capsule/* , int timestep */);
	std::map<int, std::vector<int>> generateLabelGroupVars(void* solver, sat_capsule & capsule, std::vector<int> &labelVars/* , int timestep */);
	std::map<int, std::map<int, std::vector<int>>> generateHelperVars(sat_capsule & capsule/* , int timestep */);
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
