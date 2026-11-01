#ifndef SAT_SIGMA_ENCODING_H
#define SAT_SIGMA_ENCODING_H

#include "common_encoding.h"

#include <functional>
#include <map>
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

  one_move=true: exists-step (lax) -- at most one real move per factor and step. A monotone
  variable M_i per factor and label position with changed states ("the factor has moved by
  position i"): entering a state sets it (-sigma_{i-1}(s) & sigma_i(s) -> M_i), and once it is set
  no state can be entered (M_{i-1} & -sigma_{i-1}(s) -> -sigma_i(s)). Every other firing label then
  runs as a self-loop at the factor's current state, i.e. sequential execution in label order.
  strict=true (with one_move): in addition every firing label is applicable at the state at the
  start of the step (Rintanen's exists-step).

  group_size=k: consecutive groups of k labels of the label order form one micro-step (idea of
  Joao). Per factor, a group whose labels include one that is relevant there is encoded as a
  forall-step between the group's start and end sigma (encode_group): at most one label moves the
  factor, every other firing label self-loops at both; new variables only for the states some
  label of the group changes. A group with a single relevant label is the plain sigma micro-step
  (encode_single), so group_size=1 is the plain encoding. Groups trade R2E parallelism inside a
  group for fewer variables.
*/
namespace sat_search {

struct SigmaLabelData {
    int label;
    std::vector<int> changed;                         // C_l, sorted
    std::vector<int> applicable;                      // states with a transition of l
    std::vector<std::pair<int, std::vector<int>>> targets;      // source -> targets
    std::vector<std::pair<int, std::vector<int>>> predecessors; // s in C_l -> sources into s
};

struct SigmaUpdate {
    int label;
    std::vector<std::pair<int, int>> vars;            // (state, variable) of the changed states
    int mover;                                        // group: the label's mover variable; 0: own micro-step; -1: matrix group
    int group;                                        // first position of the label's group (groups only)
};

struct SigmaData {
    bool groupMatrix = true;
    bool groupCompact = false;
    bool groupPureAmo = true;
    std::vector<std::vector<int>> factorBlock;          // factor_groups: per factor, block id per label entry
    std::vector<int> groupOf;                         // label -> micro-step group
    bool oneMove = false;
    bool strict = false;
    std::vector<int> order;                           // label order of the step semantics
    std::vector<std::vector<SigmaLabelData>> factors; // per factor, relevant labels in order
};

class SigmaEncoding : public CommonEncoding {
    std::shared_ptr<const SigmaData> data;
    // per time and factor: (label, [(state, variable)] for its changed states), for extraction
    std::map<int, std::vector<std::vector<SigmaUpdate>>> allTimesUpdates;
    using OneMove = std::function<void(const std::vector<int> &, const std::vector<int> &, int)>;
    void encode_single(int ts, const SigmaLabelData &ld, const std::vector<int> &labelVars,
                       std::vector<int> &cur, int fromTime, std::vector<SigmaUpdate> &updates,
                       const OneMove &oneMove);
    void amoLadder(const std::vector<int> &xs);
    void encode_group_compact(int ts, const std::vector<SigmaLabelData> &labels, size_t begin, size_t end,
                              const std::vector<int> &labelVars, std::vector<int> &cur, int fromTime,
                              std::vector<SigmaUpdate> &updates, const OneMove &oneMove);
    void encode_group_matrix(int ts, const std::vector<SigmaLabelData> &labels, size_t begin, size_t end,
                             const std::vector<int> &labelVars, std::vector<int> &cur, int fromTime,
                             std::vector<SigmaUpdate> &updates, const OneMove &oneMove);
    void encode_group(int ts, const std::vector<SigmaLabelData> &labels, size_t begin, size_t end,
                      const std::vector<int> &labelVars, std::vector<int> &cur, int fromTime,
                      std::vector<SigmaUpdate> &updates, const OneMove &oneMove);

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
    const bool oneMove;
    const bool strict;
    const int groupSize;
    const bool groupMatrix;
    const bool groupCompact;
    const bool groupPureAmo;
    const bool competingGroups;
    const bool groupOrder;
    const bool factorGroups;
    const bool factorGroupsExact;
    const bool groupAllowChains;
    const bool groupKeepEnablers;
    void compute_group_order(SigmaData &d) const;
    const int groupMax;
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
