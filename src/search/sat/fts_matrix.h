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
    class FTSMatrix {
        // precomputed data structures that are the same for all encoding instances
        std::vector<std::vector<std::vector<int>>> labelGroups;
        std::vector<std::vector<std::vector<int>>> labelProjection;
        std::vector<std::vector<std::set<int>>> empty_rows, empty_cols;
        std::vector<std::vector<std::vector<int>>> labelsWithEffectOnValue;
        std::vector<std::vector<std::vector<int>>> empty_projected_cells_per_row;//ts->row->cols
        std::vector<std::vector<std::vector<std::set<int>>>> ones_per_row;

    public:
        FTSMatrix(
            const std::shared_ptr<task_representation::FTSTask> & fts,
            bool useEmptyRows,
            bool useEmptyCols,
            bool useEmptyPillars,
            bool useSelfloopOptimisation
        );


        const std::set<int> & get_empty_rows(int ts, int lg) const {
            return empty_rows[ts][lg];
        }

        const std::set<int> & get_empty_cols(int ts, int lg) const {
            return empty_cols[ts][lg];
        }

        const std::set<int> & get_ones_per_row(int ts, int lg, int src) const {
            return ones_per_row[ts][lg][src];
        }

        int get_num_label_groups(int ts) const {
            return labelGroups[ts].size();
        }

        const std::vector<int> & get_labels_in_label_group(int ts, int lg) const {
            return labelGroups[ts][lg];
        }

        //TODO: What does this mean? Is this int a number of transitions?
        int get_label_projection(int ts, int src, int target) const {
            return labelProjection[ts][src][target];
        }

        const std::vector<int> & get_labels_with_effect_on_value(int ts, int states) const {
            return labelsWithEffectOnValue[ts][states];
        }

        const std::vector<int> & get_empty_projected_cells_per_row(int ts, int row) const {
            return empty_projected_cells_per_row[ts][row];
        }

    };
}

#endif