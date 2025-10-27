#ifndef SAT_SAT_ENCODING
#define SAT_SAT_ENCODING

#include "sat_encoder.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/transition_system.h"
#include "../option_parser.h"


// needed for access to g_main_task
#include "../globals.h"


namespace sat_search {

// abstract interface for a SAT encoding
class SATEncoding {
protected:
	std::shared_ptr<sat_capsule> sat;
	std::shared_ptr<task_representation::FTSTask> fts;
	bool forceAtLeastOneAction;
public:
	SATEncoding(std::shared_ptr<sat_capsule> capsule, const std::shared_ptr<task_representation::FTSTask> &_fts, bool forceAtLeastOneAction) :
		sat(capsule), fts(_fts), forceAtLeastOneAction(forceAtLeastOneAction) {};
	virtual ~SATEncoding() = default;
	virtual void encode(int fromTime, int toTime) = 0;
	virtual void encodeInit(int fromTime) = 0;
	virtual void encodeGoal(int toTime) = 0;
	virtual std::tuple<PlanState,std::vector<PlanState>,std::vector<int>,std::set<int>> extractSolution(int initTime, std::vector<std::pair<int,int>> time_step_order) = 0;
};

// abstract interface for initialisation of SAT encoding
class SATEncodingFactory {
protected:
	std::shared_ptr<task_representation::FTSTask> fts;
	const bool forceAtLeastOneAction;
public:
	SATEncodingFactory(bool _forceAtLeastOneAction) : fts(g_main_task), forceAtLeastOneAction(_forceAtLeastOneAction) {};
	virtual ~SATEncodingFactory() = default;
	virtual std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) = 0;
	virtual void initialize() = 0;
};

};

#endif
