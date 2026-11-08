#ifndef SAT_STEP_ENCODING_H
#define SAT_STEP_ENCODING_H

#include "common_encoding.h"
#include "static_constraints.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace label_order_finder {
class LabelOrderFinder;
}

namespace options {
class Options;
class OptionParser;
}

/*
  One encoding of a time step for all label-based encodings (label_sat, sigma_sat,
  micro_steps_sat are configurations of it).

  Per factor, a time step is a sequence of *segments* between *layers* of state variables:
  layer 0 are the state variables of time t, the last layer those of time t+1. A segment is a set
  of labels executed with forall-step semantics between its start and end layer; the segments of
  a factor follow the label order, so between segments the step is sequential (R2E).

    label_sat        one segment per factor holding every label (forall step; chains or
                     self-loop parallelism, or exists-step via the exists chains)
    sigma_sat        one segment per label (the intermediate-state / micro-step encoding), or
                     blocks of labels (group_size, competing / ordered / per-factor groups)
    micro_steps_sat  one segment per label, with the row-wise primal / dual clauses of
                     micro_steps_encoding (Joao)

  The base class owns the layers: which states get a new variable in a segment's end layer (only
  the states the segment's labels move; the others keep the previous layer's variable), whether a
  state's last change in the step is the t+1 state variable, and whether the t+1 state variables
  are generated up front (label_sat, sigma_sat) or are the last layer itself, so that a state no
  label of the step moves keeps its variable from time t (micro_steps_sat).

  Segments are encoded by the case that applies:
    - all labels of a factor (label_sat): label_sat's projection encoding over the factor's
      (source, target, label group) matrix, frame axioms or self-loop variables, and the
      parallelism constraints (sequential, self-loop parallel, forall chains, exists chains);
    - a single label: no parallelism constraints are needed; three clause styles (SUPPORT,
      PROJECTION, ROWS), see encode_single*;
    - several labels whose order guarantees that none can feed another in the factor (exact
      per-factor blocks): a compact forall encoding (encode_group_compact);
    - several arbitrary labels: forall with mover variables or label_sat's conditions
      (encode_group, encode_group_matrix).
*/
namespace sat_search {
class FTSMatrix;

enum encoding_type {
	SEQUENTIAL,
	SELF_LOOP_PARALLEL,
	CHAINS_PARALLEL
};

// ---------------------------------------------------------------------------
// label_sat: exists_step data
// ---------------------------------------------------------------------------

/*
  exists_step=true: Rintanen's (strict) exists-step semantics on top of the forall encoding.

  In a step every factor makes at most one real move s -> t, by the first firing label (in the
  label order `label_order`) that has the transition s -> t. Every other firing label runs as a
  self-loop: at s if it comes before the mover, at t if after it; all firing labels are
  applicable at s (strict). The forall encoding is changed in three ways:
    - the forall chains (CHAINS_PARALLEL) are not emitted;
    - clauses that forbid a label l together with prev = s, next = t (s != t) because (s, t) is
      not a transition of l are weakened: if l self-loops at s only, l must come before the
      mover; at t only, after it; at both, no clause;
    - the label -> possible targets clauses (empty cols, and the self-loop optimisation's
      "always self-loop label -> next state is a self-loop state") are dropped for labels with
      a self-loop, since a label before the mover need not self-loop at t.
  The mover's position comes from one prefix chain per (factor, target state t) over the labels
  with a real transition into t ("adders" of t), in label order: I_t[k] = "the factor moved into
  t by adder k or earlier". Events: l & prev_u & next_t -> I_t[k(l)] for every real transition
  u -> t of l; upper bound: I_t[k] -> I_t[k-1] | (l_k & prev is a real source of l_k into t);
  I_t[last] -> next_t & -prev_t, and next_t & -prev_t -> I_t[last]. A label lacking a self-loop
  at t is forbidden after a move into t: l -> -I_t[before l].

  exists_lax=true: a label after the mover need not be applicable at s, only self-loop at t; the
  label -> applicable start state clauses get the alternative "after a move into a self-loop
  state of the label" (existsAfterMove).

  exists_effects_persist=true: a label with a real transition in a factor must end the step in
  one of its targets there (it may still self-loop before the mover, but only if the mover ends in
  one of its targets), as in FDR, where an action's effect holds at the end of an exists step;
  this keeps the forall encoding's label -> target clauses ("empty cols") for those labels, which
  propagate the end state from a firing label. Only restricts, so sound.

  The mover is the label at which I_next switches (plan extraction reads it there). Events are
  emitted only for transitions u -> t of labels without a self-loop at u: such a label cannot run
  before the mover, so it is the mover or runs after it; a label with a self-loop at u may run
  before the mover, so it does not force the chain, and the upper bound still allows it to be the
  mover. Labels before the switch are checked against s, labels after it against t (strict:
  every firing label applicable at s), so the semantics is exactly exists-step in the label order.
*/
struct LabelExistsFactor {
	std::unordered_map<int, int> index;                 // relevant label -> entry
	std::vector<int> labels;                            // per entry
	std::vector<std::vector<int>> selfLoops;            // per entry, sorted states
	std::vector<std::vector<int>> forbiddenAfter;       // per entry: states t, l -> -I_t[before l]
	std::vector<std::vector<int>> adders;               // per state t: labels, in label order
	std::vector<std::vector<int>> adderPos;             // their positions
	std::vector<std::vector<std::vector<int>>> adderSources; // per t, k: real sources into t
	bool hasSelfLoop(int label, int state) const;       // irrelevant labels: true
	bool hasAnySelfLoop(int label) const;
};

struct LabelExistsData {
	std::vector<int> pos;                               // label -> position in the order
	std::vector<LabelExistsFactor> factors;
};

// ---------------------------------------------------------------------------
// Segments of a factor (sigma_sat, micro_steps_sat)
// ---------------------------------------------------------------------------

/*
  A label of a factor's step sequence. changed = the states it moves (sources and targets of its
  real transitions), the states that get a new variable in its segment's end layer.
*/
struct SigmaLabelData {
    int label;
    std::vector<int> changed;                         // C_l, sorted
    std::vector<int> applicable;                      // states with a transition of l
    std::vector<std::pair<int, std::vector<int>>> targets;      // source -> targets
    std::vector<std::pair<int, std::vector<int>>> predecessors; // s in C_l -> sources into s
};

// per segment and label, for plan extraction
struct SigmaUpdate {
    int label;
    std::vector<std::pair<int, int>> vars;            // (state, variable) of the changed states
    int mover;                                        // group: the label's mover variable; 0: own micro-step; -1: matrix group
    int group;                                        // first position of the label's group (groups only)
};

/*
  Clause style of a single-label segment (see encode_single, encode_single_projection,
  encode_single_rows).
  SUPPORT (0, sigma_sat's default): precondition, both frame directions, effect
  l & p_x -> OR n_targets, justification l & n_y -> OR p_sources, at most one target per
  nondeterministic source.
  PROJECTION (1): label_sat's projection encoding applied to the segment's (source, target, label)
  matrix (Sa 2026): rows (label-source), cols (label-target), pillars (source-target, unguarded by
  the label, as a skipped label leaves the state), and the residual (label, source) -> target;
  each either primal (forbid the zeros) or dual (list the ones) when the ones are below the
  threshold.
  ROWS (2, micro_steps_sat): exactly-one per layer, frame axioms, and per source / target row of
  the label's matrix either the dual clause (if its arity is at most rows_dual_threshold) or the
  primal ones; constraint numbers (22)-(33) as in the R2E paper's micro-step section.
  layer_alo / layer_amo: exactly-one on every intermediate layer (pairs involving a new
  variable).
*/
struct SigmaClauses {
    int style = 0;                  // 0 SUPPORT, 1 PROJECTION (projection options: StepOptions'
                                    // use_empty_* / use_positive_one / one_encoding_threshold*), 2 ROWS
    bool layerAlo = false, layerAmo = false;
    bool shareLast = true;          // the last change of a state in a step is the t+1 variable
    bool frameReverse = false;      // PROJECTION: also -l & n_s -> p_s
    bool justification = false;     // PROJECTION: also l & n_y -> OR p_sources
    int rowsDualThreshold = 3;      // ROWS: arity threshold tau (< 0: always dual)
};

struct SigmaData {
    SigmaClauses clauses;
    bool groupMatrix = true;
    bool groupCompact = false;
    bool groupPureAmo = true;
    std::vector<std::vector<int>> factorBlock;          // factor_groups: per factor, block id per label entry
    std::vector<int> groupOf;                         // label -> micro-step group
    bool oneMove = false;
    bool strict = false;
    bool fullLayers = false;        // every label gets new variables for every state (micro_steps_sat
                                    // without variable sharing)
    std::vector<int> order;                           // label order of the step semantics
    std::vector<std::vector<SigmaLabelData>> factors; // per factor, labels in order (relevant ones,
                                                      // or all with fullLayers)
};

// ---------------------------------------------------------------------------
// Options and shared data
// ---------------------------------------------------------------------------

// which configuration built the encoding (option defaults, plugin-specific output)
enum class StepPreset { LABEL_SAT, SIGMA, MICRO_STEPS };

struct StepOptions {
	StepPreset preset = StepPreset::LABEL_SAT;
	// label_sat
	bool useLabelGroups = false;
	bool useSelfloopOptimisation = false;
	bool useEmptyRows = false;
	bool useEmptyCols = false;
	bool useEmptyPillars = false;
	bool useOnesInLastDimension = true;
	bool usePositiveOneForEmpty = false;
	size_t oneEncodingThreshold = 1;
	int oneEncodingThresholdPercent = 0;
	encoding_type encoding = SEQUENTIAL;
	bool chainsInterleaved = false;
	bool existsStep = false;
	bool existsLax = false;
	bool existsPersist = false;
	bool existsOnlyChains = false;
	bool existsPureEvents = false;
	// sigma_sat (segments)
	bool oneMove = false;
	bool strict = false;
	int groupSize = 1;
	bool groupMatrix = true;
	bool groupCompact = false;
	bool groupPureAmo = true;
	bool competingGroups = false;
	bool groupOrder = false;
	bool factorGroups = false;
	bool factorGroupsExact = false;
	bool groupAllowChains = false;
	bool groupKeepEnablers = true;
	int groupMax = 16;
	int commuteOrder = 0;
	bool reportOrder = false;
	int groupRelax = 0;
	SigmaClauses clauses;
	// layers
	bool nextStateFromLayers = false;   // t+1 state variables are the last layer (micro_steps_sat)
	bool fullLayers = false;            // micro_steps_sat without variable sharing
};

struct StepData {
	StepOptions opt;
	// label_sat
	std::vector<std::shared_ptr<FTSMatrix>> ftsMatrices;
	std::shared_ptr<const LabelExistsData> existsData;
	// segments
	SigmaData sigma;
	bool segments = false;              // sigma_sat / micro_steps_sat: per-factor segment sequences
};

// ---------------------------------------------------------------------------
// The encoding
// ---------------------------------------------------------------------------

class StepEncoding : public CommonEncoding {
	std::shared_ptr<const StepData> data;
	const StepOptions & opt;
	bool statisticsPrinted;

