#ifndef SEARCH_ENGINE_H
#define SEARCH_ENGINE_H

#include "operator_cost.h"
#include "search_progress.h"
#include "search_space.h"
#include "search_statistics.h"
#include "state_registry.h"
#include "operator_id.h"
#include "plan.h"
#include "preferred_operators_info.h"

#include <vector>

class Heuristic;

namespace options {
class OptionParser;
class Options;
}

namespace ordered_set {
template<typename T>
class OrderedSet;
}

enum SearchStatus {IN_PROGRESS, TIMEOUT, FAILED, SOLVED};

namespace task_representation {
class SearchTask;
class SuccessorGenerator;
}

class SearchEngine {
private:
    SearchStatus status;
    bool solution_found;
    Plan plan;
protected:
    StateRegistry state_registry;
    SearchSpace search_space;
    SearchProgress search_progress;
    SearchStatistics statistics;
    int bound;
    OperatorCost cost_type;
    double max_time;

    std::shared_ptr<task_representation::SearchTask> task;

    virtual void initialize() {}
    virtual SearchStatus step() = 0;

    bool check_goal_and_set_plan(const GlobalState &state);
    bool check_goal_and_set_plan(const PlanState &goal_state, const std::vector<PlanState> &states, const std::vector<OperatorID> &ops,
                                 const std::shared_ptr<task_representation::FTSTask> &_task);
    bool check_goal_and_set_plan(const PlanState& goal_state, const std::vector<PlanState>& states, const std::vector<int>& labels, const std::shared_ptr<task_representation::FTSTask>& _task);

    int get_adjusted_cost(int cost) const;
public:
    SearchEngine(const options::Options &opts);
    virtual ~SearchEngine();

    virtual void print_statistics() const;
    bool found_solution() const;
    SearchStatus get_status() const;
    const Plan &get_plan() const;
    void search();
    const SearchStatistics &get_statistics() const {return statistics; }
    void set_bound(int b) {bound = b; }
    int get_bound() {return bound; }

    /* The following three methods should become functions as they
       do not require access to private/protected class members. */
    static void add_pruning_option(options::OptionParser &parser);
    static void add_options_to_parser(options::OptionParser &parser);
    static void add_succ_order_options(options::OptionParser &parser);
};

/*
  Print heuristic values of all heuristics evaluated in the evaluation context.
*/
extern void print_initial_h_values(const EvaluationContext &eval_context);

extern ordered_set::OrderedSet<OperatorID> collect_preferred_operators(
    const task_representation::SearchTask & search_task,
    EvaluationContext &eval_context,const std::vector<OperatorID> & applicable_operators, 
    const std::vector<Heuristic *> &preferred_operator_heuristics);

#endif
