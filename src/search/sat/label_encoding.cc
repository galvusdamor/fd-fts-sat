#include <chrono>
#include <thread>
#include <ctime>
#include <atomic>

#include "label_encoding.h"

//#include "../task_representation/transition_system.h"
//#include "../task_representation/fts_task.h"
#include "../task_representation/label_equivalence_relation.h"

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
	bool _useSelfloopOptimisation): StateEncoding(capsule,_fts, _forceAtLeastOneAction),
      useLabelGroups(_useLabelGroups),
	  useSelfloopOptimisation(_useSelfloopOptimisation)
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
		const auto & tss = fts->get_ts(ts);//TODO:CONTINUE FIXING FROM HERE!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		int num_label_groups = 0;
		vector<vector<int>> labelGroups;
		for (const auto &gat: tss) {
			num_label_groups++;
			vector<int> labels_in_group;
            for (int label: gat.label_group) {
                labels_in_group.push_back(label);
            }
			labelGroups.push_back(labels_in_group);
		}
		labelGroupVars[ts].resize(num_label_groups);
		for(int lg = 0 ; lg < num_label_groups; lg++){
			if (useLabelGroups) {
				// if the label group has only one member then always use the variable of that label itself.
				if(labelGroups[lg].size() == 1){
					labelGroupVars[ts][lg].push_back(labelVars[labelGroups[lg][0]]);
					continue;
				}
				int lab_group = sat->new_variable();
				DEBUG(sat->registerVariable(lab_group,"LabelGroup:"+to_string(lg)));
				labelGroupVars[ts][lg].push_back(lab_group);
				vector<int> labels;
				for(int label : labelGroups[lg]){
					sat->implies(labelVars[label], lab_group);
					labels.push_back(labelVars[label]);
				}
				sat->impliesOr(lab_group, labels);
			} else {
				for(int label : labelGroups[lg]) {
					labelGroupVars[ts][lg].push_back(labelVars[label]);
				}
			}
		}
		labelGroups.clear();
	}
	return labelGroupVars;
}

map<int, map<int, vector<int>>> LabelEncoding::generateHelperVars(/* , int timestep */) const{
	map<int, map<int, vector<int>>> helperVars;
	for(int ts = 0 ; ts < fts->get_size() ; ts++){
		vector<vector<int>> labelsReachingTarget = compute_and_return_labels_reaching_target(fts->get_ts(ts));
		for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
			int num_helper_vars = labelsReachingTarget[states].size() - 1;
			for(int h = 0 ; h < num_helper_vars ; h++){
				int helperVar = sat->new_variable();
				DEBUG(sat->registerVariable(helperVar, "Helpers"));
				helperVars[ts][states].push_back(helperVar);
			}
		}
	}
	return helperVars;
}

vector<vector<int>> LabelEncoding::compute_and_return_labels_reaching_target(const TransitionSystem &tss) const{
	vector<vector<int>> labelsReachingTarget(tss.get_size());
	for (const auto &gat: tss) {
		bool is_always_self_loop = std::all_of(
                gat.transitions.begin(),
                gat.transitions.end(),
                [&](const Transition &t) {
                    return t.src == t.target;
                }
            );

		if (!is_always_self_loop) {
			std::set<int> targets;
			for (const Transition &t: gat.transitions) {
				targets.insert(t.target);
			}
			for (int target: targets) {
				for (int label: gat.label_group) {
					labelsReachingTarget[target].push_back(label);
				}
			}
		}
	}
	return labelsReachingTarget;
}

};
