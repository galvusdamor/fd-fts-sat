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
        std::vector<std::vector<bool>> hasAnyTransition;
        std::vector<std::set<int>> empty_rows, empty_cols;
        std::vector<std::vector<int>> labelsWithEffectOnValue;
        std::vector<std::vector<int>> empty_projected_cells_per_row;//ts->row->cols

        // Full matrix in a sparse representation. Excludes any entry
        std::vector<std::vector<std::set<int>>> ones_per_row;

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
            return ones_per_row[lg][src];
        }

        int get_num_label_groups() const {
            return labelGroups.size();
        }

        const std::vector<int> & get_labels_in_label_group(int lg) const {
            return labelGroups[lg];
        }

        // returns whether there is any transition from src to target (or whether src==target).
        bool has_any_transition(int src, int target) const {
            return hasAnyTransition[src][target];
        }

        const std::vector<int> & get_labels_with_effect_on_value(int states) const {
            return labelsWithEffectOnValue[states];
        }

        const std::vector<int> & get_empty_projected_cells_per_row(int row) const {
            return empty_projected_cells_per_row[row];
        }

    };
}

#endif
