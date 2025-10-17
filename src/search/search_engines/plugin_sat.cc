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

	parser.add_option<shared_ptr<sat_search::LengthStrategy>> (
		"length_strategy","strategy to determine plan lengths", "one_by_one()");
	parser.add_option<shared_ptr<sat_search::SATEncodingFactory>> (
		"encoder","type of formula to use for encoding", "label_sat()");

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


