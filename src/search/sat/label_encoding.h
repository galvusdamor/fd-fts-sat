#ifndef LABEL_ENCODING
#define LABEL_ENCODING

#include "state_encoding.h"

namespace sat_search{

class FTSMatrix;

class LabelEncoding : public StateEncoding {
	
	protected:
	bool useLabelGroups;

	std::vector<std::shared_ptr<FTSMatrix>> fts_matrices;
	
	//// persistent data structures
	std::map<int,std::vector<int>> allTimesLabelVars;

	//// functions generating data structures
    std::vector<int> generateLabelVars() const;
	std::vector<std::vector<std::vector<int>>> generateLabelGroupVars(const std::vector<int> &labelVars) const;
	std::map<int, std::map<int, std::vector<int>>> generateHelperVars() const;

public:
    explicit LabelEncoding(
		std::shared_ptr<sat_capsule> capsule,
		const std::shared_ptr<task_representation::FTSTask> & _fts,
		bool _forceAtLeastOneAction,
		bool _useLabelGroups,
		const std::vector<std::shared_ptr<FTSMatrix>> & _fts_matrices
			);
	~LabelEncoding() override = default;
};

}

#endif
