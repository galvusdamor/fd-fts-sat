#include "transitions_encoding.h"

#include "../utils/logging.h"
#include "ipasir.h"
#include "sat_encoder.h"

using namespace std;
using namespace task_representation;

namespace transitions_encoding {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TransitionsEncoding::TransitionsEncoding(
    shared_ptr<sat_capsule>                         capsule,
    const shared_ptr<FTSTask>&                      fts,
    bool                                            forceAtLeastOneAction,
    bool                                            useSelfloopOptimisation)
    : CommonEncoding(capsule, fts, forceAtLeastOneAction,
                    false, useSelfloopOptimisation)// These encodings do not support label groups
{}

// ---------------------------------------------------------------------------
// CommonEncoding hook
// ---------------------------------------------------------------------------

void TransitionsEncoding::generateAdditionalVariables(int fromTime) {
    generateTransitionVars(fromTime);
}

// ---------------------------------------------------------------------------
// Transition predicate
// ---------------------------------------------------------------------------

bool TransitionsEncoding::isSelfLoop(Transition t) const {
    return t.src == t.target;
}

// ---------------------------------------------------------------------------
// Movement queries
// ---------------------------------------------------------------------------

vector<vector<int>> TransitionsEncoding::compute_possible_label_movements(
    int ts, int label)
{
    set<int> to, from, non;
    for (Transition transition :
         fts->get_ts(ts).get_transitions_with_label(getRelevantLabel(ts, label)))
    {
        if (has_to_encode_transition(transition)) {
            to.insert(transition.target);
            from.insert(transition.src);
        } else {
            non.insert(transition.src);
        }
    }
    vector<int> from_vec(from.begin(), from.end());
    vector<int> to_vec  (to.begin(),   to.end());
    vector<int> non_vec (non.begin(),  non.end());
    return states_label_can_move_from_to_and_non_encoded_state[make_tuple(ts, label)]
               = {from_vec, to_vec, non_vec};
}

vector<int> TransitionsEncoding::get_all_states_label_can_move_from(int ts, int label) {
    auto it = states_label_can_move_from_to_and_non_encoded_state.find(make_tuple(ts, label));
    if (it == states_label_can_move_from_to_and_non_encoded_state.end())
        return compute_possible_label_movements(ts, label)[0];
    return (it->second)[0];
}

vector<int> TransitionsEncoding::get_all_states_label_can_move_to(int ts, int label) {
    auto it = states_label_can_move_from_to_and_non_encoded_state.find(make_tuple(ts, label));
    if (it == states_label_can_move_from_to_and_non_encoded_state.end())
        return compute_possible_label_movements(ts, label)[1];
    return (it->second)[1];
}

// Base version: delegates to the movement cache.
// FullTransitionsEncoding overrides this with its own dedicated cache.
vector<int> TransitionsEncoding::get_all_non_encoded_self_loop_states(int ts, int label) {
    auto it = states_label_can_move_from_to_and_non_encoded_state.find(make_tuple(ts, label));
    if (it == states_label_can_move_from_to_and_non_encoded_state.end())
        return compute_possible_label_movements(ts, label)[2];
    return (it->second)[2];
}

// ---------------------------------------------------------------------------
// Precondition / effect helpers
// ---------------------------------------------------------------------------

vector<int> TransitionsEncoding::getPreconditions(int src, int ts, int relevantLabel) {
    vector<int> impliesOrPrec;
    for (int relevantLabelPrec = 0; relevantLabelPrec < relevantLabel; relevantLabelPrec++)
        appendPreconditions(impliesOrPrec, ts, relevantLabelPrec, src);
    return impliesOrPrec;
}

vector<int> TransitionsEncoding::get_all_later_vars_moving_to_different_state(int target, int ts, int relevantLabel, int fromTime){
    auto access = make_tuple(target, ts, relevantLabel, fromTime);
    auto it = moving_to_different_state_vars.find(access);
    if (it == moving_to_different_state_vars.end()) {
        vector<int> impliesOrEff;
        for (int eff = relevantLabel + 1; eff < getNumRelevantLabels(ts); eff++)
            appendEffects(impliesOrEff, ts, eff, target);
        return moving_to_different_state_vars[access] = impliesOrEff;
    }
    return it->second;
}

// ---------------------------------------------------------------------------
// Core encoding helper
// ---------------------------------------------------------------------------

void TransitionsEncoding::encodeRegularPreconditionsAndEffects(int ts, int relevantLabel, const std::vector<std::vector<int>>& previousStateVars, const std::vector<std::vector<int>>& nextStateVars, int fromTime) {

    // --- Non-self-loop transitions: preconditions ---
    for (int state : get_all_states_label_can_move_from(ts, relevantLabel)) {
        for (int transVar : get_all_sat_vars_moving_from_state(ts, relevantLabel, state, fromTime)) {
            vector<int> impliesOrPrec = getPreconditions(state, ts, relevantLabel);
            impliesOrPrec.push_back(previousStateVars[ts][state]);
            sat->impliesOr(transVar, impliesOrPrec);
        }
    }

    // --- Non-self-loop transitions: effects ---
    for (int state : get_all_states_label_can_move_to(ts, relevantLabel)) {
        for (int transVar : get_all_sat_vars_moving_to_state(ts, relevantLabel, state, fromTime)) {
            vector<int> impliesOrEff = get_all_later_vars_moving_to_different_state(state, ts, relevantLabel, fromTime);
            impliesOrEff.push_back(nextStateVars[ts][state]);
            sat->impliesOr(transVar, impliesOrEff);
        }
    }

    // --- Self-loop transitions (no explicit SAT var): preconditions ---
    vector<int> precsForSelfLoops;
    for (int state : get_all_non_encoded_self_loop_states(ts, relevantLabel)) {
        vector<int> impliesOrPrec = getPreconditions(state, ts, relevantLabel);
        precsForSelfLoops.insert(precsForSelfLoops.end(), impliesOrPrec.begin(), impliesOrPrec.end());
        precsForSelfLoops.push_back(previousStateVars[ts][state]);
    }

    if (!precsForSelfLoops.empty()) {
        vector<int> nonSelfLoopVars;
        for (int state : get_all_states_label_can_move_from(ts, relevantLabel))
            for (int tv : get_all_sat_vars_moving_from_state(ts, relevantLabel, state, fromTime))
                nonSelfLoopVars.push_back(tv);

        precsForSelfLoops.insert(precsForSelfLoops.end(), nonSelfLoopVars.begin(), nonSelfLoopVars.end());
        sat->impliesOr(getLabelSATVar(getRelevantLabel(ts, relevantLabel), fromTime), precsForSelfLoops);
    }
}

// ---------------------------------------------------------------------------
// Aux-var helper
// ---------------------------------------------------------------------------

int TransitionsEncoding::findPreviousValidAuxVar(vector<int>& auxVars, int label) {
    for (int prev = label - 1; prev >= 0; prev--)
        if (auxVars[prev] != -1) return auxVars[prev];
    return -1;
}

} // namespace transitions_encoding