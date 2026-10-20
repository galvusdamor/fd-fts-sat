#ifndef SAT_STATIC_CONSTRAINTS_H
#define SAT_STATIC_CONSTRAINTS_H

#include "../task_utils/dead_labels.h"
#include "../task_utils/state_mutexes.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

class sat_capsule;

namespace options {
class OptionParser;
class Options;
}
namespace task_representation {
class FTSTask;
}

/*
  Constraints that hold at every time step, independently of the encoding: unreachable
  factor states and mutex pairs between factor states (task_utils/state_mutexes), and dead
  labels (task_utils/dead_labels). The option names are those of bdd_sat, so the same
  option string works for every encoder that uses this.

  An encoding computes them once (in its factory's initialize) and adds, per step, a unit
  clause per unreachable state and per dead label and a binary clause per mutex pair.
*/
namespace static_constraints {
struct StaticConstraints {
    std::vector<std::pair<int, int>> unreachable;      // (factor, state)
    std::vector<std::array<int, 4>> mutex_pairs;       // (factor, state, factor, state)
    std::vector<int> dead_labels;
    bool empty() const {return unreachable.empty() && mutex_pairs.empty() && dead_labels.empty();}
};

void add_options(options::OptionParser &parser);

struct Options {
    bool state_mutexes;
    state_mutexes::Options mutex_options;
    int mutex_clauses_max;
    dead_labels::Options dead_label_options;
    explicit Options(const options::Options &opts);
};

// Prints "SATSTAT state_mutexes ..." / "DEADLABELS ..." (or their "skipped" lines).
std::shared_ptr<const StaticConstraints> compute(const task_representation::FTSTask &fts, const Options &opts);

// the clauses of one step: state variables of the step's end, label variables of the step
void encode(sat_capsule &sat, const StaticConstraints &c,
            const std::vector<std::vector<int>> &stateVars, const std::vector<int> &labelVars);
}

#endif
