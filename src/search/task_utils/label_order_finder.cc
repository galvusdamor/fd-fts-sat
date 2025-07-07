#include "label_order_finder.h"

#include "../task_representation/fts_task.h"
#include "../utils/rng.h"
#include "../utils/rng_options.h"
#include "../option_parser.h"
#include "../plugin.h"

using namespace std;
namespace  label_order_finder {

    LabelOrderFinderRandom::LabelOrderFinderRandom(const options::Options &opts)
: rng(utils::parse_rng_from_options(opts)) {
    }

    LabelOrderFinderLinear::LabelOrderFinderLinear(const options::Options &) {
    }

    LabelOrderFinderReverse::LabelOrderFinderReverse(const options::Options &) {
    }
    std::vector<int> LabelOrderFinderLinear::find_order(const task_representation::FTSTask &fts_task) {
        vector<int> order;
        order.reserve(fts_task.get_num_labels());
        // for now just the natural ordering
        for(int l = 0; l < fts_task.get_num_labels(); l++) {
            order.push_back(l);
        }
        return order;
    }
    std::vector<int> LabelOrderFinderReverse::find_order(const task_representation::FTSTask &fts_task) {
        vector<int> order;
        order.reserve(fts_task.get_num_labels());
        // for now just the natural ordering
        for(int l = fts_task.get_num_labels() - 1; l >=0; --l) {
            order.push_back(l);
        }
        return order;
    }

    std::vector<int> LabelOrderFinderRandom::find_order(const task_representation::FTSTask &fts_task) {
        vector<int> order;
        order.reserve(fts_task.get_num_labels());
        // for now just the natural ordering
        for(int l = 0; l < fts_task.get_num_labels(); l++) {
            order.push_back(l);
        }
        rng->shuffle(order);
        return order;
    }


    static options::PluginTypePlugin<LabelOrderFinder> _type_plugin(
        "LabeLOrderFinder",
        "This page describes the various label ordering strategies.");

    static shared_ptr<LabelOrderFinder>_parse_random(OptionParser &parser) {
        parser.document_synopsis("random", "");
        utils::add_rng_options(parser);

        Options opts = parser.parse();
        if (parser.help_mode())
            return nullptr;

        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LabelOrderFinderRandom>(opts);
    }
    static shared_ptr<LabelOrderFinder>_parse_linear(OptionParser &parser) {
        parser.document_synopsis("linear", "");
        Options opts = parser.parse();
        if (parser.help_mode())
            return nullptr;

        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LabelOrderFinderLinear>(opts);
    }

    static shared_ptr<LabelOrderFinder>_parse_reverse(OptionParser &parser) {
        parser.document_synopsis("reverse", "");
        Options opts = parser.parse();
        if (parser.help_mode())
            return nullptr;

        if (parser.dry_run())
            return nullptr;
        else
            return make_shared<LabelOrderFinderReverse>(opts);
    }


    static PluginShared<LabelOrderFinder> _plugin_random("label_order_random", _parse_random);
    static PluginShared<LabelOrderFinder> _plugin_linear("label_order_linear", _parse_linear);
    static PluginShared<LabelOrderFinder> _plugin_reverse("label_order_reverse", _parse_reverse);

}
