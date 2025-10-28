#include "rintanen_search.h"
#include "search_common.h"

#include "../option_parser.h"
#include "../plugin.h"

using namespace std;

namespace plugin_sat {
static shared_ptr<SearchEngine> _parse(OptionParser &parser) {
    parser.document_synopsis("Rintanen's Parallel SAT Search", "");

	parser.add_option<shared_ptr<sat_search::LengthStrategy>> (
		"length_strategy","strategy to determine plan lengths", "one_by_one()");
	parser.add_option<shared_ptr<sat_search::SATEncodingFactory>> (
		"encoder","type of formula to use for encoding", "label_sat()");
	
	parser.add_option<int> (
		"max_parallel_calls","maximum number of SAT calls run in parallel", "20");
	parser.add_option<int> (
		"scheduler_interval","interval for the schedule in seconds", "1");
	parser.add_option<int> (
		"memory_limit_mb","memory limit in MBs", "3500");
	parser.add_option<bool> (
		"schedule_formula_as_one","schedule the creation of a formula as one item (helps keeping to the memory limit)", "true");

	parser.add_option<bool>(
		"solver_quiet",
		"if possible try to put the SAT solver into quiet mode (less output to parse for experiments)",
		"false");

    SearchEngine::add_succ_order_options(parser);
    SearchEngine::add_options_to_parser(parser);
    Options opts = parser.parse();


	shared_ptr<sat_search::RintanenSATSearch> engine;
    if (!parser.dry_run())
        engine = make_shared<sat_search::RintanenSATSearch>(opts);

    return engine;
}
static PluginShared<SearchEngine> _plugin("rintanen", _parse);
}


