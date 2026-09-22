#ifndef GLOBALS_H
#define GLOBALS_H

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

class Axiom;
class AxiomEvaluator;
class GlobalOperator;
class GlobalState;
class StateRegistry;

namespace int_packer {
class IntPacker;
}

namespace successor_generator {
class SuccessorGenerator;
}

namespace task_representation {
struct FactPair;
class FTSTask;
class SASTask;
}

namespace task_transformation {
    class PlanReconstruction;
}

namespace utils {
struct Log;
class RandomNumberGenerator;
}

bool test_goal(const GlobalState &state);
/*
  Set generates_multiple_plan_files to true if the planner can find more than
  one plan and should number the plans as FILENAME.1, ..., FILENAME.n.
*/
std::string get_next_plan_name(bool generates_multiple_plan_files = false);
int calculate_plan_cost(const std::vector<const GlobalOperator *> &plan);

void read_everything(std::istream &in);
void dump_everything();

// The following six functions are deprecated. Use task_properties.h instead.
/* bool is_unit_cost(); */
/* bool has_axioms(); */
/* void verify_no_axioms(); */
/* bool has_conditional_effects(); */
/* void verify_no_conditional_effects(); */
/* void verify_no_axioms_no_conditional_effects(); */

void check_magic(std::istream &in, std::string magic);

/* bool are_mutex(const FactPair &a, const FactPair &b); */


/* // TODO: The following five belong into a new Variable class. */

extern int_packer::IntPacker *g_state_packer;

/* extern AxiomEvaluator *g_axiom_evaluator; */
/* extern successor_generator::SuccessorGenerator *g_successor_generator; */
extern std::string g_plan_filename;
// If non-empty, the plan on the *transformed* FTS task (label ids of
// g_main_task, before plan reconstruction) is also written here, one
// label id per line. Read by experiments/optimal_label_order.py.
extern std::string g_fts_plan_filename;
extern int g_num_previously_generated_plans;
extern bool g_is_part_of_anytime_portfolio;

extern std::shared_ptr<task_transformation::PlanReconstruction> g_plan_reconstruction;
extern const std::shared_ptr<task_representation::SASTask> g_sas_task();
extern std::shared_ptr<task_representation::FTSTask> g_main_task;
//extern const std::shared_ptr<FTSTask> g_root_task();


extern utils::Log g_log;

#endif
