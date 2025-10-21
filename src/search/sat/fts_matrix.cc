#include "fts_matrix.h"

#include <map>
#include "../task_representation/transition_system.h"
#include "../task_representation/fts_task.h"
#include "../task_representation/label_equivalence_relation.h"

using namespace std;
using namespace task_representation;

namespace sat_search {
    FTSMatrix::FTSMatrix(
        const TransitionSystem &tss,
        bool useEmptyRows,
        bool useEmptyCols,
        bool useEmptyPillars,
        bool useSelfloopOptimisation
    ) {
        int num_states = tss.get_size();
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

        set<int> set_of_all_states;
        std::vector<std::vector<bool>> hasAnyTransition = vector<vector<bool> >(num_states, vector<bool>(num_states, 0));
        for (int states = 0; states < num_states; states++) {
            set_of_all_states.insert(states);
            hasAnyTransition[states][states] = true;
        }

        empty_rows.resize(labelGroups.size());
        empty_cols.resize(labelGroups.size());
        ones_per_row.resize(labelGroups.size());
        for (size_t lg = 0; lg < labelGroups.size(); lg++) {
            ones_per_row[lg].resize(num_states);
            if (useEmptyRows) empty_rows[lg] = set_of_all_states;
            if (useEmptyCols) empty_cols[lg] = set_of_all_states;
            int label = labelGroups[lg][0];
            if (useSelfloopOptimisation && (tss.isIrrelevantLabel(label) || tss.isAlwaysSelfLoop(label))) continue;
            auto transitions = tss.get_transitions_with_label(label);
            for (const Transition &t: transitions) {
                if (useEmptyRows) empty_rows[lg].erase(t.src);
                if (useEmptyCols) empty_cols[lg].erase(t.target);
                ones_per_row[lg][t.src].insert(t.target);
                //ones_per_column[lg][t.target].insert(t.src);
                hasAnyTransition[t.src][t.target] = true;
            }
        }

        if (useEmptyPillars) {
            empty_projected_cells_per_row.resize(num_states);
            for (int src = 0; src < num_states; src++) {
                for (int target = 0; target < num_states; target++) {
                    if (hasAnyTransition[src][target] == false)
                        empty_projected_cells_per_row[src].insert(target);
                }
            }
        }
    }
}

