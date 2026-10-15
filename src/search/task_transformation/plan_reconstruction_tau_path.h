#ifndef TASK_TRANSFORMATION_PLAN_RECONSTRUCTION_TAU_PATH_H
#define TASK_TRANSFORMATION_PLAN_RECONSTRUCTION_TAU_PATH_H

#include "plan_reconstruction.h"
#include "../search_space.h"
#include "../state_registry.h"
#include "label_map.h"
#include "factored_transition_system.h"

namespace task_representation {
class FTSTask;
class TransitionSystem;
}

namespace task_transformation {
class MergeAndShrinkRepresentation;
class TauGraph;

// This Task Reconstruction obtains a plan from the task right after tau-label shrinking
// has been applied to a transition system. This requires us, every time this type of
// shrinking is applied we should restart the merge_and_shrink process from there.

// The plan that we receive contains states where one of two things happens:
// a) ts_index == -1 -> the states do not have any value of our variable. This makes
//    things easier because it means that we only need to introduce a tau-path from
//    the current state to a state where the next label in the plan is executable.

// b) ts_index >= 0 -> the states have a value for ts_index val, therefore we must
// ensure that the tau-path ends in a state s with a transition s -- l --> t where l
// is the next label in the plan and abstraction(t) == val where abstraction is the
// equivalence mapping returned by this abstraction.

class TauShrinking {
    // Index of the transition system in the predecessor and successor task
    int ts_index_predecessor, ts_index_successor; 

    // Tau label graph of the predecessor task
    std::unique_ptr<TauGraph> tau_graph;
    
    // Transition system of the predecessor class: needed to know from which states a label is applicable.
    std::unique_ptr<task_representation::TransitionSystem> transition_system; 

    // Mapping from states to abstract states
    std::vector<int> abstraction;
    
    std::vector<int> haslum_rule_center_state;
    bool is_target(int s, int label, int abstract_target) const;


    int get_concrete_target(int s, int label, int abstract_target) const;
    
    void reconstruct_step(int source, const std::vector<bool> & target, 
                          std::vector<int> & new_label_path,
                          std::vector<PlanState> & new_traversed_states) const ;


public:
    TauShrinking (int ts_index_predecessor_, int ts_index_successor_,
                  std::unique_ptr<TauGraph> tau_graph_,
                  std::vector<int> && abstraction_mapping_,
                  std::unique_ptr<task_representation::TransitionSystem> && transition_system_,
                  const std::vector<int> & haslum_rule_center_state_) :
    ts_index_predecessor (ts_index_predecessor_),
        ts_index_successor (ts_index_successor_),
        tau_graph(move(tau_graph_)),
        transition_system(move(transition_system_)),
        abstraction(move(abstraction_mapping_)), haslum_rule_center_state(haslum_rule_center_state_) {
        }


    int get_ts_index_predecessor() const {
        return ts_index_predecessor;
    }

    int get_ts_index_successor() const {
        return ts_index_successor;
    }
    const std::vector<int> &get_abstraction() const {return abstraction;}
    const TauGraph &get_tau_graph() const {return *tau_graph;}
    void apply_label_mapping(const LabelMapping & label_mapping);

        
    void reconstruct_step(int label,
                          const PlanState & abstract_target, PlanState & concrete_target,
                          std::vector<int> & new_label_path,
                          std::vector<PlanState> & new_traversed_states) const ;

    bool has_tau_path(const PlanState & concrete_source,
                      const PlanState & abstract_target) const;

    
    bool has_tau_path(int label,
                      const PlanState & concrete_source,
                      const PlanState & abstract_target) const;

    void reconstruct_goal_step(std::vector<int> & new_label_path,
                               std::vector<PlanState> & new_traversed_states) const ;

};

class PlanReconstructionTauPath : public PlanReconstruction {
    PlanState initial_state;
    std::vector<std::unique_ptr<TauShrinking> > tau_transformations;
    std::vector<int> transition_system_mapping;
    
    std::vector<TauShrinking *> label_only_relevant_for;

public:
PlanReconstructionTauPath(const FTSMapping & fts_mapping, PlanState initial_state_,
                          std::vector<std::unique_ptr<TauShrinking> > && transformations,
                          std::vector<TauShrinking *> && label_only_relevant_for) :
    initial_state(initial_state_), tau_transformations(move(transformations)),
        transition_system_mapping(fts_mapping.transition_system_mapping),
        label_only_relevant_for(move(label_only_relevant_for)) {
    }
    
    virtual ~PlanReconstructionTauPath() = default;

    const std::vector<std::unique_ptr<TauShrinking>> &get_tau_transformations() const {
        return tau_transformations;
    }
    const std::vector<int> &get_transition_system_mapping() const {return transition_system_mapping;}
    virtual void reconstruct_plan(Plan &plan) const override;

    virtual void print(std::ostream& o) const override;
};
}
#endif
