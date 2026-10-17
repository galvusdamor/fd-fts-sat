#ifndef TASK_TRANSFORMATION_LABEL_TRANSFORMATION_UTILS_H
#define TASK_TRANSFORMATION_LABEL_TRANSFORMATION_UTILS_H

#include "../task_representation/transition_system.h"

#include <memory>
#include <vector>

namespace task_representation {
class FTSTask;
class Labels;
}

/*
  Helpers for transformations that rewrite the transitions of individual labels
  (tau_baking, label_closure). Transitions are handled per label, not per label group:
  a transformed transition system is built with one group per label and the locally
  equivalent labels are grouped again by the TransitionSystem constructor.
*/
namespace task_transformation {
// transitions[l] = sorted, duplicate-free transitions of label l (for every current label)
std::vector<std::vector<task_representation::Transition>> transitions_by_label(
    const task_representation::TransitionSystem &ts, int num_labels);

// label l only self-loops, in every state
bool is_noop_label(const task_representation::TransitionSystem &ts, int label);

void sort_unique(std::vector<task_representation::Transition> &transitions);

// labels: the costs of the kept labels of `old`, in the order of `kept`
std::unique_ptr<task_representation::Labels> select_labels(
    const task_representation::Labels &old, const std::vector<int> &kept);

std::unique_ptr<task_representation::TransitionSystem> build_transition_system(
    const task_representation::TransitionSystem &like, const task_representation::Labels &labels,
    std::vector<std::vector<task_representation::Transition>> &&transitions_by_label,
    int num_states, std::vector<bool> &&goal_states, int init_state);
}

#endif
