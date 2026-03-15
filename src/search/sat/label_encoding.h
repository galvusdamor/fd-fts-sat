#ifndef LABEL_ENCODING
#define LABEL_ENCODING

#include "state_encoding.h"

namespace task_representation {
    class TransitionSystem;
}

namespace sat_search{

class LabelEncoding : public StateEncoding {
	
protected:
	bool useLabelGroups;
	bool useSelfloopOptimisation;
	
	//// persistent data structures
	std::map<int,std::vector<int>> allTimesLabelVars;

	//// functions generating data structures
    std::vector<int> generateLabelVars() const;
	std::vector<std::vector<std::vector<int>>> generateLabelGroupVars(const std::vector<int> &labelVars) const;
	std::map<int, std::map<int, std::vector<int>>> generateHelperVars() const;
	std::vector<std::vector<int>> compute_and_return_labels_reaching_target(const task_representation::TransitionSystem & fts) const;

public:
    explicit LabelEncoding(
		std::shared_ptr<sat_capsule> capsule,
		const std::shared_ptr<task_representation::FTSTask> & _fts,
		bool _forceAtLeastOneAction,
		bool _useLabelGroups,
		bool _useSelfloopOptimisation
			);
	~LabelEncoding() override = default;
};

}

#endif
