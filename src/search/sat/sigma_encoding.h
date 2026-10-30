#ifndef SAT_SIGMA_ENCODING_H
#define SAT_SIGMA_ENCODING_H

#include "common_encoding.h"

#include <memory>
#include <vector>

namespace label_order_finder {
class LabelOrderFinder;
}

namespace options {
class Options;
}

/*
  Intermediate-state encoding over an FTS (sigma_sat), after Symbolic Pattern Planning
  (Cardellini, Giunchiglia, Maratea, AIJ 352, 2026): the step semantics is the one of R2E / TAV /
  bdd_sat(one_step_only=false) -- the labels of a step run in the label order, each at most once,
  and a factor may move several times within a step -- but instead of chain ("forbidden")
  variables the encoding keeps, per factor, the factor's state after every label position:
  sigma_i(s) = "after the first i labels of the step the factor is in s" (one-hot).

  Only the states a label changes get new variables: for label l at position i in factor f, the
  sources and targets of l's real (non-self-loop) transitions, C_l. For every other state the
  previous variable is reused (a state where l only self-loops does not change). With p the
  current variables before l and n the new ones (n(s) for s in C_l, p(s) otherwise):

    not fired     -l & p(s) -> n(s),   -l & n(s) -> p(s)                      s in C_l
    precondition  l -> OR p(x)                         x with a transition of l
    effect        l & p(x) -> OR n(y)                  (x, y) transitions of l
    justification l & n(s) -> OR p(x)                  s in C_l, (x, s) transitions of l
    one target    l & p(x) -> -n(y1) | -n(y2)          sources x with several targets

  The first variables of a step are the state variables of time t; the last ones are made
  equivalent to those of time t+1. Labels irrelevant to a factor (self-loops everywhere) are
  skipped there; a label with self-loops only gets just the precondition clause.
*/
namespace sat_search {

struct SigmaLabelData {
    int label;
    std::vector<int> changed;                         // C_l, sorted
    std::vector<int> applicable;                      // states with a transition of l
    std::vector<std::pair<int, std::vector<int>>> targets;      // source -> targets
    std::vector<std::pair<int, std::vector<int>>> predecessors; // s in C_l -> sources into s
};

struct SigmaData {
    std::vector<int> order;                           // label order of the step semantics
    std::vector<std::vector<SigmaLabelData>> factors; // per factor, relevant labels in order
};

class SigmaEncoding : public CommonEncoding {
    std::shared_ptr<const SigmaData> data;
    // per time and factor: (label, [(state, variable)] for its changed states), for extraction
    std::map<int, std::vector<std::vector<std::pair<int, std::vector<std::pair<int, int>>>>>> allTimesUpdates;

protected:
    void generateAdditionalVariables(int fromTime) override;
    void encode_transition(const std::vector<std::vector<int>> &previousStateVars,
                           const std::vector<std::vector<int>> &nextStateVars, int fromTime) override;
    void encode_frame_axioms(const std::vector<std::vector<int>> &previousStateVars,
                             const std::vector<std::vector<int>> &nextStateVars, int fromTime) override;
    std::vector<std::vector<int>> extractIntermediateStates(std::vector<int> &selectedLabels,
                                                            std::vector<int> &currentLastState,
                                                            std::vector<int> &nextState,
                                                            int labelTimestep) override;

public:
    SigmaEncoding(std::shared_ptr<sat_capsule> capsule,
                  const std::shared_ptr<task_representation::FTSTask> &fts,
                  bool forceAtLeastOneAction,
                  std::shared_ptr<const SigmaData> data);
    ~SigmaEncoding() override = default;
};

class SigmaEncodingFactory : public SATEncodingFactory {
    std::shared_ptr<label_order_finder::LabelOrderFinder> labelOrderFinder;
    const static_constraints::Options staticOptions;
    std::shared_ptr<const static_constraints::StaticConstraints> staticConstraints;
    std::shared_ptr<const SigmaData> data;

public:
    explicit SigmaEncodingFactory(const options::Options &opts);
    void initialize() override;
    std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
    ~SigmaEncodingFactory() override = default;
};

}

#endif
