#include "globals.h"

//#include "axioms.h"
#include "global_state.h"
#include "heuristic.h"

#include "algorithms/int_packer.h"
#include "utils/logging.h"
#include "utils/rng.h"
#include "utils/system.h"
#include "utils/timer.h"

#include "task_representation/state.h"
#include "task_representation/sas_task.h"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using utils::ExitCode;


int calculate_plan_cost(const vector<int> &plan) {
    // TODO: Refactor: this is only used by save_plan (see below) and the SearchEngine
    //       classes and hence should maybe be moved into the SearchEngine (along with
    //       save_plan).
    int plan_cost = 0;
    for (int op : plan) {
        plan_cost += g_sas_task()->get_operator_cost(op);
    }
    return plan_cost;
}

std::string get_next_plan_name(bool generates_multiple_plan_files) {
    // TODO: Refactor: this is only used by the SearchEngine classes
    //       and hence should maybe be moved into the SearchEngine.
    ostringstream filename;
    filename << g_plan_filename;
    int plan_number = g_num_previously_generated_plans + 1;
    if (generates_multiple_plan_files || g_is_part_of_anytime_portfolio) {
        filename << "." << plan_number;
    } else {
        assert(plan_number == 1);
    }
    ++g_num_previously_generated_plans;

    return filename.str();
}

// AxiomEvaluator *g_axiom_evaluator;
// successor_generator::SuccessorGenerator *g_successor_generator;

string g_plan_filename = "sas_plan";
string g_fts_plan_filename;
int g_num_previously_generated_plans = 0;
bool g_is_part_of_anytime_portfolio = false;

const shared_ptr<task_representation::SASTask> g_sas_task() {
    static shared_ptr<task_representation::SASTask> sas_task = make_shared<task_representation::SASTask>();
    return sas_task;
}

std::shared_ptr<task_representation::FTSTask> g_main_task;
std::shared_ptr<task_transformation::PlanReconstruction> g_plan_reconstruction;

utils::Log g_log;
