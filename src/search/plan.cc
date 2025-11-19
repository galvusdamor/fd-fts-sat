#include "plan.h"

#include <iostream>
#include <fstream>
#include <cassert>

#include "operator_id.h"
#include "task_representation/fts_task.h"
#include "task_representation/search_task.h"

using namespace std;


PlanState::PlanState(const GlobalState &s) : values (s.get_values()) {
} 

Plan::Plan (const task_representation::FTSTask *task_) :
    task(task_), solved(false) {
    if (task->trivially_solved()) {
        solved = true;
        states.emplace_back(task->get_initial_state());
    }
    }

void Plan::set_plan(std::vector<PlanState> && states_, std::vector<int>&& labels_) {
    states = move(states_);
    labels = move(labels_);
    solved=true;
    assert(states.size() == labels.size() + 1);
}

void Plan::set_plan_operators(const std::vector<GlobalState> & states_,
                              const std::vector<OperatorID> & operators) {

    for (const auto & s : states_) {
        states.push_back(s);
    }

    labels.reserve(operators.size());
    for(auto op : operators) {
        labels.push_back(task->get_search_task()->get_label (op));
    }
    solved=true;
    assert(states.size() == labels.size() + 1);

//    cout << "Plan found" << endl;
//    states[0].dump_fdr();
//    for(size_t i = 0; i < operators.size(); ++i) {
//        cout << endl << " Operator " << operators[i].get_index() << " ";
//        task->get_search_task()->dump_op(operators[i]);
//        cout << endl;
//        states[i+1].dump_fdr();
//    }
}

void Plan::remove_redundant_actions() {
    for (size_t i = 0; i < labels.size(); ++i) {
        if (states[i] == states[i+1]) {
            // Remove action i
            labels.erase(labels.begin() + i);
            states.erase(states.begin() + i + 1);
            --i;
        }
    }
}
ostream &operator<<(ostream &os, const PlanState & s) {
    for (int val : s.values) {
        os << " " << val;
    }

    return os << " ";
}

std::ostream & operator<<(std::ostream &os, const Plan &plan) {
    assert(plan.get_labels().size() + 1 == plan.get_traversed_states().size());
    for (size_t i = 0; i < plan.get_labels().size(); ++i) {
        os << "("<< plan.get_traversed_states()[i] << ") -- " << plan.get_labels()[i] << " --> ";
    }
    os << "(" << plan.get_traversed_states().back() <<")" << endl;
    return os;
}


PlanState::PlanState(const PlanState & other,
                     const std::vector<int> & transition_system_mapping) :
    values(transition_system_mapping.size(), -1) {
    for(size_t i = 0; i < transition_system_mapping.size(); ++i) {
        if (transition_system_mapping[i] >= 0) {
            values[i] = other[transition_system_mapping[i]];
        }
    }
}



PlanState::PlanState(const PlanState & other, const PlanState & default_values) :
    values(other.values) {
    for(size_t i = 0; i < values.size(); ++i) {
        if (values[i] < 0) {
            values[i] = default_values[i];
        }
    }
}


bool PlanState::compatible(const PlanState & other) const {
    if (values.size() != other.values.size()) {
        return false;
    }

    for(size_t i = 0; i < values.size(); ++i) {
        if (values[i] >= 0 && other.values[i] >= 0 && values[i] != other.values[i]) {
            return false;
        }
    }
    return true;

}
