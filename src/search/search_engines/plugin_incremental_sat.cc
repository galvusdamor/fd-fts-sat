#include "incremental_sat_search.h"
#include "search_common.h"

#include "../option_parser.h"
#include "../plugin.h"

using namespace std;

namespace plugin_sat {
static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis("Incremental SAT Search", "");

	parser.add_option<shared_ptr<sat_search::LengthStrategy>> (
		"length_strategy","strategy to determine plan lengths", "one_by_one()");
	parser.add_option<shared_ptr<sat_search::SATEncodingFactory>> (
		"encoder","type of formula to use for encoding", "label_sat()");

	parser.add_option<bool>(
		"solver_quiet",
		"if possible try to put the SAT solver into quiet mode (less output to parse for experiments)",
		"false");

	vector<string> incremental_strategy;
	vector<string> incremental_strategy_doc;
	incremental_strategy.push_back("GOAL");
	incremental_strategy_doc.push_back("add new time-steps after the current goal");
	incremental_strategy.push_back("INIT");
	incremental_strategy_doc.push_back("add new time-steps before the current goal");
	incremental_strategy.push_back("MIDDLE");
	incremental_strategy_doc.push_back( "add new time-steps in the middle");
	parser.add_enum_option("strategy",
	                       incremental_strategy,
	                       "incremental strategy",
	                       "GOAL",
	                       incremental_strategy_doc);

    SearchEngine::add_succ_order_options(parser);
    SearchEngine::add_options_to_parser(parser);
    Options opts = parser.parse();


	shared_ptr<sat_search::IncrementalSATSearch> engine;
    if (!parser.dry_run())
        engine = make_shared<sat_search::IncrementalSATSearch>(opts);

    return engine;
}
static PluginShared<SearchEngine> _plugin("incremental_sat", _parse);
}


