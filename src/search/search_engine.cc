#include "search_engine.h"

#include "evaluation_context.h"
#include "globals.h"
#include "option_parser.h"
#include "plugin.h"

#include "algorithms/ordered_set.h"

#include "task_representation/search_task.h"

#include "utils/countdown_timer.h"
#include "utils/rng_options.h"
#include "utils/system.h"
#include "utils/timer.h"

#include <cassert>
#include <iostream>
#include <limits>

using namespace std;
using utils::ExitCode;

class PruningMethod;

SearchEngine::SearchEngine(const Options &opts)
    : status(IN_PROGRESS),
      solution_found(false),
      plan(g_main_task.get()),
      state_registry(g_main_task->get_search_task(true)),
      search_space(state_registry,
                   static_cast<OperatorCost>(opts.get_enum("cost_type"))),
      cost_type(static_cast<OperatorCost>(opts.get_enum("cost_type"))),
      max_time(opts.get<double>("max_time")),
      task(g_main_task->get_search_task()) {
    if (opts.get<int>("bound") < 0) {
        cerr << "error: negative cost bound " << opts.get<int>("bound") << endl;
        utils::exit_with(ExitCode::INPUT_ERROR);
    }
    bound = opts.get<int>("bound");
}

SearchEngine::~SearchEngine() {
}

void SearchEngine::print_statistics() const {
    cout << "Bytes per state: "
         << state_registry.get_state_size_in_bytes() << endl;
}

bool SearchEngine::found_solution() const {
    return solution_found;
}

SearchStatus SearchEngine::get_status() const {
    return status;
}

const Plan &SearchEngine::get_plan() const {
    return plan;
}

void SearchEngine::search() {
    initialize();
    utils::CountdownTimer timer(max_time);
    while (status == IN_PROGRESS) {
        status = step();
        if (timer.is_expired()) {
            cout << "Time limit reached. Abort search." << endl;
            status = TIMEOUT;
            break;
        }
    }
    // TODO: Revise when and which search times are logged.
    cout << "Actual search time: " << timer
         << " [t=" << utils::g_timer << "]" << endl;
}

bool SearchEngine::check_goal_and_set_plan(const GlobalState &state) {
    if (task->is_goal_state(state)) {
        cout << "Solution found!" << endl;
        cout << "Trace path" << endl;
		search_space.trace_path(state, plan);
        solution_found = true;
        return true;
    }
    return false;
}

bool SearchEngine::check_goal_and_set_plan(const PlanState& goal_state, const std::vector<PlanState>& states, const std::vector<OperatorID>& ops, const std::shared_ptr<task_representation::FTSTask>& _task) {
    if (_task->is_goal_state(goal_state)) {
        cout << "Solution found!" << endl;
        search_space.set_path(goal_state, plan, states, ops, _task);
        solution_found = true;
        return true;
    }
    cout << "Goal state provided is not actually a goal state." << endl;
    return false;
}

bool SearchEngine::check_goal_and_set_plan(const PlanState& goal_state, const std::vector<PlanState>& states, const std::vector<int>& labels, const std::shared_ptr<task_representation::FTSTask>& _task) {
    if (_task->is_goal_state(goal_state)) {
        cout << "Solution found!" << endl;
        //search_space.set_path(goal_state, plan, states, ops, _task);
        plan.set_plan(std::vector<PlanState>(states),  std::vector<int>(labels));
        solution_found = true;
        return true;
    }
    cout << "Goal state provided is not actually a goal state." << endl;
    return false;
}

int SearchEngine::get_adjusted_cost(int cost) const {
    return get_adjusted_action_cost(cost, cost_type);
}

/* TODO: merge this into add_options_to_parser when all search
         engines support pruning.

   Method doesn't belong here because it's only useful for certain derived classes.
   TODO: Figure out where it belongs and move it there. */
void SearchEngine::add_pruning_option(OptionParser &parser) {
    parser.add_option<shared_ptr<PruningMethod>>(
        "pruning",
        "Pruning methods can prune or reorder the set of applicable operators in "
        "each state and thereby influence the number and order of successor states "
        "that are considered.",
        "null()");
}

void SearchEngine::add_options_to_parser(OptionParser &parser) {
    ::add_cost_type_option_to_parser(parser);
    parser.add_option<int>(
        "bound",
        "exclusive depth bound on g-values. Cutoffs are always performed according to "
        "the real cost, regardless of the cost_type parameter", "infinity");
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds the search is allowed to run for. The "
        "timeout is only checked after each complete search step "
        "(usually a node expansion), so the actual runtime can be arbitrarily "
        "longer. Therefore, this parameter should not be used for time-limiting "
        "experiments. Timed-out searches are treated as failed searches, "
        "just like incomplete search algorithms that exhaust their search space.",
        "infinity");
}

/* Method doesn't belong here because it's only useful for certain derived classes.
   TODO: Figure out where it belongs and move it there. */
void SearchEngine::add_succ_order_options(OptionParser &parser) {
    vector<string> options;
    parser.add_option<bool>(
        "randomize_successors",
        "randomize the order in which successors are generated",
        "false");
    parser.add_option<bool>(
        "preferred_successors_first",
        "consider preferred operators first",
        "false");
    parser.document_note(
        "Successor ordering",
        "When using randomize_successors=true and "
        "preferred_successors_first=true, randomization happens before "
        "preferred operators are moved to the front.");
    utils::add_rng_options(parser);
}

void print_initial_h_values(const EvaluationContext &eval_context) {
    eval_context.get_cache().for_each_heuristic_value(
        [] (const Heuristic *heur, const EvaluationResult &result) {
        cout << "Initial heuristic value for "
             << heur->get_description() << ": ";
        if (result.is_infinite())
            cout << "infinity";
        else
            cout << result.get_h_value();
        cout << endl;
    }
        );
}


static PluginTypePlugin<SearchEngine> _type_plugin(
    "SearchEngine",
    // TODO: Replace empty string by synopsis for the wiki page.
    "");


ordered_set::OrderedSet<OperatorID> collect_preferred_operators(
    const task_representation::SearchTask & search_task,
    EvaluationContext &eval_context, const std::vector<OperatorID> & applicable_operators,
    const vector<Heuristic *> &preferred_operator_heuristics) {
    ordered_set::OrderedSet<OperatorID> preferred_operators;
    for (Heuristic *heuristic : preferred_operator_heuristics) {
        /*
          Unreliable heuristics might consider solvable states as dead
          ends. We only want preferred operators from finite-value
          heuristics.
        */
        if (!eval_context.is_heuristic_infinite(heuristic)) {
            eval_context.get_preferred_operators(search_task, applicable_operators,
                                                 heuristic, preferred_operators);
        }
    }
    return preferred_operators;
}
