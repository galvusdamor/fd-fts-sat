#include "label_transformation_utils.h"

#include "../task_representation/label_equivalence_relation.h"
#include "../task_representation/labels.h"
#include "../utils/memory.h"

#include <algorithm>
#include <cassert>

using namespace std;
using namespace task_representation;

namespace task_transformation {
vector<vector<Transition>> transitions_by_label(const TransitionSystem &ts, int num_labels) {
    vector<vector<Transition>> result(num_labels);
    for (const GroupAndTransitions &gat : ts) {
        for (int label : gat.label_group) {
            assert(label < num_labels);
            result[label] = gat.transitions;
        }
    }
    return result;
}

bool is_noop_label(const TransitionSystem &ts, int label) {
    const auto &transitions = ts.get_transitions_with_label(label);
    if ((int)transitions.size() != ts.get_size())
        return false;
    for (const Transition &t : transitions)
        if (t.src != t.target)
            return false;
    return true;
}

void sort_unique(vector<Transition> &transitions) {
    sort(transitions.begin(), transitions.end());
    transitions.erase(unique(transitions.begin(), transitions.end()), transitions.end());
}

unique_ptr<Labels> select_labels(const Labels &old, const vector<int> &kept) {
    vector<unique_ptr<Label>> labels;
    labels.reserve(kept.size());
    for (int label : kept)
        labels.push_back(utils::make_unique_ptr<Label>(old.get_label_cost(label)));
    const int size = labels.size();
    return utils::make_unique_ptr<Labels>(move(labels), size);
}

unique_ptr<TransitionSystem> build_transition_system(
    const TransitionSystem &like, const Labels &labels,
    vector<vector<Transition>> &&transitions, int num_states, vector<bool> &&goal_states,
    int init_state) {
    assert((int)transitions.size() == labels.get_size());
    vector<vector<int>> groups(transitions.size());
    for (size_t label = 0; label < transitions.size(); ++label) {
        assert(!transitions[label].empty());
        groups[label] = {int(label)};
    }
    auto relation = utils::make_unique_ptr<LabelEquivalenceRelation>(labels, groups);
    return utils::make_unique_ptr<TransitionSystem>(
        like.get_num_variables(), vector<int>(like.get_incorporated_variables()),
        move(relation), move(transitions), num_states, move(goal_states), init_state, true);
}
}
