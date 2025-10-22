#include "fts_matrix.h"

#include <map>
#include "../task_representation/transition_system.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/label_equivalence_relation.h"

using namespace std;
using namespace task_representation;

namespace sat_search {
void calculate_possible_impossible_dimention(
		std::vector<std::set<int>> & is_impossible,
		std::vector<std::set<int>> & is_possible,
		int second_dimension_size,
		const std::vector<std::map<int,std::set<int>>> & sparse_access){
	is_impossible.resize(sparse_access.size());
	is_possible.resize(sparse_access.size());
	// compute the projection
	for (size_t i = 0; i < sparse_access.size(); i++) {
		// if value j is in sparse access, it has to be possible
		for (const auto & [j,zs] : sparse_access[i]) {
			is_possible[i].insert(j);
		}
		// inverse of possible is impossible
		for (int j = 0; j < second_dimension_size; j++) {
			if (! is_possible[i].contains(j)) 
				is_impossible[i].insert(j);
		}
	}
}


	FTSMatrix::FTSMatrix(const TransitionSystem &tss) {
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
		sparse_src_target_label.resize(num_states);
		sparse_src_label_target.resize(num_states);
		sparse_target_src_label.resize(num_states);
		sparse_target_label_src.resize(num_states);

		// iterate over all transitions once and insert them into the right data structure
        for (size_t lg = 0; lg < labelGroups.size(); lg++) {
            int label = labelGroups[lg][0];
			auto transitions = tss.get_transitions_with_label(label);
            for (const Transition &t: transitions) {
                sparse_label_src_target[lg][t.src].insert(t.target);
                sparse_label_target_src[lg][t.target].insert(t.src);
                sparse_src_target_label[t.src][t.target].insert(lg);
                sparse_src_label_target[t.src][lg].insert(t.target);
                sparse_target_src_label[t.target][t.src].insert(lg);
                sparse_target_label_src[t.target][lg].insert(t.src);
            }
        }

		/////////////// extract counting information from sparse information
		calculate_possible_impossible_dimention(label_impossible_source,label_possible_source,
				num_states,
				sparse_label_src_target);

		calculate_possible_impossible_dimention(label_impossible_target,label_possible_target,
				num_states,
				sparse_label_target_src);

		calculate_possible_impossible_dimention(source_impossible_target,source_possible_target,
				num_states,
				sparse_src_target_label);
	}
}















