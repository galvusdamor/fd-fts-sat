#ifndef SAT_SAT_ENCODING
#define SAT_SAT_ENCODING

#include "../option_parser.h"
#include "../plugin.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/transition_system.h"

namespace sat_search {

// abstract interface for a SAT encoding
class SAT_encoding {
	virtual void initialize() = 0;
	virtual void encode(int currentLength, int stepTimeLimit) = 0;
};

};

#endif
