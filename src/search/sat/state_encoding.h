#ifndef STATE_ENCODING
#define STATE_ENCODING

#include "sat_encoding.h"

namespace sat_search{

class StateEncoding : public SATEncoding {

protected:
	//// persistent data structures
	std::map<int,std::vector<std::vector<int>>> allTimesStateVars;

	//// functions generating data structures
    std::vector<std::vector<int>> generateStateVars() const;

	int getPreviousStateSATVar(int ts, int state, int fromTime) const;
	int getNextStateSATVar(int ts, int state, int toTime) const;

public:
    explicit StateEncoding(
		std::shared_ptr<sat_capsule> capsule,
		const std::shared_ptr<task_representation::FTSTask> & _fts,
        bool _forceAtLeastOneAction);
	~StateEncoding() override = default;

	void encodeInit(int fromTime, bool retractable) override;
	void encodeGoal(int toTime, bool retractable) override;
	void encodeStateEquals(int fromTime, int toTime, bool retractable) override;
	};

}

#endif