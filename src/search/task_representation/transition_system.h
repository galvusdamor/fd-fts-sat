#ifndef TASK_REPRESENTATION_TRANSITION_SYSTEM_H
#define TASK_REPRESENTATION_TRANSITION_SYSTEM_H

#include "types.h"
#include "../task_transformation/types.h"
#include "../task_transformation/label_map.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <set>
#include <unordered_map>

namespace task_transformation {
class Distances;
}

using namespace task_transformation;

namespace task_representation {
class LabelEquivalenceRelation;
class LabelGroup;
class Labels;

struct Transition {
    int src;
    int target;

    Transition(int src, int target)
        : src(src), target(target) {
    }

    bool operator==(const Transition &other) const {
        return src == other.src && target == other.target;
    }

    bool operator<(const Transition &other) const {
        return src < other.src || (src == other.src && target < other.target);
    }

    // Required for "is_sorted_unique" in utilities
    bool operator>=(const Transition &other) const {
        return !(*this < other);
    }
};

struct GroupAndTransitions {
    LabelGroupID group_id;
    const LabelGroup &label_group;
    const std::vector<Transition> &transitions;
    GroupAndTransitions(LabelGroupID id, const LabelGroup &label_group,
                       const std::vector<Transition> &transitions)
        : group_id (id), label_group(label_group),
        transitions(transitions) {
    }
};

class TSConstIterator {
    /*
      This class allows users to easily iterate over both label groups and
      their transitions of a TransitionSystem. Most importantly, it hides
      the data structure used by LabelEquivalenceRelation, which could be
      easily exchanged.
    */
    const LabelEquivalenceRelation &label_equivalence_relation;
    const std::vector<std::vector<Transition>> &transitions_by_group_id;
    // current_group_id is the actual iterator
    LabelGroupID current_group_id;

    void next_valid_index();
public:
    TSConstIterator(const LabelEquivalenceRelation &label_equivalence_relation,
                    const std::vector<std::vector<Transition>> &transitions_by_group_id,
                    bool end);
    void operator++();
    GroupAndTransitions operator*() const;

    bool operator==(const TSConstIterator &rhs) const {
        return current_group_id == rhs.current_group_id;
    }

    bool operator!=(const TSConstIterator &rhs) const {
        return current_group_id != rhs.current_group_id;
    }
};

class TransitionSystem {
private:
    /*
      The following two attributes are only used for output.

      - num_variables: total number of variables in the factored
        transition system

      - incorporated_variables: variables that contributed to this
        transition system
    */
    const int num_variables;
    std::vector<int> incorporated_variables;

    /*
      All locally equivalent labels are grouped together, and their
      transitions are only stored once for every such group, see below.

      LabelEquivalenceRelation stores the equivalence relation over all
      labels of the underlying factored transition system.
    */
    std::unique_ptr<LabelEquivalenceRelation> label_equivalence_relation;

    /*
      The transitions of a label group are indexed via its ID. The ID of a
      group does not change, and hence its transitions are never moved.

      We tested different alternatives to store the transitions, but they all
      performed worse: storing a vector transitions in the label group increases
      memory usage and runtime; storing the transitions more compactly and
      incrementally increasing the size of transitions_of_groups whenever a
      new label group is added also increases runtime. See also issue492 and
      issue521.
    */
    std::vector<std::vector<Transition>> transitions_by_group_id;

    int num_states;
    std::vector<bool> goal_states;
    int init_state;

    //list of goal states
    mutable std::vector<int> goal_state_list;

    //for each label group, stores the set of states in which the label can be applied.
    mutable std::vector<std::vector<int>> label_group_precondition;

    //List of label groups that have a non-self-loop transition
    mutable std::vector<LabelGroupID> relevant_label_groups;

    //List of label groups that have a selfloop transition in every state
    mutable std::vector<bool> selfloop_everywhere_label_groups;

    /*
      Check if two or more labels are locally equivalent to each other, and
      if so, update the label equivalence relation.
    */
    void compute_locally_equivalent_labels();

