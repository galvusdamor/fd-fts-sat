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
        std::vector<std::vector<int>> labelsWithEffectOnValue;
        std::vector<std::set<int>> empty_cols, empty_rows, empty_pillars;
		// Gregor: previously, we set empty_pillars[x][x] = false always. This seems unnecessary.

        // Alvaro: I wonder why we do not keep track of the following:
        // For each source -> number of possible targets, number of possible labels
        // For each label -> number of possible sources, number of possible targets
        // For each target -> number of possible sources, number of possible labels
        // For each <source,target> -> number of possible labels
        // For each <source,label> -> number of possible targets
        // For each <label,target> -> number of possible sources

        // In all of the cases above, if the number is 0 this corresponds to a constraint forbidding the combination.
        //                            if the number is 1 (or some k for a low value of k), this corresponds to a clause implying the right hand side.
        //                            if the number is n - 1 (or minus k for a low value of k), this corresponds to a clause forbidding the missing combinations.
        // I think that currently, empty_rows, empty_cols, empty_projected_cells_per_row are actually some of these combinations.
        // My question is which ones are we considering? Can you please identify which of the six cases above are we encoding
        // when we use empty_rows or empty_cols or empty_pillars? I think this is the last three cases and we only consider the case when the number is 0 or 1, is that correct?

        // Full matrix in a sparse representation. If useSelfloopOptimisation, then this representation does not contain the encoding of the self-loops. In a SAT encoding, self-loops will be handled separately.
        // Alvaro, some entries are excluded here, related to empty rows/cols/pillars. Can we write a comment here related to which ones are excluded? Gregor: only self-loops are excluded if optimised. The only other exclusion happened for the has_any_transition. 
        std::vector<std::vector<std::set<int>>> sparse_label_src_target; 
        std::vector<std::vector<std::set<int>>> sparse_label_target_src;
        std::vector<std::vector<std::set<int>>> sparse_src_target_label;

    public:
        FTSMatrix(
            const task_representation::TransitionSystem & fts,
            bool useEmptyRows,
            bool useEmptyCols,
            bool useEmptyPillars,
            bool useSelfloopOptimisation
        );

        const std::set<int> & get_empty_rows(int lg) const {
            return empty_rows[lg];
        }

        const std::set<int> & get_empty_cols(int lg) const {
            return empty_cols[lg];
        }

        const std::set<int> & get_ones_per_row(int lg, int src) const {
            return sparse_label_src_target[lg][src];
        }

        int get_num_label_groups() const {
            return labelGroups.size();
        }

        const std::vector<int> & get_labels_in_label_group(int lg) const {
            return labelGroups[lg];
        }

        const std::vector<int> & get_labels_with_effect_on_value(int states) const {
            return labelsWithEffectOnValue[states];
        }

        const std::set<int> & get_empty_projected_cells_per_row(int row) const {
            return empty_pillars[row];
        }

    };
}

#endif
