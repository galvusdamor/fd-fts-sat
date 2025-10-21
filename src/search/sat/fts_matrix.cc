#include "fts_matrix.h"

#include <map>
#include "../task_representation/transition_system.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/label_equivalence_relation.h"

using namespace std;
using namespace task_representation;

namespace sat_search {
//void calculate_empty_dimention(std::vector<std::set<int>> & is_empty, const std::vector<std::vector<std::set<int>>> & sparse_access){
//
//}


	FTSMatrix::FTSMatrix(
        const TransitionSystem &tss,
        bool useEmptyRows,
        bool useEmptyCols,
        bool useEmptyPillars,
        bool useSelfloopOptimisation
    ) {
        const int num_states = tss.get_size();
        labelsWithEffectOnValue.resize(num_states);
        for (const auto &gat: tss) {
            vector<int> labels_in_group;
            for (int label: gat.label_group) {
                labels_in_group.push_back(label);
            }
            labelGroups.push_back(labels_in_group);

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
                        labelsWithEffectOnValue[target].push_back(label);
                    }
                }
            }
        }

		/////////// compute sparse representation of transition table.
		// prepare empty data structures
        sparse_label_src_target.resize(labelGroups.size());
        sparse_label_target_src.resize(labelGroups.size());
        for (size_t lg = 0; lg < labelGroups.size(); lg++) {
            sparse_label_src_target[lg].resize(num_states);
            sparse_label_target_src[lg].resize(num_states);
		}
        
		sparse_src_target_label.resize(labelGroups.size());
		for (int src = 0; src < num_states; src++) {
			sparse_src_target_label[src].resize(num_states);
		}

		// iterate over all transitions once and insert them into the right data structure
        for (size_t lg = 0; lg < labelGroups.size(); lg++) {
            int label = labelGroups[lg][0];
            if (useSelfloopOptimisation && (tss.isIrrelevantLabel(label) || tss.isAlwaysSelfLoop(label))) continue;
            auto transitions = tss.get_transitions_with_label(label);
            for (const Transition &t: transitions) {
                sparse_label_src_target[lg][t.src].insert(t.target);
                sparse_label_target_src[lg][t.target].insert(t.src);
                sparse_src_target_label[t.src][t.target].insert(lg);
            }
        }

		/////////////// extract counting information from sparse information
        empty_cols.resize(labelGroups.size());
        if (useEmptyCols){
			for (size_t lg = 0; lg < labelGroups.size(); lg++) {
				for (int target = 0; target < num_states; target++) {
					if (sparse_label_target_src[lg][target].size() == 0)
						empty_cols[lg].insert(target);
				}
			}
		}

        empty_rows.resize(labelGroups.size());
		if (useEmptyRows){
			for (size_t lg = 0; lg < labelGroups.size(); lg++) {
				for (int src = 0; src < num_states; src++) {
					if (sparse_label_src_target[lg][src].size() == 0)
						empty_rows[lg].insert(src);
				}
			}
		}

		empty_pillars.resize(num_states);
		if (useEmptyPillars){
			for (int src = 0; src < num_states; src++) {
        	    for (int target = 0; target < num_states; target++) {
					if (sparse_src_target_label[src][target].size() == 0)
						empty_pillars[src].insert(target);
				}
			}
		}
    }
}



