#ifndef COMMON_ENCODING
#define COMMON_ENCODING

#include "label_encoding.h"

namespace sat_search{

class CommonEncoding : public LabelEncoding {

protected:

    void encode(int fromTime, int toTime) override;
    
    /// encoding function for the main transition relation
    virtual void generateAdditionalVariables(int fromTime) = 0;
	virtual void encode_transition(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<int>> & nextStateVars, int fromTime) = 0;
    virtual void encode_frame_axioms(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<int>> & nextStateVars, int fromTime) = 0;

    virtual std::vector<std::vector<int>> extractIntermediateStates(std::vector<int> & selectedLabels, std::vector<int> & currentLastState, std::vector<int> & nextState) = 0;
    std::tuple<PlanState,std::vector<PlanState>,std::vector<int>,std::set<int>> extractSolution(int initTime, std::vector<std::pair<int,int>> time_step_order) override;

public:
    explicit CommonEncoding(
        std::shared_ptr<sat_capsule> capsule,
		const std::shared_ptr<task_representation::FTSTask> & _fts,
		bool _forceAtLeastOneAction,
		bool _useLabelGroups,
        bool _useSelfloopOptimisation
    );
    ~CommonEncoding() override = default;

    
};

}

#endif