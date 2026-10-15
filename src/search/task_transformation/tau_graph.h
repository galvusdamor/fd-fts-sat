#ifndef TASK_TRANSFORMATION_TAU_GRAPH_H
#define TASK_TRANSFORMATION_TAU_GRAPH_H

#include "types.h"

#include "../task_representation/labels.h"

#include <vector>


namespace task_transformation {
/* class LabelMap; */
/* class MergeAndShrinkRepresentation; */

    class FactoredTransitionSystem;

    struct TauTransition {
        int target;
        task_representation::LabelID label;
        int cost;

    TauTransition(int target_, task_representation::LabelID label_, int cost_) :
        target(target_), label(label_), cost(cost_) {
        }

        int get_successor() const{
            return target;
        }
    };
    
    
    class TauGraph {
        std::vector<std::vector<TauTransition> > adjacency_matrix; 
        std::vector<bool> is_goal;
    public:

        TauGraph(const FactoredTransitionSystem &fts, int index, bool preserve_optimality);

        StateEquivalenceRelation compute_own_label_shrinking();
        StateEquivalenceRelation compute_own_label_plus_sg_shrinking(const FactoredTransitionSystem &fts, int index);

        // Auxiliar method that finds the shortest path from source to a target state. It
        // returns the path as a pair of label, target transitions.
        std::vector<std::pair<task_representation::LabelID, int>>
            find_shortest_path (int source, const std::vector<bool> & target) const;

        bool path_exists (int source, const std::vector<bool> & target) const;

        int get_num_states() const {return adjacency_matrix.size();}
        const std::vector<TauTransition> &get_transitions(int s) const {return adjacency_matrix[s];}
        
        void apply_label_mapping(const LabelMapping & label_mapping); 
    };

}
#endif
