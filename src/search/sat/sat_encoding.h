#ifndef SAT_SAT_ENCODING
#define SAT_SAT_ENCODING

#include "../option_parser.h"
#include "../plugin.h"
#include "sat_encoder.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/transition_system.h"


// needed for access to g_main_task
// TODO Is there another way to access it?
#include "../search_engine.h"


namespace sat_search {

// abstract interface for a SAT encoding
class SATEncoding {
protected:
	sat_capsule & sat;
	std::shared_ptr<task_representation::FTSTask> fts;
	const bool & forceAtLeastOneAction;
public:
	SATEncoding(sat_capsule & capsule, std::shared_ptr<task_representation::FTSTask> _fts, const bool & _forceAtLeastOneAction) :
		sat(capsule), fts(_fts), forceAtLeastOneAction(_forceAtLeastOneAction) {};
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
	virtual SATEncoding* createEncodingInstance(sat_capsule & capsule) = 0;
	virtual void initialize() = 0;
};

};

#endif
