#ifndef TASK_REPRESENTATION_SAS_OPERATOR_H
#define TASK_REPRESENTATION_SAS_OPERATOR_H

#include "../global_state.h"

#include <iostream>
#include <string>
#include <vector>

namespace task_representation {
class SASTask;
void check_magic(std::istream &in, std::string magic);

struct SASCondition {
    int var;
    int val;
    explicit SASCondition(std::istream &in);
    SASCondition(int variable, int value);
    // checked against the given task instead of g_sas_task() (for reading other SAS files)
    SASCondition(std::istream &in, const SASTask &task);
    SASCondition(int variable, int value, const SASTask &task);

    bool is_applicable(const GlobalState &state) const {
        return state[var] == val;
    }

    bool operator==(const SASCondition &other) const {
        return var == other.var && val == other.val;
    }

    bool operator!=(const SASCondition &other) const {
        return !(*this == other);
    }

    bool operator<(const SASCondition &other) const {
        return var < other.var || (var == other.var && val < other.val);
    }


    void dump() const;
};

struct SASEffect {
    int var;
    int val;
    std::vector<SASCondition> conditions;
    explicit SASEffect(std::istream &in);
    SASEffect(int variable, int value, const std::vector<SASCondition> &conds);
    SASEffect(int variable, int value, const std::vector<SASCondition> &conds, const SASTask &task);

    bool does_fire(const GlobalState &state) const {
        for (size_t i = 0; i < conditions.size(); ++i)
            if (!conditions[i].is_applicable(state))
                return false;
        return true;
    }

    bool operator<(const SASEffect &other) const {
        //TODO: Conditions are ignored
        return var < other.var || (var == other.var && val < other.val);
    }



    void dump() const;
};

class SASOperator {
    bool is_an_axiom;
    std::vector<SASCondition> preconditions;
    std::vector<SASEffect> effects;
    std::string name;
    int cost;

    void read_pre_post(std::istream &in, const SASTask &task);
public:
    // the facts are checked against task, the task the operator belongs to
    explicit SASOperator(std::istream &in, bool is_axiom,
                         bool g_use_metric, int & g_min_action_cost, int & g_max_action_cost,
                         const SASTask &task);
    SASOperator (bool _is_an_axiom,
                 std::vector<SASCondition> && _preconditions,
                 std::vector<SASEffect> && _effects,
                 std::string _name,
                 int _cost) : is_an_axiom(_is_an_axiom),
                              preconditions(std::move(_preconditions)),
                              effects (std::move(_effects)),
                              name(_name), cost(_cost) {


    }
    void dump() const;
    const std::string &get_name() const {return name; }

    bool is_axiom() const {return is_an_axiom; }

    const std::vector<SASCondition> &get_preconditions() const {return preconditions; }
    const std::vector<SASEffect> &get_effects() const {return effects; }

    bool is_applicable(const GlobalState &state) const {
        for (size_t i = 0; i < preconditions.size(); ++i)
            if (!preconditions[i].is_applicable(state))
                return false;
        return true;
    }

    int get_cost() const {return cost; }
};
}

//extern int get_op_index_hacked(const SASOperator *op);

#endif
