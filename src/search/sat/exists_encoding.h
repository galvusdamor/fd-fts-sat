#ifndef SAT_EXISTS_ENCODING_H
#define SAT_EXISTS_ENCODING_H

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
  Exists-step encoding over an FTS with labels in a fixed order (exists_sat).

  In a step every factor makes at most one real move. For a factor going from s to s' the
  mover has the transition s -> s'; every other firing label runs as a self-loop, at s if it
  comes before the mover in the label order and at s' if after it. Semantics:
    LAX     exactly that (a label after the mover needs only its self-loop at s')
    STRICT  Rintanen's exists-step: in addition every firing label is applicable at s, the
            state at the start of the step
    FORALL  forall-step with the same machinery, for comparisons: every non-mover needs its
            self-loop at s *and* at s', regardless of its position (no order, no guards)

  Variables per step and factor: mv (label l performs the factor's move) for every label with a
  real transition in the factor, and the prefix M_j <-> "one of the first j movers (in label
  order) fired", which gives at most one mover and says whether a label runs before or after it.

  A non-mover label l is constrained by
    guarded:    l & -mv_l & -M  -> l has a self-loop at the start state
                l & -mv_l &  M  -> l has a self-loop at the end state
  (M = prefix of the movers before l). With exploit_order, a label whose conflicts the label
  order already resolves gets the unguarded l & -mv_l -> self-loop at the start or at the end
  state instead. Conflicts of l with a mover a of the move s -> s' (b = l):
    kind 1  self-loop at s, none at s'            l must run before a
    kind 2  self-loop at s', none at s            l must run after a  (STRICT: and l applicable at s)
  If every kind-1 mover of l is after it and every kind-2 mover before it, l's position needs
  no guard. conflict_order=true reorders the labels by the SCCs of this constraint graph
  (topologically, ties and SCC-internal order by label_order), which resolves every conflict
  outside the SCCs (Rintanen's disabling graph, generalised by kind 2; see
  experiments/fts_dumps/exists_order.py).
*/
namespace sat_search {

enum class ExistsSemantics {STRICT, LAX, FORALL};

// per factor, read-only after initialize()
struct ExistsFactorData {
    // labels relevant in the factor (not a self-loop in every state), in label order
    std::vector<int> labels;
    std::vector<std::vector<int>> selfLoopStates;   // per entry of labels
    std::vector<std::vector<int>> sourceStates;     // states with an outgoing transition
    // real moves of the label: (source, targets != source)
    std::vector<std::vector<std::pair<int, std::vector<int>>>> moves;
    std::vector<int> moverIndex;                    // per entry: index among the movers, or -1
    std::vector<int> movers;                        // entries of labels with a real move, in order
    std::vector<bool> guarded;                      // per entry: needs the before/after guard
    // implied_clauses: per entry, (start state s, end states allowed when the label fires
    // and the factor starts in s), only where that is more restrictive than the factor itself
    std::vector<std::vector<std::pair<int, std::vector<int>>>> implied;
    // break_mover_symmetry: groups of entries with identical transitions in the factor (in
    // label order), only groups with >= 2 movers
    std::vector<std::vector<int>> moverGroups;
    bool allSourcesEverywhere(int k, int numStates) const {
        return (int)sourceStates[k].size() == numStates;
    }
};

struct ExistsData {
    ExistsSemantics semantics;
    bool exploitOrder;
    bool impliedClauses;
    bool breakMoverSymmetry;
    bool moverVariables;
    std::vector<int> order;                         // label order of the step semantics
    std::vector<ExistsFactorData> factors;
};

class ExistsEncoding : public CommonEncoding {
    std::shared_ptr<const ExistsData> data;
    // per step and factor: the prefix ("moved at or before mover candidate j") per mover
    // candidate; plan extraction takes the first true one as the mover
    std::map<int, std::vector<std::vector<int>>> allTimesPrefixVars;

    void encode_factor(int ts, const std::vector<int> &prev, const std::vector<int> &next,
                       const std::vector<int> &labelVars, std::vector<int> &prefix);
    void encode_factor_mv(int ts, const std::vector<int> &prev, const std::vector<int> &next,
                          const std::vector<int> &labelVars, std::vector<int> &prefix);

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
    ExistsEncoding(std::shared_ptr<sat_capsule> capsule,
                   const std::shared_ptr<task_representation::FTSTask> &fts,
                   bool forceAtLeastOneAction,
                   std::shared_ptr<const ExistsData> data);
    ~ExistsEncoding() override = default;
};

class ExistsEncodingFactory : public SATEncodingFactory {
    const ExistsSemantics semantics;
    const bool exploitOrder;
    const bool conflictOrder;
    const bool impliedClauses;
    const bool breakMoverSymmetry;
    const bool moverVariables;
    std::shared_ptr<label_order_finder::LabelOrderFinder> labelOrderFinder;
    const static_constraints::Options staticOptions;
    std::shared_ptr<const static_constraints::StaticConstraints> staticConstraints;
    std::shared_ptr<const ExistsData> data;

public:
    explicit ExistsEncodingFactory(const options::Options &opts);
    void initialize() override;
    std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
    ~ExistsEncodingFactory() override = default;
};

}

#endif
