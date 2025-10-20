#include "fts_matrix.h"

#include <map>
#include "../task_representation/transition_system.h"
#include "../task_representation/fts_task.h"

using namespace std;
using namespace task_representation;

namespace sat_search {
	FTSMatrix::FTSMatrix(
		const std::shared_ptr<task_representation::FTSTask> & fts,
		bool useEmptyRows,
		bool useEmptyCols,
		bool useEmptyPillars,
		bool useSelfloopOptimisation
	) {
		labelGroups.resize(fts->get_size());
		for(int ts = 0 ; ts < fts->get_size() ; ts++){
			map<set<pair<int, int>>, vector<int>> label_groups;
			for(int label = 0 ; label < fts->get_num_labels() ; label++){
				auto transitions = fts->get_ts(ts).get_transitions_with_label(label);

				std::set<std::pair<int, int>> transition_set;
				for (const auto& t : transitions) {
					transition_set.emplace(t.src, t.target);  // direction matters
				}

				label_groups[transition_set].push_back(label);
			}

			for(const auto& [_, labels] : label_groups){
				labelGroups[ts].push_back(labels);
			}
		}


		labelProjection.resize(fts->get_size());
		empty_rows.resize(fts->get_size());
		empty_cols.resize(fts->get_size());
		labelsWithEffectOnValue.resize(fts->get_size());
		empty_projected_cells_per_row.resize(fts->get_size());
		ones_per_row.resize(fts->get_size());
		for(int ts = 0 ; ts < fts->get_size() ; ts++){
			const TransitionSystem & tss = fts->get_ts(ts);
			set<int> values;
			labelProjection[ts] = vector<vector<int>> (fts->get_ts(ts).get_size(), vector<int>(fts->get_ts(ts).get_size(), 0));
			labelsWithEffectOnValue[ts].resize(fts->get_ts(ts).get_size());
			for(int states = 0 ; states < fts->get_ts(ts).get_size() ; states++){
				values.insert(states);
				labelProjection[ts][states][states] = 1;
				for(int label = 0 ; label < fts->get_num_labels() ; label++){
					if(tss.isAlwaysSelfLoop(label)) continue;
					auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
					for(const Transition & t : transitions){
						if(t.target == states){
							labelsWithEffectOnValue[ts][states].push_back(label);
							break;
						}
					}
				}
			}
			empty_rows[ts].resize(labelGroups[ts].size());
			empty_cols[ts].resize(labelGroups[ts].size());
			ones_per_row[ts].resize(labelGroups[ts].size());
			for(size_t lg = 0 ; lg < labelGroups[ts].size() ; lg++){
				ones_per_row[ts][lg].resize(fts->get_ts(ts).get_size());
				if (useEmptyRows) empty_rows[ts][lg] = values;
				if (useEmptyCols) empty_cols[ts][lg] = values;
				int label = labelGroups[ts][lg][0];
				if(useSelfloopOptimisation && (tss.isIrrelevantLabel(label) || tss.isAlwaysSelfLoop(label))) continue;
				auto transitions = fts->get_ts(ts).get_transitions_with_label(label);
				for(const Transition & t : transitions){
					if (useEmptyRows) empty_rows[ts][lg].erase(t.src);
					if (useEmptyCols) empty_cols[ts][lg].erase(t.target);
					ones_per_row[ts][lg][t.src].insert(t.target);
					//ones_per_column[ts][lg][t.target].insert(t.src);
					labelProjection[ts][t.src][t.target] = 1;
				}
			}

			if (useEmptyPillars){
				empty_projected_cells_per_row[ts].resize(fts->get_ts(ts).get_size());
				for(int src = 0 ; src < fts->get_ts(ts).get_size() ; src++){
					for(int target = 0 ; target < fts->get_ts(ts).get_size() ; target++){
						if(labelProjection[ts][src][target] == 0)
							empty_projected_cells_per_row[ts][src].push_back(target);
					}
				}
			}
		}
	}
}