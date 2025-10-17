#include "sat_search.h"
#include "search_common.h"

#include "../option_parser.h"
#include "../plugin.h"

using namespace std;

namespace plugin_sat {
static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis("SAT Search", "");

            parser.add_option<bool>(
        "continue_after_first_plan",
        "Normally the planner stops once the first plan is found. If set to true, continue search even if a plan as been found.",
        "false");

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


		vector<string> base_encoding;
		vector<string> base_encoding_doc;
		base_encoding.push_back("SEQUENTIAL");
		base_encoding_doc.push_back(
		    "sequential encoding");
		base_encoding.push_back("SELF_LOOP_PARALLEL");
		base_encoding_doc.push_back(
		    "self loop parallelism");
		base_encoding.push_back("CHAINS_PARALLEL");
		base_encoding_doc.push_back(
		    "chains parallelism");
		parser.add_enum_option("encoding",
		                       base_encoding,
		                       "base encoding to be used",
		                       "SEQUENTIAL",
		                       base_encoding_doc);


		parser.add_option<bool>(
        "use_label_group",
        "use label group optimisation",
        "false");

		parser.add_option<bool>(
        "use_self_loop_optimisation",
        "use optimisation for self loops",
        "false");

		parser.add_option<bool>(
        "use_empty_rows",
        "use separate encoding for empty rows",
        "false");

		parser.add_option<bool>(
        "use_empty_cols",
        "use separate encoding for empty cols",
        "false");

		parser.add_option<bool>(
        "use_empty_pillars",
        "use separate encoding for empty pillars",
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


