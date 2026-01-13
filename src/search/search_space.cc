#include "search_space.h"

#include "task_representation/sas_operator.h"
#include "global_state.h"
#include "globals.h"

#include <cassert>
#include "search_node_info.h"
#include "plan.h"
#include "../task_representation/search_task.h"

using namespace std;

SearchNode::SearchNode(const StateRegistry &state_registry,
                       StateID state_id,
                       SearchNodeInfo &info,
                       OperatorCost cost_type)
    : state_registry(state_registry),
      state_id(state_id),
      info(info),
      cost_type(cost_type) {
    assert(state_id != StateID::no_state);
}

GlobalState SearchNode::get_state() const {
    return state_registry.lookup_state(state_id);
}

bool SearchNode::is_open() const {
    return info.status == SearchNodeInfo::OPEN;
}

bool SearchNode::is_closed() const {
    return info.status == SearchNodeInfo::CLOSED;
}

bool SearchNode::is_dead_end() const {
    return info.status == SearchNodeInfo::DEAD_END;
}

bool SearchNode::is_new() const {
    return info.status == SearchNodeInfo::NEW;
}

int SearchNode::get_g() const {
    assert(info.g >= 0);
    return info.g;
}

int SearchNode::get_real_g() const {
    return info.real_g;
}

void SearchNode::open_initial() {
    assert(info.status == SearchNodeInfo::NEW);
    info.status = SearchNodeInfo::OPEN;
    info.g = 0;
    info.real_g = 0;
    info.parent_state_id = StateID::no_state;
    info.creating_operator = -1;
}

void SearchNode::open(const SearchNode &parent_node, OperatorID creating_operator, int cost) {
    assert(info.status == SearchNodeInfo::NEW);
    info.status = SearchNodeInfo::OPEN;
    info.g = parent_node.info.g + get_adjusted_action_cost(cost, cost_type);
    info.real_g = parent_node.info.real_g + cost;
    info.parent_state_id = parent_node.get_state_id();
    info.creating_operator = creating_operator.get_index();
}

void SearchNode::reopen(const SearchNode &parent_node, OperatorID creating_operator, int cost) {
    assert(info.status == SearchNodeInfo::OPEN ||
           info.status == SearchNodeInfo::CLOSED);

    // The latter possibility is for inconsistent heuristics, which
    // may require reopening closed nodes.
    info.status = SearchNodeInfo::OPEN;
    info.g = parent_node.info.g + get_adjusted_action_cost(cost, cost_type);
    info.real_g = parent_node.info.real_g + cost;
    info.parent_state_id = parent_node.get_state_id();
    info.creating_operator = creating_operator.get_index();
}

// like reopen, except doesn't change status
void SearchNode::update_parent(const SearchNode &parent_node, OperatorID creating_operator, int cost) {
    assert(info.status == SearchNodeInfo::OPEN ||
           info.status == SearchNodeInfo::CLOSED);
    // The latter possibility is for inconsistent heuristics, which
    // may require reopening closed nodes.
    info.g = parent_node.info.g + get_adjusted_action_cost(cost, cost_type);
    info.real_g = parent_node.info.real_g + cost;
    info.parent_state_id = parent_node.get_state_id();
    info.creating_operator = creating_operator.get_index();
}

void SearchNode::close() {
    assert(info.status == SearchNodeInfo::OPEN);
    info.status = SearchNodeInfo::CLOSED;
}

void SearchNode::mark_as_dead_end() {
    info.status = SearchNodeInfo::DEAD_END;
}

void SearchNode::dump() const {
    cout << state_id << ": ";
    get_state().dump_fdr();
    if (info.creating_operator != -1) {
        cout << " created by " << info.creating_operator//.get_name()
             << " from " << info.parent_state_id << endl;
    } else {
        cout << " no parent" << endl;
    }
}

SearchSpace::SearchSpace(StateRegistry &state_registry, OperatorCost cost_type)
    : state_registry(state_registry),
      cost_type(cost_type) {
}

SearchNode SearchSpace::get_node(const GlobalState &state) {
    return SearchNode(
        state_registry, state.get_id(), search_node_infos[state], cost_type);
}

void SearchSpace::trace_path(const GlobalState &goal_state,
                             Plan &plan) const {

    std::vector<GlobalState> states;
    std::vector<OperatorID> operators;
    states.push_back(goal_state);
    assert(plan.empty());
    for (;;) {
        const SearchNodeInfo &info = search_node_infos[states.back()];
        if (info.creating_operator == -1) {
            assert(info.parent_state_id == StateID::no_state);
            break;
        }

        operators.push_back(OperatorID(info.creating_operator));
        states.push_back(state_registry.lookup_state(info.parent_state_id));
    }
    reverse(operators.begin(), operators.end());
    reverse(states.begin(), states.end());

	//cout << "Plan: {";
	//bool first = true;
	//for (OperatorID id : operators) {
	//	if (!first) cout << ", ";
	//	cout << id.get_index();
	//	first = false;
	//}
	//cout << "}" << endl;
    plan.set_plan_operators(states, operators);

}

void SearchSpace::set_path(const PlanState &state, Plan& _plan, const std::vector<PlanState> &states, const std::vector<OperatorID> &ops,
                           const std::shared_ptr<task_representation::FTSTask> &task) const {
    (void)state;
    assert(task->is_goal_state(state));

    _plan.set_plan(std::vector<PlanState>(states), task->get_search_task()->get_labels_from_operator_IDs(ops));
}
//
//void SearchSpace::trace_path(const PlanState &goal_state, const std::vector<PlanState>& path,
//                             Plan &plan, shared_ptr<task_representation::SearchTask> &task) const {
//
//    std::vector<PlanState> states;
//    std::vector<OperatorID> operators;
//    std::vector<int> labels;
//    states.push_back(goal_state);
//    assert(plan.empty());
//
//    plan.set_plan()
//
////
////    for (;;) {
////        const SearchNodeInfo &info = search_node_infos[states.back()];
////        if (info.creating_operator == -1) {
////            assert(info.parent_state_id == StateID::no_state);
////            break;
////        }
////
////        operators.emplace_back(info.creating_operator);
////        labels.push_back(task->get_label(operators.back()));
////        states.emplace_back(state_registry.lookup_state(info.parent_state_id));
////    }
////    reverse(operators.begin(), operators.end());
////    reverse(states.begin(), states.end());
//
//    plan.set_plan(move(states), move(labels));
//}

void SearchSpace::dump() const {
    for (StateID id : state_registry) {
        GlobalState state = state_registry.lookup_state(id);
        const SearchNodeInfo &node_info = search_node_infos[state];
        cout << id << ": ";
        state.dump_fdr();
        if (node_info.creating_operator != -1 &&
            node_info.parent_state_id != StateID::no_state) {
            cout << " created by " << node_info.creating_operator//.get_name()
                 << " from " << node_info.parent_state_id << endl;
        } else {
            cout << "has no parent" << endl;
        }
    }
}

void SearchSpace::print_statistics() const {
    cout << "Number of registered states: "
         << state_registry.size() << endl;
}
