#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "sat_encoder.h"
#include "sat_encoding.h"
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

class LabelBasedEncoding : public SATEncoding {
	bool statisticsPrinted;
	bool useLabelGroups;
	bool useSelfloopOptimisation;
	bool useEmptyRows;
	bool useEmptyCols;
	bool useEmptyPillars;
	bool useOnesInLastDimension;
	bool usePositiveOneForEmpty;
	encoding_type encoding;

	std::shared_ptr<task_representation::FTSTask> fts;
	std::vector<std::shared_ptr<FTSMatrix>> fts_matrices;

	size_t oneEncodingThreshold = 1;
protected:
	//// persistent data structures
	std::map<int,std::vector<std::vector<int>>> allTimesStateVars;
	std::map<int,std::vector<int>> allTimesLabelVars;

	//// functions generating data structures
    std::vector<std::vector<int>> generateStateVars() const;
    std::vector<int> generateLabelVars() const;
	std::vector<std::vector<std::vector<int>>> generateLabelGroupVars(const std::vector<int> &labelVars) const;
	std::map<int, std::map<int, std::vector<int>>> generateHelperVars() const;

	/// encoding functions for parallelism
	void encode_sequential(const std::vector<int> & labelVars);
	void encode_self_loop_parallel(const std::vector<int> & labelVars);
	void encode_chains_parallel(const std::vector<int> & labelVars, const std::vector<std::vector<int>> & nextStateVars);

	/// encoding function for the main transition relation
	void encode_transition(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<std::vector<int>>> &labelGroupVars, const std::vector<std::vector<int>> & nextStateVars);
	void encode_frame_axioms(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<std::vector<int>>> &labelGroupVars, const std::vector<std::vector<int>> & nextStateVars);


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
		const encoding_type & _encoding,
		const std::vector<std::shared_ptr<FTSMatrix>> & fts_matrices
			);
	//~LabelBasedEncoding() override = default;
	~LabelBasedEncoding() override { std::cout << "Deleting encoding instance" << std::endl; };

	void encode(int fromTime, int toTime) override;
	void encodeInit(int fromTime, bool retractable) override;
	void encodeGoal(int toTime, bool retractable) override;
	void encodeStateEquals(int fromTime, int toTime, bool retractable) override;
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
