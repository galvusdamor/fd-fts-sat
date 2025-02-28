#include "sat_search.h"

#include "../plugins/options.h"
#include "../utils/logging.h"

using namespace std;

namespace sat_search {
SATSearch(const plugins::Options &opts): SearchAlgorithm(opts),
	planLength(opts.get<int>("plan_length")){

}

void SATSearch::initialize() {
	log << "Initialising" << endl;
}


SearchStatus SATSearch::step() {
	log << "HI doing step!" << endl;
	retuern IN_PROGRESS;
}


void SATSearch::print_statistics() const{
	statistics.print_detailed_statistics();
}

void add_options_to_feature(plugins::Feature &feature) {
    SearchAlgorithm::add_pruning_option(feature);
    SearchAlgorithm::add_options_to_feature(feature);
}
}
