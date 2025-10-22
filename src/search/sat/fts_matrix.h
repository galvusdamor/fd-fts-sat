#ifndef SAT_FTS_MATRIX_H
#define SAT_FTS_MATRIX_H


#include <vector>
#include <set>
#include <memory>

namespace task_representation {
    class FTSTask;
    class TransitionSystem;
    struct Transition;
}

namespace sat_search {
    // Stores a factored transition system matrix.
    // Rows correspond to fixing label groups and sources
    // Columns correspond to fixing label groups and targets
    // Pillars correspond to fixing sources and targets
    class FTSMatrix {
        // precomputed data structures that are the same for all encoding instances
		std::vector<std::vector<int>> labelGroups;
        std::vector<std::vector<int>> labelsReachingTarget;


        // Full matrix in a sparse representation.
        std::vector<std::vector<std::set<int>>> sparse_label_src_target; 
        std::vector<std::vector<std::set<int>>> sparse_label_target_src;
        std::vector<std::vector<std::set<int>>> sparse_src_target_label;
		// via calling .size, this includes the following information:
        // For each <label,source> -> number of possible targets
        // For each <label,target> -> number of possible sources
		// For each <source,target> -> number of possible labels
 

		// From these data-structures we can derive the following sub-information
		// Each X_impossible_Y maps all X to the Y's for which there is no transition involving both X and Y.
		// These six structures contain the information for which the three sparse_* data structures have size 0
		// only the first three are currently used in encoding
		std::vector<std::set<int>> label_impossible_source, label_impossible_target, source_impossible_target;
		// TODO: (Gregor) new currently unused options; created for symmetry
        //std::vector<std::set<int>> source_impossible_label, target_impossible_source, target_impossible_label;

        // Alvaro: I wonder why we do not keep track of the following:
        // For each source -> number of possible targets, number of possible labels
        // For each label -> number of possible sources, number of possible targets
        // For each target -> number of possible sources, number of possible labels
		// Gregor: No good reason, actually.
		// Easy implementation would be via e.g. source_impossible_target: its complement is exactly what we want here

		// Alvaro: 
        // In all of the cases above, if the number is 0 this corresponds to a constraint forbidding the combination.
        //                            if the number is 1 (or some k for a low value of k), this corresponds to a clause implying the right hand side.
        //                            if the number is n - 1 (or minus k for a low value of k), this corresponds to a clause forbidding the missing combinations.
		// Gregor: 
		// 	- the 0 case is encoded via label_impossible_target, label_impossible_source, and source_impossible_target, but only for these three combinations
		// 	- the 1 case is *only* checked for the combination <label,source> -> target (We only use if not covered otherwise and not in a systematic way)
		// 	- the n-1 case is used in any other case for the combination <label,source> -> target (it is effectively a fall-back)
		//
		//
		// Gregor: Text from Alvaro that I did not want to delete.
        // My question is which ones are we considering? Can you please identify which of the six cases above are we encoding
        // when we use empty_rows or empty_cols or empty_pillars? I think this is the last three cases and we only consider the case when the number is 0 or 1, is that correct?

       
    public:
        FTSMatrix(const task_representation::TransitionSystem & fts);
		// general information on self-computed label group IDs
		int get_num_label_groups() const {
            return labelGroups.size();
        }

        const std::vector<int> & get_labels_in_label_group(int lg) const {
            return labelGroups[lg];
        }

		// information on impossible cases
        const std::set<int> & get_impossible_sources_for_label(int lg) const {
            return label_impossible_source[lg];
        }

        const std::set<int> & get_impossible_targets_for_label(int lg) const {
            return label_impossible_target[lg];
        }

		const std::set<int> & get_impossible_targets_for_source(int source) const {
            return source_impossible_target[source];
        }


		// access to sparse representation
        const std::set<int> & get_targets_for_source_and_label(int lg, int src) const {
            return sparse_label_src_target[lg][src];
        }

		// all labels, except labels that are always self-loops, that can reach the state
        const std::vector<int> & get_not_always_selfloop_labels_reaching_target(int target) const {
            return labelsReachingTarget[target];
        }


    };
}

#endif
