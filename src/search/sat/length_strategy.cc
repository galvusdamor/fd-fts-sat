#include "length_strategy.h"

#include <cmath>

#include "../options/option_parser.h"
#include "../options/options.h"
#include "../options/plugin.h"

using namespace std;


namespace sat_search {
    LengthStrategyConstant::LengthStrategyConstant(const options::Options &opts) : plan_length(
        opts.get<int>("plan_length")) {
    }

    LengthStrategyByIteration::LengthStrategyByIteration(const options::Options &opts) : start_length(
            opts.get<int>("start_length")),
        multiplier(opts.get<double>("multiplier")),
        maximum_iteration(opts.get<int>("maximum_iteration")) {
    }

    std::optional<int> LengthStrategyByIteration::get_next_length(int step_number, int) const {
        if (step_number == maximum_iteration) {
            return std::nullopt;
        }
        return lround(0.5 + start_length * std::pow(multiplier, step_number));
    }


    static options::PluginTypePlugin<LengthStrategy> _type_plugin(
        "LengthStrategy",
        "This page describes the various length strategies supported "
        "by the planner.");

    static shared_ptr<LengthStrategy> _parse_one_by_one(options::OptionParser &parser) {
        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LengthStrategyOneByOne>();
    }

    static options::PluginShared<LengthStrategy> _plugin("one_by_one", _parse_one_by_one);

    static shared_ptr<LengthStrategy> _parse_by_iteration(options::OptionParser &parser) {
        parser.add_option<int>(
            "start_length",
            "Start value for C.",
            "5");

        parser.add_option<int>(
            "maximum_iteration",
            "Limit on the number of iterations",
            "5");

        parser.add_option<double>(
            "multiplier",
            "Multiplier for C.",
            "1.41");

        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LengthStrategyByIteration>(opts);
    }

    static options::PluginShared<LengthStrategy> _plugin_it("by_iteration", _parse_by_iteration);

    static shared_ptr<LengthStrategy> _parse_constant(options::OptionParser &parser) {
        parser.add_option<int>(
            "plan_length",
            "value for C.");

        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LengthStrategyConstant>(opts);
    }

    static options::PluginShared<LengthStrategy> _plugin_constant("constant", _parse_constant);

}
