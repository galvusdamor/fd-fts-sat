#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "sat_encoder.h"
#include "sat_encoding.h"
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




class LabelBasedEncoding : public SATEncoding {
	const bool & useLabelGroups;
	const bool & useSelfloopOptimisation;
	const bool & useEmptyRows;
	const bool & useEmptyCols;
	const bool & useEmptyPillars;
	const encoding_type & encoding;

	std::shared_ptr<task_representation::FTSTask> fts;
	
	const std::vector<std::vector<std::vector<int>>> & labelGroups;
	const std::vector<std::vector<std::vector<int>>> &labelProjection;
	const std::vector<std::vector<std::set<int>>> &empty_rows, &empty_cols;
	const std::vector<std::vector<std::vector<int>>> &labelsWithEffectOnValue;
	//ts->row->cols
	const std::vector<std::vector<std::vector<int>>> &empty_projected_cells_per_row;
	const std::vector<std::vector<std::vector<std::set<int>>>> &ones_per_row;



protected:
	//// persistent data structures
	std::map<int,std::vector<std::vector<int>>> allTimesStateVars;
	std::map<int,std::vector<int>> allTimesLabelVars;


	//// functions generating data structures
    std::vector<std::vector<int>> generateStateVars();
    std::vector<int> generateLabelVars();
	std::vector<std::vector<int>> generateLabelGroupVars(const std::vector<int> &labelVars);
	std::map<int, std::map<int, std::vector<int>>> generateHelperVars();


	/// encoding functions for parallelism
	void encode_sequential(const std::vector<int> & labelVars);
	void encode_self_loop_parallel(const std::vector<int> & labelVars);
	void encode_chains_parallel(const std::vector<int> & labelVars, const std::vector<std::vector<int>> & nextStateVars);

	/// encoding function for the main transition relation
	void encode_transition(const std::vector<std::vector<int>> & previousStateVars, const std::vector<int> & labelVars, const std::vector<std::vector<int>> &labelGroupVars, const std::vector<std::vector<int>> & nextStateVars);


public:
    explicit LabelBasedEncoding(
		sat_capsule & capsule,
		std::shared_ptr<task_representation::FTSTask> _fts,
		const bool & _useLabelGroups,
		const bool & _useSelfloopOptimisation,
		const bool & _useEmptyRows,
		const bool & _useEmptyCols,
		const bool & _useEmptyPillars,
		const bool & _forceAtLeastOneAction,
		const encoding_type & _encoding,
		const std::vector<std::vector<std::vector<int>>> &_labelGroups,
		const std::vector<std::vector<std::vector<int>>> &_labelProjection,
		const std::vector<std::vector<std::set<int>>> &_empty_rows,
		const std::vector<std::vector<std::set<int>>> &_empty_cols,
		const std::vector<std::vector<std::vector<int>>> &_labelsWithEffectOnValue,
		const std::vector<std::vector<std::vector<int>>> &_empty_projected_cells_per_row,
		const std::vector<std::vector<std::vector<std::set<int>>>> &_ones_per_row
			);
    virtual ~LabelBasedEncoding() = default;

	virtual void encode(int fromTime, int toTime);
	virtual void encodeInit(int fromTime);
	virtual void encodeGoal(int toTime);
	std::tuple<PlanState,std::vector<PlanState>,std::vector<int>,std::set<int>> extractSolution(std::vector<std::pair<int,int>> time_step_order);
};


class LabelBasedEncodingFactory : public SATEncodingFactory {
	const bool useLabelGroups;
	const bool useSelfloopOptimisation;
	const bool useEmptyRows;
	const bool useEmptyCols;
	const bool useEmptyPillars;
	const encoding_type encoding;
	
	// precomputed data structures that are the same for all encoding instances
	std::vector<std::vector<std::vector<int>>> labelGroups;
	
	std::vector<std::vector<std::vector<int>>> labelProjection;
	std::vector<std::vector<std::set<int>>> empty_rows, empty_cols;
	std::vector<std::vector<std::vector<int>>> labelsWithEffectOnValue;
	std::vector<std::vector<std::vector<int>>> empty_projected_cells_per_row;//ts->row->cols
	std::vector<std::vector<std::vector<std::set<int>>>> ones_per_row;
    

public:	
	virtual void initialize() override;
	virtual LabelBasedEncoding* createEncodingInstance(sat_capsule & capsule) override;
    explicit LabelBasedEncodingFactory(const options::Options &opts);
    virtual ~LabelBasedEncodingFactory() = default;
};





extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
