#include "fts_matrix.h"

#include <map>
#include "../task_representation/transition_system.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/label_equivalence_relation.h"

using namespace std;
using namespace task_representation;

namespace sat_search {
void calculate_empty_dimention(std::vector<std::set<int>> & is_empty,
		const std::vector<std::vector<std::set<int>>> & sparse_access,
		int dimention_to_project_onto){
	// using 1 and 2 is better to read then numbers
	assert(dimention_to_project_onto == 1 || dimention_to_project_onto == 2);

	if (dimention_to_project_onto == 1){
		// if we project onto the first dimension that the result must have the same size
		assert(sparse_access.size() == is_empty.size());
		// compute the projection
		for (size_t i = 0; i < sparse_access.size(); i++) {
			for (size_t j = 0; j < sparse_access[i].size(); j++) {
				if (sparse_access[i][j].size() == 0)
					is_empty[i].insert(j);
			}
		}
	} else if (dimention_to_project_onto == 2){
		if (sparse_access.size() == 0) return;
		size_t dim_2_size = sparse_access[0].size();
		
		// compute the projection
		for (size_t j = 0; j < dim_2_size; j++) {
			for (size_t i = 0; i < sparse_access.size(); i++) {
				assert(sparse_access[i].size() == dim_2_size); // all second dimensions must have the same size
				if (sparse_access[i][j].size() == 0)
					is_empty[j].insert(i);
			}
		}
	}
}


	FTSMatrix::FTSMatrix(
        const TransitionSystem &tss,
        bool useEmptyRows,
        bool useEmptyCols,
        bool useEmptyPillars
    ) {
        const int num_states = tss.get_size();
        labelsReachingTarget.resize(num_states);
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
                        labelsReachingTarget[target].push_back(label);
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
        
		sparse_src_target_label.resize(num_states);
		for (int src = 0; src < num_states; src++) {
			sparse_src_target_label[src].resize(num_states);
		}

		// iterate over all transitions once and insert them into the right data structure
        for (size_t lg = 0; lg < labelGroups.size(); lg++) {
            int label = labelGroups[lg][0];
			auto transitions = tss.get_transitions_with_label(label);
            for (const Transition &t: transitions) {
                sparse_label_src_target[lg][t.src].insert(t.target);
                sparse_label_target_src[lg][t.target].insert(t.src);
                sparse_src_target_label[t.src][t.target].insert(lg);
            }
        }

		/////////////// extract counting information from sparse information
        label_impossible_source.resize(labelGroups.size());
		if (useEmptyCols) calculate_empty_dimention(label_impossible_source,sparse_label_src_target,1);

        label_impossible_target.resize(labelGroups.size());
		if (useEmptyRows) calculate_empty_dimention(label_impossible_target,sparse_label_target_src,1);

		source_impossible_target.resize(num_states);
		if (useEmptyPillars) calculate_empty_dimention(source_impossible_target,sparse_src_target_label,1);


		// TODO: currently unused, have to add command line options
		source_impossible_label.resize(num_states);
		calculate_empty_dimention(source_impossible_label,sparse_label_src_target,2);
		target_impossible_source.resize(num_states);
		calculate_empty_dimention(target_impossible_source,sparse_src_target_label,2);
		target_impossible_label.resize(num_states);
		calculate_empty_dimention(target_impossible_label,sparse_label_target_src,2);
	}
}















