#ifndef PLAN_H
#define PLAN_H

#include "../utils/hash.h"

#include "global_state.h"
#include <vector>


namespace task_representation {
    class FTSTask;
}
class OperatorID;


// Internal class to represents the plans of a state. It is similar to state, but without
// any particular task because during task reconstruction we don't explicitly have an
// FTSTask object for all the intermediate tasks
class PlanState {
    std::vector<int> values;
public:
explicit PlanState(std::vector<int> &&values)
    : values(std::move(values)) {
    }

    PlanState(const PlanState & other,
              const std::vector<int> & transition_system_mapping);


    PlanState(const PlanState & other, const PlanState & default_values);
    
    ~PlanState() = default;
    PlanState(const PlanState &) = default;

    PlanState(const GlobalState &);

    PlanState &operator=(const PlanState &&other) {
        if (this != &other) {
            values = std::move(other.values);
        }
        return *this;
    }

    bool compatible(const PlanState &other) const;
    
    bool is_complete() const{
        for (int v : values) {
            if (v == -1) return false;
        }
        return true;
    }


    bool operator==(const PlanState &other) const {
        return values == other.values;
    }

    bool operator!=(const PlanState &other) const {
        return !(*this == other);
    }

    std::size_t hash() const {
        std::hash<std::vector<int>> hasher;
        return hasher(values);
    }

    std::size_t size() const {
        return values.size();
    }

    int operator[](std::size_t var_id) const {
        return values[var_id];
    }

    int operator[](int var) const {
        return values[var];
    }

    void set(int var, int val)  {
        values[var]  = val;
    }

    const std::vector<int> &get_values() const {
        return values;
    }

    friend std::ostream &operator<<(std::ostream &os, const PlanState & plan_state);
};


class Plan {
    const task_representation::FTSTask *task;
    bool solved;
    std::vector<int> labels;
    std::vector<PlanState> states;
public:
    Plan (const task_representation::FTSTask *task_);
    ~Plan() = default;

    bool empty() const {
        return !solved;
    }

    void set_plan(std::vector<PlanState> && states, std::vector<int> && labels);
    void set_plan_operators(const std::vector<GlobalState> & states,
                            const std::vector<OperatorID> & operators);

    void remove_redundant_actions();

    const std::vector<int> & get_labels ()const {
        return labels;
    }

    const std::vector<PlanState> & get_traversed_states ()const {
        return states;
    }

    friend std::ostream &operator<<(std::ostream &os, const Plan & plan);

};

#endif