	// ----- label_sat: one segment per factor with every label (step_encoding_forall.cc)
	const std::vector<std::shared_ptr<FTSMatrix>> & fts_matrices;
	std::shared_ptr<const LabelExistsData> existsData;
	bool existsLax;
	bool existsWeaken;      // exists_step without exists_only_chains
	bool existsPersist;
	bool existsPureEvents;
	std::vector<std::vector<std::vector<int>>> existsChains; // [ts][t][k]
	std::map<int, std::vector<std::vector<std::vector<int>>>> existsChainsPerTime; // for extraction
	std::map<int,std::vector<std::vector<std::vector<int>>>> allTimesLabelGroupVars;
	std::map<int,int> someLabelExecutedPerTime;

	std::vector<int> existsAfterMove(int ts, int label, const std::vector<int> & sources, int skip = -1) const;
	int existsBefore(int ts, int t, int label) const;   // I_t[last adder before label] or 0
	void encode_exists_residual(int ts, int lg, int src,
		const std::vector<std::vector<int>> & previousStateVars,
		const std::vector<std::vector<std::vector<int>>> & labelGroupVars,
		const std::vector<std::vector<int>> & nextStateVars,
		const std::set<int> & coveredByLabel, const std::set<int> & coveredBySource);
	void encode_exists_chains(const std::vector<std::vector<int>> & previousStateVars,
		const std::vector<int> & labelVars, const std::vector<std::vector<int>> & nextStateVars);
	void encode_sequential(const std::vector<int> & labelVars);
	void encode_self_loop_parallel(const std::vector<int> & labelVars);
	void encode_chains_parallel_grouped(const std::vector<int> & labelVars, const std::vector<std::vector<int>> & nextStateVars);
	void encode_chains_parallel_interleaved(const std::vector<int> & labelVars, const std::vector<std::vector<int>> & nextStateVars);
	void encode_transition_semantics(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<std::vector<int>>> &labelGroupVars, const int someLabelExecutedVar, const std::vector<std::vector<int>> & nextStateVars);
	bool is_below_threshold(int ts, size_t ones_to_consider);
	void forall_generate_additional_variables(int fromTime);
	void forall_encode_frame_axioms(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<int>> & nextStateVars, int fromTime);
	void forall_encode_transition(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<int>> & nextStateVars, int fromTime);
	std::vector<std::vector<int>> forall_extract_intermediate_states(std::vector<int> & selectedLabels, std::vector<int> & currentLastState, std::vector<int> & nextState, int labelTimestep);

