#ifndef TASK_TRANSFORMATION_FACTORED_TRANSITION_SYSTEM_H
#define TASK_TRANSFORMATION_FACTORED_TRANSITION_SYSTEM_H

#include "types.h"
#include "label_map.h"
#include "../task_representation/transition_system.h"

#include <memory>
#include <optional>
#include <vector>
#include <set>

namespace task_representation {
class Labels;
class LabelID;
class TransitionSystem;
class FTSTask;
}

namespace task_transformation {
class Distances;
class FactoredTransitionSystem;
class MergeAndShrinkRepresentation;
class LabelMap;
class PlanReconstruction;
class StateMapping;

class FTSConstIterator {
    /*
      This class allows users to easily iterate over the active indices of
      a factored transition system.
    */
    const FactoredTransitionSystem &fts;
    // current_index is the actual iterator
    int current_index;

    void next_valid_index();
public:
    FTSConstIterator(const FactoredTransitionSystem &fts, bool end);
    void operator++();

    int operator*() const {
        return current_index;
    }

    bool operator==(const FTSConstIterator &rhs) const {
        return current_index == rhs.current_index;
    }

    bool operator!=(const FTSConstIterator &rhs) const {
        return current_index != rhs.current_index;
    }
};

class FTSMapping {
public:
    std::vector<int> transition_system_mapping;
    std::vector<int> transition_system_all_mapping;

    LabelMapping label_mapping;

FTSMapping(std::vector<int> && ts_mapping, std::vector<int> && ts_all_mapping,LabelMapping && lm)  :
    transition_system_mapping(std::move(ts_mapping)),
        transition_system_all_mapping(std::move(ts_all_mapping)),
        label_mapping(std::move(lm)){

    }
};

class FactoredTransitionSystem {
    std::unique_ptr<task_representation::Labels> labels;
    // Entries with nullptr have been merged.
    std::vector<std::unique_ptr<task_representation::TransitionSystem>> transition_systems;
    std::vector<std::unique_ptr<MergeAndShrinkRepresentation>> mas_representations;
    std::shared_ptr<task_transformation::LabelMap> label_map;
    std::vector<std::unique_ptr<Distances>> distances;

    const bool compute_init_distances;
    const bool compute_goal_distances;
    int num_active_entries;

    std::vector<std::shared_ptr<PlanReconstruction>> plan_reconstruction_steps;

    std::shared_ptr<task_representation::FTSTask> predecessor_fts_task;

    const bool lossy_mapping;

    /*
      Assert that the factor at the given index is in a consistent state, i.e.
      that there is a transition system, a distances object, and an MSR.
    */
    void assert_index_valid(int index) const;

    /*
      We maintain the invariant that for all factors, distances are always
      computed and all transitions are grouped according to locally equivalent
      labels.
    */
    bool is_component_valid(int index) const;

    void assert_all_components_valid() const;
public:
    FactoredTransitionSystem(std::shared_ptr<task_representation::FTSTask>,
        std::unique_ptr<task_representation::Labels> labels,
        std::vector<std::unique_ptr<task_representation::TransitionSystem>> &&transition_systems,
        std::vector<std::unique_ptr<MergeAndShrinkRepresentation>> &&mas_representations,
        std::vector<std::unique_ptr<Distances>> &&distances,
        const bool compute_init_distances,
        const bool compute_goal_distances,
        Verbosity verbosity,
        const bool lossy);

    FactoredTransitionSystem(FactoredTransitionSystem &&other);
    ~FactoredTransitionSystem();

    // No copying or assignment.
    FactoredTransitionSystem(const FactoredTransitionSystem &) = delete;
    FactoredTransitionSystem &operator=(
        const FactoredTransitionSystem &) = delete;

    // Merge-and-shrink transformations.
    /*
      Apply the given label mapping to the factored transition system by
      updating all transitions of all transition systems. Only for the factor
      at combinable_index, the local equivalence relation over labels must be
      recomputed; for all factors, all labels that are combined by the label
      mapping have been locally equivalent already before.
    */
    void apply_label_mapping(
        const std::vector<std::pair<int, std::vector<int>>> &label_mapping,
        int combinable_index);

    /*
      Apply the given state equivalence relation to the transition system at
      index if it would reduce its size. If the transition system was shrunk,
      update the other components of the factor (distances, MSR) and return
      true, otherwise return false.

      Note that this method is also suitable to be used for a prune
      transformation. All states not mentioned in the state equivalence
      relation are pruned.
    */
    bool apply_abstraction(
        int index,
        const StateEquivalenceRelation &state_equivalence_relation,
        Verbosity verbosity, bool ignore_mas_representation = false);

    /*
      Merge the two factors at index1 and index2.
    */
    int merge(
        int index1,
        int index2,
        Verbosity verbosity);

    /*
      Various extraction methods, rendering the FTS invalid.
    */
    std::vector<std::unique_ptr<task_representation::TransitionSystem>>
        extract_transition_systems();
    std::unique_ptr<MergeAndShrinkRepresentation> extract_mas_representation(int index);
    std::unique_ptr<task_representation::Labels> extract_labels();

    void statistics(int index) const;
    void dump(int index) const;

    const task_representation::TransitionSystem &get_ts(int index) const {
        return *transition_systems[index];
    }

    task_representation::TransitionSystem& get_ts_mutable(int index) const {
        return *transition_systems[index];
    }

    const Distances &get_distances(int index) const {
        return *distances[index];
    }

    void recompute_distances(int index);

    /*
      A factor is solvabe iff the distance of the initial state to some goal
      state is not infinity. Technically, the distance is infinity either if
      the information of Distances is infinity or if the initial state is
      pruned.
    */
    bool is_factor_solvable(int index) const;

    int get_num_active_entries() const {
        return num_active_entries;
    }

    // Used by LabelReduction and MergeScoringFunctionDFP
    const task_representation::Labels &get_labels() const {
        return *labels;
    }

    // The following methods are used for iterating over the FTS
    FTSConstIterator begin() const {
        return FTSConstIterator(*this, false);
    }

    FTSConstIterator end() const {
        return FTSConstIterator(*this, true);
    }

    int get_size() const {
        return transition_systems.size();
    }

    bool is_active(int index) const;

    bool is_irrelevant_label(task_representation::LabelID label) const;
    bool is_externally_relevant_label(task_representation::LabelID label, int index) const;
    int single_ts_effect(task_representation::LabelID label) const;


    bool remove_irrelevant_labels();
    std::vector<int> remove_labels(const std::vector<task_representation::LabelID> & labels);
    bool remove_irrelevant_transition_systems(Verbosity verbosity);
    std::optional<int> remove_transitions_from_goal();

    const task_representation::Labels& get_labels() {
        return *labels;
    }

    bool is_tau_label(int index, task_representation::LabelID label) const;
    std::vector<task_representation::LabelID> get_tau_labels(int index) const;

    bool is_only_goal_relevant (int ts_index) const;

    FTSMapping cleanup
        (const std::set<int> & exclude_transition_systems = std::set<int>());

    void reinitialize_predecessor_task ();

    void add_plan_reconstruction(std::shared_ptr<PlanReconstruction> plan_reconstruction) {
        plan_reconstruction_steps.push_back(plan_reconstruction);
    }
    std::shared_ptr<PlanReconstruction> get_plan_reconstruction();
    std::shared_ptr<task_representation::FTSTask> get_transformed_fts_task();
    Mapping get_mapping();
    const std::vector<std::unique_ptr<task_representation::TransitionSystem>> &get_transition_systems() const;


};
}

#endif
