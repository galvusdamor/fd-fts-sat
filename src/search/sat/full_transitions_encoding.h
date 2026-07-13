#ifndef FULL_TRANSITIONS_ENCODING_H
#define FULL_TRANSITIONS_ENCODING_H

#include "transitions_encoding.h"
#include "../task_representation/transition_system.h"

namespace transitions_encoding {

/**
 * Concrete transition encoding where every non-self-loop transition
 * (and optionally every transition) gets its own SAT variable.
 *
 * Also responsible for initializing relevantLabels (inherited from
 * LabelEncoding), since it is the only class that knows useSelfloopOptimisation.
 */
class FullTransitionsEncoding : public TransitionsEncoding {
private:
    
    // Indexed by time step (push_back once per generateTransitionVars call).
    // Layout: [timestep][ts][label] -> vector of (Transition, SATVar) pairs.
    // SATVar == -1 means the transition has no dedicated variable.
    std::vector<
        std::map<int,
            std::map<int,
                std::vector<std::pair<task_representation::Transition, int>>>>>
        allTimesTransitionVars;

    // Lazily populated caches, keyed by {ts, label, state, timestep}.
    std::map<std::tuple<int,int,int,int>, std::vector<int>> moving_to_state_vars;
    std::map<std::tuple<int,int,int,int>, std::vector<int>> moving_from_state_vars;

    // Non-encoded (implicit self-loop) states per {ts, label}.
    std::map<std::tuple<int,int>, std::vector<int>> states_with_non_encoded_transitions;

    bool useLabelsInEffectsConstraints;

protected:
    void generateTransitionVars(int fromTime) override;

    void encode_transition(const std::vector<std::vector<int>>& previousStateVars, const std::vector<std::vector<int>>& nextStateVars, int fromTime) override;

    void encode_frame_axioms(
        const std::vector<std::vector<int>>& previousStateVars,
        const std::vector<std::vector<int>>& nextStateVars,
        int fromTime) override;

    std::vector<std::vector<int>> extractIntermediateStates(std::vector<int>& selectedLabels, std::vector<int>& currentLastState, std::vector<int>& nextState, int labelTimestep) override;

public:
    explicit FullTransitionsEncoding(
        
        std::shared_ptr<sat_capsule>                          capsule,
        const std::shared_ptr<task_representation::FTSTask>&  fts,
        bool                                                  forceAtLeastOneAction,
        bool                                                  useSelfloopOptimisation
    );
    ~FullTransitionsEncoding() override = default;

    bool has_to_encode_transition(task_representation::Transition transition) override;

    void appendNewTransitionVars(
        std::map<int, std::map<int,
            std::vector<std::pair<task_representation::Transition, int>>>>& newVars);

    void appendPreconditions(std::vector<int>& impliesOrPrec, int ts, int relevantLabelPrec, int src) override;

    void appendEffects(std::vector<int>& impliesOrEff, int ts, int relevantLabelEff, int target) override;

    std::vector<int> get_all_sat_vars_moving_to_state(int ts, int label, int state, int fromTime) override;

    std::vector<int> get_all_sat_vars_moving_from_state(int ts, int label, int state, int fromTime) override;

    std::vector<int> get_all_non_encoded_self_loop_states(int ts, int label) override;

    void encode_label_consistency(int ts, int relevantLabelIndex, int time) override;

    void encode_r2_chains(int ts, int time);

};

class FullTransitionsEncodingFactory : public sat_search::SATEncodingFactory {
	//const bool useLabelGroups;
	const bool useSelfloopOptimisation;
    const bool useLabelsInEffectsConstraints;
	bool statisticsPrinted;

public:
	explicit FullTransitionsEncodingFactory(const options::Options &opts);

	void initialize() override;
	std::unique_ptr<sat_search::SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
    ~FullTransitionsEncodingFactory() override = default;
};

} // namespace transitions_encoding

#endif // FULL_TRANSITIONS_ENCODING_H