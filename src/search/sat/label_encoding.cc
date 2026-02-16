#include <chrono>
#include <thread>
#include <ctime>
#include <atomic>

#include "label_encoding.h"

#include "fts_matrix.h"
#include "../utils/logging.h"
#include "../utils/timer.h"
#include "ipasir.h"
#include "sat_encoder.h"


using namespace std;
using namespace task_representation;


namespace sat_search {

LabelEncoding::LabelEncoding(
	std::shared_ptr<sat_capsule> capsule,
	const std::shared_ptr<FTSTask> & _fts,
    bool _forceAtLeastOneAction, 
	bool _useLabelGroups,
	const std::vector<std::shared_ptr<FTSMatrix>> & _fts_matrices): StateEncoding(capsule,_fts, _forceAtLeastOneAction),
      useLabelGroups(_useLabelGroups),
	  fts_matrices(_fts_matrices)
{
}

vector<int> LabelEncoding::generateLabelVars(/* , int timestep */) const {
	vector<int> labelVars(fts->get_num_labels());
	for(int label = 0 ; label < fts->get_num_labels() ; label++){
		int labelVar = sat->new_variable();
		labelVars[label] = labelVar;
		DEBUG(sat->registerVariable(labelVar,"Label:"+to_string(label)));
		//cout << labelVar << endl;
	}
	return labelVars;
}

vector<vector<vector<int>>> LabelEncoding::generateLabelGroupVars(const vector<int> &labelVars/* , int timestep */) const{
	vector<vector<vector<int>>> labelGroupVars(fts->get_size());
	for(int ts = 0 ; ts < fts->get_size(); ts++){
		const auto & fts_matrix = fts_matrices[ts];
		labelGroupVars[ts].resize(fts_matrix->get_num_label_groups());
		for(int lg = 0 ; lg < fts_matrix->get_num_label_groups(); lg++){
			if (useLabelGroups) {
				// if the label group has only one member then always use the variable of that label itself.
				if(fts_matrix->get_labels_in_label_group(lg).size() == 1){
					labelGroupVars[ts][lg].push_back(labelVars[fts_matrix->get_labels_in_label_group(lg)[0]]);
					continue;
				}
				int lab_group = sat->new_variable();
				DEBUG(sat->registerVariable(lab_group,"LabelGroup:"+to_string(lg)));
				labelGroupVars[ts][lg].push_back(lab_group);
				vector<int> labels;
				for(int label : fts_matrix->get_labels_in_label_group(lg)){
					sat->implies(labelVars[label], lab_group);
					labels.push_back(labelVars[label]);
				}
				sat->impliesOr(lab_group, labels);
			} else {
				for(int label : fts_matrix->get_labels_in_label_group(lg)) {
					labelGroupVars[ts][lg].push_back(labelVars[label]);
				}
			}
		}
	}
	return labelGroupVars;
}

map<int, map<int, vector<int>>> LabelEncoding::generateHelperVars(/* , int timestep */) const{
	map<int, map<int, vector<int>>> helperVars;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			int num_helper_vars = fts_matrices[ts]->get_not_always_selfloop_labels_reaching_target(states).size() - 1;
			for(int h = 0 ; h < num_helper_vars ; h++){
				int helperVar = sat->new_variable();
				DEBUG(sat->registerVariable(helperVar, "Helpers"));
				helperVars[ts][states].push_back(helperVar);
			}
		}
	}
	return helperVars;
}

};