    // Statistics and output
    std::string get_description() const;

public:
    TransitionSystem(
        int num_variables,
        std::vector<int> &&incorporated_variables,
        std::unique_ptr<LabelEquivalenceRelation> &&label_equivalence_relation,
        std::vector<std::vector<Transition>> &&transitions_by_label,
        int num_states,
        std::vector<bool> &&goal_states,
        int init_state,
        bool compute_label_equivalence_relation);
    TransitionSystem(const TransitionSystem &other);
    TransitionSystem(const TransitionSystem &other, const Labels &labels);
    ~TransitionSystem();
    /*
      Factory method to construct the merge of two transition systems.

      Invariant: the children ts1 and ts2 must be solvable.
      (It is a bug to merge an unsolvable transition system.)
    */
    static std::unique_ptr<TransitionSystem> merge(
        const Labels &labels,
        const TransitionSystem &ts1,
        const TransitionSystem &ts2,
        Verbosity verbosity);
    /*
      Applies the given state equivalence relation to the transition system.
      abstraction_mapping is a mapping from old states to new states, and it
      must be consistent with state_equivalence_relation in the sense that
      old states are only mapped to the same new state if they are in the same
      equivalence class as specified in state_equivalence_relation.
    */
    void apply_abstraction(
        const StateEquivalenceRelation &state_equivalence_relation,
        const std::vector<int> &abstraction_mapping,
        Verbosity verbosity);

    /*
      Applies the given label mapping, mapping old to new label numbers. This
      updates the label equivalence relation which is internally used to group
      locally equivalent labels and store their transitions only once.
    */
    void apply_label_reduction(
        const std::vector<std::pair<int, std::vector<int>>> &label_mapping,
        bool only_equivalent_labels);

    void apply_label_mapping(const task_transformation::LabelMapping &label_mapping);
    bool remove_labels(const std::vector<LabelID> & labels);

    TSConstIterator begin() const {
        return TSConstIterator(*label_equivalence_relation,
                               transitions_by_group_id,
                               false);
    }

    TSConstIterator end() const {
        return TSConstIterator(*label_equivalence_relation,
                               transitions_by_group_id,
                               true);
    }

    /*
      Method to identify the transition system in output.
      Print "Atomic transition system #x: " for atomic transition systems,
      where x is the variable. For composite transition systems, print
      "Transition system (x/y): " for a transition system containing x
      out of y variables.
    */
    std::string tag() const;

    /*
      The transitions for every group of locally equivalent labels are
      sorted (by source, by target) and there are no duplicates.
    */
    bool are_transitions_sorted_unique() const;

    int compute_total_transitions() const;
    bool is_solvable(const Distances &distances) const;

    void dump_dot_graph() const;
    void dump_labels_and_transitions() const;
    void statistics() const;
    bool is_unit_cost() const;

    const std::vector<Transition> &get_transitions_for_group_id(int group_id) const {
        return transitions_by_group_id[group_id];
    }

    int get_size() const {
        return num_states;
    }

    int get_init_state() const {
        return init_state;
    }

    bool is_goal_state(int state) const {
        return goal_states[state];
    }

    const std::vector<int> &get_incorporated_variables() const {
        return incorporated_variables;
    }

    const std::vector<int> & get_goal_states() const;

    const std::vector<bool> & get_is_goal() const {
        return goal_states;
    }

    bool is_goal_relevant() const {
        return get_goal_states().size() < (size_t)num_states;
    }

    const std::vector<int> & get_label_precondition(LabelID label) const;

    bool is_relevant_label (LabelID label) const;

    int num_label_groups () const;
    bool is_relevant_label_group (LabelGroupID group_id) const;

    const std::vector<LabelGroupID> & get_relevant_label_groups() const;

    bool has_precondition_on (LabelID label) const {
        return get_label_precondition(label).size() < (size_t)num_states;
    }

    const LabelGroup &get_label_group(LabelGroupID group_id) const;

    LabelGroupID get_label_group_id_of_label(LabelID label_id) const;

    bool is_selfloop_everywhere(LabelID label) const;

    const std::vector<Transition> &get_transitions_with_label(int label_id) const ;

    friend std::ostream &operator<<(std::ostream &os, const TransitionSystem &tr);

    void check_dead_labels(std::set<LabelID> & dead_labels) const;

    bool remove_transitions_from_goal();

    void remove_transitions_for_labels(std::unordered_map<int, std::set<Transition>>& label_to_transitions);

	bool isAlwaysSelfLoop(int label) const;

	// is always a self-loop and it is a self-loop for all states.
	bool isIrrelevantLabel(int label) const;
	
	bool hasSelfLoopOnValue(int state, int label) const;
};
}

#endif
