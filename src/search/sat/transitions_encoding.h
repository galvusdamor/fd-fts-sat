#ifndef TRANSITIONS_ENCODING_H
#define TRANSITIONS_ENCODING_H

#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "common_encoding.h"
#include "../task_representation/transition_system.h"

namespace transitions_encoding {

/**
 * Abstract base for transition-variable SAT encodings.
 *
 * Hierarchy:
 *   SATEncoding -> StateEncoding -> LabelEncoding -> CommonEncoding
 *                                                 -> TransitionsEncoding
 *                                                      -> FullTransitionsEncoding
 *                                                      -> SplitTransitionsEncoding
 *
 * All label/state queries (getRelevantLabel, getPreviousStateSATVar, …) are
 * inherited from LabelEncoding / StateEncoding; there are no manager objects.
 */
class TransitionsEncoding : public sat_search::CommonEncoding {
protected:
    // Cache: {ts,label} -> { from-states[], to-states[], non-encoded-self-loop-states[] }
    std::map<std::tuple<int,int>, std::vector<std::vector<int>>>
        states_label_can_move_from_to_and_non_encoded_state;

    // Cache: {target,ts,label} -> transition vars moving to a *different* state (later labels)
    std::map<std::tuple<int,int,int,int>, std::vector<int>> moving_to_different_state_vars;

    // -----------------------------------------------------------------------
    // CommonEncoding abstract interface
    // -----------------------------------------------------------------------

    /** Delegates to generateTransitionVars(fromTime). */
    void generateAdditionalVariables(int fromTime) override;

    /** Create and store all SAT transition variables for this time step. */
    virtual void generateTransitionVars(int fromTime) = 0;

    virtual void encode_transition(
        const std::vector<std::vector<int>>& previousStateVars,
        const std::vector<std::vector<int>>& nextStateVars,
        int fromTime) override = 0;

    virtual void encode_frame_axioms(
        const std::vector<std::vector<int>>& previousStateVars,
        const std::vector<std::vector<int>>& nextStateVars,
        int fromTime) override = 0;

    virtual std::vector<std::vector<int>> extractIntermediateStates(
        std::vector<int>& selectedLabels,
        std::vector<int>& currentLastState,
        std::vector<int>& nextState,
        int labelTimestep) override = 0;

public:
    explicit TransitionsEncoding(
        std::shared_ptr<sat_capsule>                         capsule,
        const std::shared_ptr<task_representation::FTSTask>& fts,
        bool                                                 forceAtLeastOneAction,
        bool                                                 useSelfloopOptimisation
    );
    ~TransitionsEncoding() override = default;

    // -----------------------------------------------------------------------
    // Transition predicate
    // -----------------------------------------------------------------------
    bool isSelfLoop(task_representation::Transition t) const;
    virtual bool has_to_encode_transition(task_representation::Transition transition) = 0;

    // -----------------------------------------------------------------------
    // Movement queries (results cached after first call per {ts,label})
    // -----------------------------------------------------------------------
    std::vector<std::vector<int>> compute_possible_label_movements(int ts, int label);
    std::vector<int> get_all_states_label_can_move_from(int ts, int label);
    std::vector<int> get_all_states_label_can_move_to(int ts, int label);
    virtual std::vector<int> get_all_non_encoded_self_loop_states(int ts, int label);

    // -----------------------------------------------------------------------
    // Precondition / effect building blocks
    // -----------------------------------------------------------------------
    std::vector<int> getPreconditions(int src, int ts, int relevantLabel);

    virtual void appendPreconditions(std::vector<int>& impliesOrPrec, int ts, int relevantLabelPrec, int src) = 0;

    std::vector<int> get_all_later_vars_moving_to_different_state(int target, int ts, int relevantLabel, int fromTime);

    virtual void appendEffects(std::vector<int>& impliesOrEff, int ts, int relevantLabelEff, int target, int time) = 0;

    virtual std::vector<int> get_all_sat_vars_moving_to_state(int ts, int label, int state, int fromTime) = 0;

    virtual std::vector<int> get_all_sat_vars_moving_from_state(int ts, int label, int state, int fromTime) = 0;

    // -----------------------------------------------------------------------
    // Encoding helpers
    // -----------------------------------------------------------------------

    /** Encode pre/effect clauses for every transition of relevantLabel in ts. */
    void encodeRegularPreconditionsAndEffects(int ts, int relevantLabel, const std::vector<std::vector<int>>& previousStateVars, const std::vector<std::vector<int>>& nextStateVars, int fromTime);

    /** Enforce the double-sided implication between label and transition vars. */
    virtual void encode_label_consistency(int ts, int relevantLabelIndex, int time) = 0;

    int findPreviousValidAuxVar(std::vector<int>& auxVars, int label);
};

} // namespace transitions_encoding

#endif // TRANSITIONS_ENCODING_H