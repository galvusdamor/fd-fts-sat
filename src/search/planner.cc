#include "option_parser.h"
#include "search_engine.h"

#include "utils/system.h"
#include "utils/timer.h"
#include "utils/logging.h"

#include "task_representation/fts_task.h"
#include "task_representation/labels.h"
#include "task_representation/sas_task.h"
#include "task_representation/transition_system.h"

#include "task_transformation/fts_factory.h"
#include "task_transformation/task_transformation.h"
#include "task_transformation/plan_reconstruction.h"

#include <iostream>

using namespace std;
using utils::ExitCode;

using namespace task_representation;
using namespace task_transformation;

int main(int argc, const char **argv) {
    utils::register_event_handlers();

    if (argc < 2) {
        cout << OptionParser::usage(argv[0]) << endl;
        utils::exit_with(ExitCode::INPUT_ERROR);
    }

    if (static_cast<string>(argv[1]) != "--help") {
        g_sas_task()->read_from_file(cin);
        if (g_sas_task()->get_num_axioms()) {
            cerr << "This configuration does not support axioms!"
                 << endl << "Terminating." << endl;
            utils::exit_with(ExitCode::UNSUPPORTED);
        }
        auto labels_and_transition_systems = create_labels_and_transition_systems(*g_sas_task());
        g_main_task = make_shared<FTSTask>(
            move(labels_and_transition_systems.second),
            move(labels_and_transition_systems.first));
        cout << "Main task constructed" << endl;
    }

    shared_ptr<TaskTransformation> transformer;

    // The command line is parsed twice: once in dry-run mode, to check for simple input
    // errors, and then in normal mode.
    bool is_unit_cost = g_sas_task()->is_unit_cost();
    try {
        OptionParser::parse_cmd_line_transform(argc, argv, true, is_unit_cost);
        transformer = OptionParser::parse_cmd_line_transform(argc, argv, false, is_unit_cost);
    } catch (ArgError &error) {
        cerr << error << endl;
        OptionParser::usage(argv[0]);
        utils::exit_with(ExitCode::INPUT_ERROR);
    } catch (ParseError &error) {
        cerr << error << endl;
        utils::exit_with(ExitCode::INPUT_ERROR);
    }

    utils::Timer transform_timer;
    if (transformer) {
        cout << "Transform task... " << endl;
        auto transformation = transformer->transform_task(g_main_task);
        g_main_task = transformation.first;
        g_plan_reconstruction = transformation.second;
        //TODO is_unit_cost = g_main_task->is_unit_cost();
    } else {
        cout << "No further transformation of atomic FTS." << endl;
    }
    cout << "Transform time: " << transform_timer << endl;

    cout << "Main task: " << *g_main_task << endl;
    if (g_main_task->trivially_solved()) {
        cout << "Task solved without search" << endl;
        Plan plan (g_main_task.get());
        if (transformer) {
            utils::Timer reconstruct_timer;
            g_plan_reconstruction->reconstruct_plan(plan);
            cout << "Plan reconstruction time: " << reconstruct_timer << endl;
        }
        g_sas_task()->save_plan(plan.get_labels(),get_next_plan_name());
        cout << "Total time: " << utils::g_timer << endl;
        utils::exit_with(ExitCode::PLAN_FOUND);
    }

    shared_ptr<SearchEngine> engine;

    // The command line is parsed twice: once in dry-run mode, to
    // check for simple input errors, and then in normal mode.
    try {
        OptionParser::parse_cmd_line(argc, argv, true, is_unit_cost);
        engine = OptionParser::parse_cmd_line(argc, argv, false, is_unit_cost);
    } catch (ArgError &error) {
        cerr << error << endl;
        OptionParser::usage(argv[0]);
        utils::exit_with(ExitCode::INPUT_ERROR);
    } catch (ParseError &error) {
        cerr << error << endl;
        utils::exit_with(ExitCode::INPUT_ERROR);
    }

    utils::Timer search_timer;
    cout << "Start search." << endl;

    engine->search();
    search_timer.stop();

    engine->print_statistics();
    cout << "Search time: " << search_timer << endl;

    if (engine->found_solution()) {
        Plan plan = engine->get_plan();

        plan.remove_redundant_actions();
        // cout << plan << endl;

        if (transformer) {
            utils::Timer reconstruct_timer;
            cout << "Plan reconstruction " << *g_plan_reconstruction << endl;
            g_plan_reconstruction->reconstruct_plan(plan);

            cout << "Plan reconstruction time: " << reconstruct_timer << endl;
        }

        g_sas_task()->save_plan(plan.get_labels(),get_next_plan_name());
        cout << "Total time: " << utils::g_timer << endl;
        utils::exit_with(ExitCode::PLAN_FOUND);
    } else {
        cout << "Total time: " << utils::g_timer << endl;
        utils::exit_with(ExitCode::UNSOLVED_INCOMPLETE);
    }
}
