#include "length_strategy.h"

#include "../utils/system.h"
#include <iostream>

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
        maximum_iteration(opts.get<int>("maximum_iteration")),
        strictly_increasing(opts.get<bool>("strictly_increasing")) {
    }

    std::optional<int> LengthStrategyByIteration::get_next_length(int step_number, int previous_length) const {
        if (step_number == maximum_iteration) {
            return std::nullopt;
        }
        const int length = lround(0.5 + start_length * std::pow(multiplier, step_number));
        // small multipliers round to the same length several times in a row
        if (strictly_increasing && length <= previous_length) return previous_length + 1;
        return length;
    }

    LengthStrategyByIncrement::LengthStrategyByIncrement(const options::Options &opts)
        : start_length(opts.get<int>("start_length")),
          increment(opts.get<int>("increment")),
          maximum_iteration(opts.get<int>("maximum_iteration")) {
    }

    std::optional<int> LengthStrategyByIncrement::get_next_length(int step_number, int) const {
        if (step_number == maximum_iteration) {
            return std::nullopt;
        }
        return start_length + step_number * increment;
    }


    static options::PluginTypePlugin<LengthStrategy> _type_plugin(
        "LengthStrategy",
        "This page describes the various length strategies supported "
        "by the planner.");

    static shared_ptr<LengthStrategy> _parse_one_by_one(options::OptionParser &parser) {
        parser.add_option<int>(
            "maximum_length",
            "last length to try; -1 = no limit",
            "-1");
        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LengthStrategyOneByOne>(opts.get<int>("maximum_length"));
    }

    static options::PluginShared<LengthStrategy> _plugin("one_by_one", _parse_one_by_one);

	// This is Rintanen's "Algorithm C"
    static shared_ptr<LengthStrategy> _parse_by_iteration(options::OptionParser &parser) {
        parser.add_option<int>(
            "start_length",
            "Start value for plan length. Further lengths will be start_length * (multiplier)^iteration.",
            "5");
        parser.add_option<int>(
	        "maximum_iteration",
    	    "Number of iterations to be performed. If set to -1, there is no limit. ",
       		"-1");

        parser.add_option<double>(
            "multiplier",
            "Multiplier for iteration.",
            "1.41");
        parser.add_option<bool>(
            "strictly_increasing",
            "if the formula rounds to a length not larger than the previous one, "
            "use previous+1 instead (small multipliers otherwise repeat lengths)",
            "false");

        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LengthStrategyByIteration>(opts);
    }

    static options::PluginShared<LengthStrategy> _plugin_it("by_iteration", _parse_by_iteration);

    static shared_ptr<LengthStrategy> _parse_by_increment(options::OptionParser &parser) {
        parser.add_option<int>("start_length", "first length", "1");
        parser.add_option<int>("increment", "added per iteration", "1");
        parser.add_option<int>("maximum_iteration",
            "Number of iterations to be performed. If set to -1, there is no limit.", "-1");
        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LengthStrategyByIncrement>(opts);
    }

    static options::PluginShared<LengthStrategy> _plugin_inc("by_increment", _parse_by_increment);

    static shared_ptr<LengthStrategy> _parse_length_list(options::OptionParser &parser) {
        parser.add_list_option<int>("lengths", "the lengths to try, in this order");
        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        vector<int> lengths = opts.get_list<int>("lengths");
        if (lengths.empty()) {
            cerr << "length_list: lengths must not be empty" << endl;
            utils::exit_with(utils::ExitCode::INPUT_ERROR);
        }
        return make_shared<LengthStrategyList>(lengths);
    }

    static options::PluginShared<LengthStrategy> _plugin_list("length_list", _parse_length_list);

    static shared_ptr<LengthStrategy> _parse_constant(options::OptionParser &parser) {
        parser.add_option<int>(
            "plan_length",
            "the plan length to try");

        options::Options opts = parser.parse();
        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LengthStrategyConstant>(opts);
    }

    static options::PluginShared<LengthStrategy> _plugin_constant("constant", _parse_constant);

}
