#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "sat_encoder.h"
#include "common_encoding.h"
#include "../task_representation/transition_system.h"


namespace plugins {
class Feature;
}


namespace sat_search{
class FTSMatrix;
class LengthStrategy;
enum encoding_type {
	SEQUENTIAL,
	SELF_LOOP_PARALLEL,
	CHAINS_PARALLEL
};

class LabelBasedEncoding : public CommonEncoding {
	bool statisticsPrinted;
	bool useEmptyRows;
	bool useEmptyCols;
	bool useEmptyPillars;
	bool useOnesInLastDimension;
	bool usePositiveOneForEmpty;
	size_t oneEncodingThreshold;
	int oneEncodingThresholdPercent;
	encoding_type encoding;

	std::vector<std::shared_ptr<FTSMatrix>> fts_matrices;

protected:

	std::map<int,std::vector<std::vector<std::vector<int>>>> allTimesLabelGroupVars;
	std::map<int,int> someLabelExecutedPerTime;

	/// encoding functions for parallelism
	void encode_sequential(const std::vector<int> & labelVars);
	void encode_self_loop_parallel(const std::vector<int> & labelVars);
	void encode_chains_parallel(const std::vector<int> & labelVars, const std::vector<std::vector<int>> & nextStateVars);

	/// encoding function for the main transition relation
	void encode_transition(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<int>> & nextStateVars, int fromTime/*, int toTime*/);
	void encode_transition_semantics(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<std::vector<int>>> &labelGroupVars, const int someLabelExecutedVar, const std::vector<std::vector<int>> & nextStateVars);
	void encode_frame_axioms(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<int>> & nextStateVars, int fromTime);
	void generateAdditionalVariables(int fromTime/*, int toTime*/);

	bool is_below_threshold(int ts, size_t ones_to_consider);

	std::vector<std::vector<int>> extractIntermediateStates(std::vector<int> & selectedLabels, std::vector<int> & currentLastState, std::vector<int> & nextState, int labelTimestep);


public:
    explicit LabelBasedEncoding(
		std::shared_ptr<sat_capsule> capsule,
		const std::shared_ptr<task_representation::FTSTask> & _fts,
		bool _statisticsPrinted,
		bool _useLabelGroups,
		bool _useSelfloopOptimisation,
		bool _useEmptyRows,
		bool _useEmptyCols,
		bool _useEmptyPillars,
		bool _useOnesInLastDimension,
		bool _usePositiveOneForEmpty,
		bool _forceAtLeastOneAction,
		const size_t _oneEncodingThreshold,
		const int _oneEncodingThresholdPercent,
		const encoding_type & _encoding,
		const std::vector<std::shared_ptr<FTSMatrix>> & fts_matrices
			);
	~LabelBasedEncoding() override = default;

	//void encode(int fromTime, int toTime) override;
	std::tuple<PlanState,std::vector<PlanState>,std::vector<int>,std::set<int>> extractSolution(int initTime, std::vector<std::pair<int,int>> time_step_order) override;
};


class LabelBasedEncodingFactory : public SATEncodingFactory {
	const bool useLabelGroups;
	const bool useSelfloopOptimisation;
	const bool useEmptyRows;
	const bool useEmptyCols;
	const bool useEmptyPillars;
	const bool useOnesInLastDimension;
	const bool usePositiveOneForEmpty;
	const size_t oneEncodingThreshold;
	const int oneEncodingThresholdPercent;
	const encoding_type encoding;
	bool statisticsPrinted;
	
	std::vector<std::shared_ptr<FTSMatrix> > fts_matrices;

public:
	explicit LabelBasedEncodingFactory(const options::Options &opts);

	void initialize() override;
	std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
    ~LabelBasedEncodingFactory() override = default;
};





extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
