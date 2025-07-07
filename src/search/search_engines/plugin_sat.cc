#include "sat_search.h"
#include "search_common.h"

#include "../option_parser.h"
#include "../plugin.h"

using namespace std;

namespace plugin_sat {
static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis("SAT Search", "");

    parser.add_option<shared_ptr<label_order_finder::LabelOrderFinder>>(
"label_order",
"order for labels",
"label_order_linear()");

                parser.add_option<int>(
            "plan_length",
            "run the search for a single plan length only. -1 if length should not be fixed.",
            "-1");
                parser.add_option<int>(
            "length_iteration",
            "run the search for a single plan length only. -1 if length should not be fixed. Plan length to use is calculated as 5 * sqrt(2)^length_iteration ",
            "-1");
                parser.add_option<int>(
            "start_length",
            "only if length_iteration != -1. Start value for C.",
            "5");
                parser.add_option<double>(
            "multiplier",
            "only if length_iteration != -1. Multiplier for C.",
            "1.41");
                parser.add_option<bool>(
            "length_by_iteration",
            "use the iteration formula of Rinanten's slgorithm C to determine plan lengths.",
            "false");
                parser.add_option<int>(
            "maximum_iteration",
            "if length by iteration, continue iteration also if plan has been found up to and including this iteration. If -1 stop at first plan found",
            "-1");


				parser.add_option<int>(
            "encoding",
            "set the encoding. Currently supported are 0: sequential; 1: R^2E; 2: R^2E without self-loops; 3: BDD full; 4: BDD only one step per factor",
            "0");
                parser.add_option<bool>(
            "impltseitsin",
            "use implicational version of Tseitsin encoding for BDDs",
            "true");
                parser.add_option<bool>(
            "omitforcedvariables",
            "omit variables in the BDD encoding that are forced by the BDD. Only useful in impltseitsin",
            "true");
                parser.add_option<int>(
            "forcedvariablesthreshold",
            "do not omit variables if indegree is greater than forcedvariablesthreshold",
            "100");
                parser.add_option<bool>(
            "combinebdds",
            "combine all BDDs for each factor into one. Otherwise transitions will be encoded for each pair of states in each factor",
            "false");
                parser.add_option<bool>(
            "cutbdds",
            "cut BDDs for the facts with each other to get stronger constraints",
            "false");
                parser.add_option<bool>(
            "coverbdds",
            "try to reformulate BDDs to find more effective representations",
            "false");
                parser.add_option<int>(
            "length_iteration",
            "run the search for a single plan length only. -1 if length should not be fixed. This options run's Rintanen's algorithm C in the round specified by length_iteration",
            "-1");
                parser.add_option<int>(
            "bdd_size_limit",
            "limit on individual BDD sizes for the non-combined encoding",
            "-1");
                parser.add_option<int>(
            "start_length",
            "only if length_iteration != -1. Start value for C.",
            "5");
                parser.add_option<double>(
            "multiplier",
            "only if length_iteration != -1. Multiplier for C.",
            "1.41");
                parser.add_option<int>(
            "step_time_limit",
            "time limit for each step of the SAT run. Defaults to -1, which means no limit",
            "-1");

                parser.add_option<bool>(
            "solver_quiet",
            "if possible try to put the SAT solver into quiet mode (less output to parse for experiments)",
            "false");

    SearchEngine::add_succ_order_options(parser);
    SearchEngine::add_options_to_parser(parser);
    Options opts = parser.parse();


	shared_ptr<sat_search::SATSearch> engine;
    if (!parser.dry_run())
        engine = make_shared<sat_search::SATSearch>(opts);

    return engine;
}
static PluginShared<SearchEngine> _plugin("sat", _parse);
}