	// ----- segments: a sequence of segments per factor (step_encoding_segments.cc)
	const SigmaData & sigma;
	// per time and factor: (label, [(state, variable)] for its changed states), for extraction
	std::map<int, std::vector<std::vector<SigmaUpdate>>> allTimesUpdates;
	using OneMove = std::function<void(const std::vector<int> &, const std::vector<int> &, int)>;
	const std::vector<std::vector<int>> *curNextStateVars = nullptr;
	// ROWS: clause counts of the first step (micro_steps_sat's statistics output)
	struct RowsStatistics {
		long long layerVars = 0, amoAuxVars = 0;
		long long c22_alo = 0, c22_amo = 0, c23 = 0, c24 = 0, c25 = 0, c26 = 0, c27 = 0;
		long long d28 = 0, d29 = 0, d30 = 0, d31 = 0, d32 = 0, d33 = 0, links = 0;
		void print() const;
	} rowsStats;

	// the factor's segments from layer `cur` on; with nextStateVars (generated up front) the last
	// layer is made equal to them, otherwise the last layer is returned as the t+1 variables
	std::vector<int> encode_factor_segments(int ts, std::vector<int> cur, const std::vector<std::vector<int>> *nextStateVars, int fromTime);
	void encode_single(int ts, const SigmaLabelData &ld, const std::vector<int> &layer, const std::vector<int> &labelVars,
	                   std::vector<int> &cur, int fromTime, std::vector<SigmaUpdate> &updates,
	                   const OneMove &oneMove);
	void encode_single_projection(int ts, const SigmaLabelData &ld, const std::vector<int> &layer, const std::vector<int> &labelVars,
	                              std::vector<int> &cur, int fromTime, std::vector<SigmaUpdate> &updates,
	                              const OneMove &oneMove);
	void encode_single_rows(int ts, const SigmaLabelData &ld, const std::vector<int> &layer, const std::vector<int> &labelVars,
	                        std::vector<int> &cur, int fromTime, std::vector<SigmaUpdate> &updates);
	void rows_layer_exactly_one(const std::vector<int> &layer, const std::vector<int> &newStates,
	                            const std::vector<char> &isNew);
	void encode_group_compact(int ts, const std::vector<SigmaLabelData> &labels, size_t begin, size_t end, const std::vector<int> &layer,
	                          const std::vector<int> &labelVars, std::vector<int> &cur, int fromTime,
	                          std::vector<SigmaUpdate> &updates, const OneMove &oneMove);
	void encode_group_matrix(int ts, const std::vector<SigmaLabelData> &labels, size_t begin, size_t end, const std::vector<int> &layer,
	                         const std::vector<int> &labelVars, std::vector<int> &cur, int fromTime,
	                         std::vector<SigmaUpdate> &updates, const OneMove &oneMove);
	void encode_group(int ts, const std::vector<SigmaLabelData> &labels, size_t begin, size_t end, const std::vector<int> &layer,
	                  const std::vector<int> &labelVars, std::vector<int> &cur, int fromTime,
	                  std::vector<SigmaUpdate> &updates, const OneMove &oneMove);
	void amoLadder(const std::vector<int> &xs);
	std::vector<int> segment_new_states(const SigmaLabelData &ld, size_t numStates) const;
	void layer_one_hot(int ts, const std::vector<int> &next, const std::vector<int> &changed);
	bool below_threshold(size_t ones, size_t numStates) const;
	void segments_encode_transition(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<int>> & nextStateVars, int fromTime);
	std::vector<std::vector<int>> segments_extract_intermediate_states(std::vector<int> & selectedLabels, std::vector<int> & currentLastState, std::vector<int> & nextState, int labelTimestep);

protected:
	void encode(int fromTime, int toTime) override;
	void generateAdditionalVariables(int fromTime) override;
	void encode_transition(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<int>> & nextStateVars, int fromTime) override;
	void encode_frame_axioms(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<int>> & nextStateVars, int fromTime) override;
	std::vector<std::vector<int>> extractIntermediateStates(std::vector<int> & selectedLabels, std::vector<int> & currentLastState, std::vector<int> & nextState, int labelTimestep) override;

public:
	StepEncoding(std::shared_ptr<sat_capsule> capsule,
	             const std::shared_ptr<task_representation::FTSTask> & fts,
	             bool forceAtLeastOneAction,
	             bool statisticsPrinted,
	             std::shared_ptr<const StepData> data);
	~StepEncoding() override = default;
};

class StepEncodingFactory : public SATEncodingFactory {
	StepOptions opt;
	std::shared_ptr<label_order_finder::LabelOrderFinder> labelOrderFinder;
	std::unique_ptr<static_constraints::Options> staticOptions;   // sigma_sat only
	std::shared_ptr<const static_constraints::StaticConstraints> staticConstraints;
	std::shared_ptr<const StepData> data;
	bool statisticsPrinted = false;

	void initialize_forall(StepData &d);
	const std::vector<std::pair<int, int>> & labelOrderFinder_chain_pairs() const;
	std::vector<int> segment_label_order() const;
	void initialize_segments(StepData &d);
	void compute_group_order(SigmaData &d) const;
	void commute_labels(SigmaData &d) const;

public:
	StepEncodingFactory(const options::Options &opts, StepPreset preset);
	void initialize() override;
	std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
	~StepEncodingFactory() override = default;
};

}

#endif
