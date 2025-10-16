#include "length_strategy.h"


namespace sat_search {

    static options::PluginTypePlugin<LengthStrategy> _type_plugin(
    "LengthStrategy",
    "This page describes the various length strategies supported "
    "by the planner.");

    static shared_ptr<LengthStrategy>_parse(options::OptionParser &parser) {
        parser.document_synopsis(
            "Score based filtering merge selector");

        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LengthStrategyOneByOne>(opts);
    }

    static options::PluginShared<LengthStrategy> _plugin("one_by_one", _parse_one_by_one);

    static shared_ptr<LengthStrategy>_parse_by_iteration(options::OptionParser &parser) {
        parser.document_synopsis(
            "Score based filtering merge selector");

        parser.add_option<int>(
    "start_length",
    "only if length_iteration != -1. Start value for C.",
    "5");
        parser.add_option<double>(
    "multiplier",
    "only if length_iteration != -1. Multiplier for C.",
    "1.41");

        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LengthStrategyByIteration>(opts);
    }

    static options::PluginShared<LengthStrategy> _plugin("by_iteration", _parse_by_iteration);

}
