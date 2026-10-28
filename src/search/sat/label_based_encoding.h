#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "sat_encoder.h"
#include "common_encoding.h"
#include "../task_representation/transition_system.h"

#include <unordered_map>


namespace plugins {
class Feature;
}

namespace label_order_finder {
class LabelOrderFinder;
}


namespace sat_search{
class FTSMatrix;
class LengthStrategy;
enum encoding_type {
	SEQUENTIAL,
	SELF_LOOP_PARALLEL,
	CHAINS_PARALLEL
};

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

class LabelBasedEncoding : public CommonEncoding {
	bool statisticsPrinted;
	bool useEmptyRows;
	bool useEmptyCols;
	bool useEmptyPillars;
	bool useOnesInLastDimension;
	bool usePositiveOneForEmpty;
	size_t oneEncodingThreshold;
	int oneEncodingThresholdPercent;
	encoding_type encoding;
	bool chainsInterleaved;

	std::vector<std::shared_ptr<FTSMatrix>> fts_matrices;

	// exists_step: shared data, and the chain variables of the step being encoded
	std::shared_ptr<const LabelExistsData> existsData;
	bool existsLax;
	std::vector<std::vector<std::vector<int>>> existsChains; // [ts][t][k]
	std::map<int, std::vector<std::vector<std::vector<int>>>> existsChainsPerTime; // for extraction
	std::vector<int> existsAfterMove(int ts, int label, const std::vector<int> & sources, int skip = -1) const;
	int existsBefore(int ts, int t, int label) const;   // I_t[last adder before label] or 0
	void encode_exists_residual(int ts, int lg, int src,
		const std::vector<std::vector<int>> & previousStateVars,
		const std::vector<std::vector<std::vector<int>>> & labelGroupVars,
		const std::vector<std::vector<int>> & nextStateVars,
		const std::set<int> & coveredByLabel, const std::set<int> & coveredBySource);
	void encode_exists_chains(const std::vector<std::vector<int>> & previousStateVars,
		const std::vector<int> & labelVars, const std::vector<std::vector<int>> & nextStateVars);

protected:

	std::map<int,std::vector<std::vector<std::vector<int>>>> allTimesLabelGroupVars;
	std::map<int,int> someLabelExecutedPerTime;

	/// encoding functions for parallelism
	void encode_sequential(const std::vector<int> & labelVars);
	void encode_self_loop_parallel(const std::vector<int> & labelVars);
	void encode_chains_parallel_grouped(const std::vector<int> & labelVars, const std::vector<std::vector<int>> & nextStateVars);
	void encode_chains_parallel_interleaved(const std::vector<int> & labelVars, const std::vector<std::vector<int>> & nextStateVars);

	/// encoding function for the main transition relation
	void encode_transition(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<int>> & nextStateVars, int fromTime/*, int toTime*/);
	void encode_transition_semantics(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<std::vector<int>>> &labelGroupVars, const int someLabelExecutedVar, const std::vector<std::vector<int>> & nextStateVars);
	void encode_frame_axioms(const std::vector<std::vector<int>> & previousStateVars, const std::vector<std::vector<int>> & nextStateVars, int fromTime);
	void generateAdditionalVariables(int fromTime/*, int toTime*/);

	bool is_below_threshold(int ts, size_t ones_to_consider);

	std::vector<std::vector<int>> extractIntermediateStates(std::vector<int> & selectedLabels, std::vector<int> & currentLastState, std::vector<int> & nextState, int labelTimestep);


public:
    explicit LabelBasedEncoding(
		std::shared_ptr<sat_capsule> capsule,
		const std::shared_ptr<task_representation::FTSTask> & _fts,
		bool _statisticsPrinted,
		bool _useLabelGroups,
		bool _useSelfloopOptimisation,
		bool _useEmptyRows,
		bool _useEmptyCols,
		bool _useEmptyPillars,
		bool _useOnesInLastDimension,
		bool _usePositiveOneForEmpty,
		bool _forceAtLeastOneAction,
		const size_t _oneEncodingThreshold,
		const int _oneEncodingThresholdPercent,
		const encoding_type & _encoding,
		bool _chainsInterleaved,
		const std::vector<std::shared_ptr<FTSMatrix>> & fts_matrices,
		std::shared_ptr<const LabelExistsData> _existsData = nullptr,
		bool _existsLax = false
			);
	~LabelBasedEncoding() override = default;

	//void encode(int fromTime, int toTime) override;
	std::tuple<PlanState,std::vector<PlanState>,std::vector<int>,std::set<int>> extractSolution(int initTime, std::vector<std::pair<int,int>> time_step_order) override;
};


class LabelBasedEncodingFactory : public SATEncodingFactory {
	const bool useLabelGroups;
	const bool useSelfloopOptimisation;
	const bool useEmptyRows;
	const bool useEmptyCols;
	const bool useEmptyPillars;
	const bool useOnesInLastDimension;
	const bool usePositiveOneForEmpty;
	const size_t oneEncodingThreshold;
	const int oneEncodingThresholdPercent;
	const encoding_type encoding;
	const bool chainsInterleaved;
	const bool existsStep;
	const bool existsLax;
	std::shared_ptr<label_order_finder::LabelOrderFinder> labelOrderFinder;
	std::shared_ptr<const LabelExistsData> existsData;
	bool statisticsPrinted;
	
	std::vector<std::shared_ptr<FTSMatrix> > fts_matrices;

public:
	explicit LabelBasedEncodingFactory(const options::Options &opts);

	void initialize() override;
	std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
    ~LabelBasedEncodingFactory() override = default;
};





extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
