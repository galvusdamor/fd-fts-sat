#ifndef SAT_BDD_ENCODING_H
#define SAT_BDD_ENCODING_H

#include <map>
#include <unordered_map>
#include <memory>
#include <set>
#include <array>
#include <vector>

#include "label_encoding.h"
#include "../task_utils/state_mutexes.h"

// include for BDDs
#include "cuddObj.hh"

namespace label_order_finder {
	class LabelOrderFinder;
}

namespace task_representation {
	class TransitionSystem;
}

namespace sat_search {

class FTSMatrix;

/**
 * Task-level data for the BDD encoding.
 *
 * Everything in here is computed once by BDDSATEncodingFactory::initialize()
 * and is strictly read-only afterwards. The per-SAT-call encodings hold a
 * shared_ptr to it, so several encodings (rintanen interleaves a number of
 * them) can share the BDDs without copying and without touching the Cudd
 * manager again. No BDD may be created after initialize() has returned.
 */
struct BDDEncodingData {
	// --- options that the encoding itself needs at clause-generation time ---
	bool combineAllBDDsIntoOne;
	bool implicationalTseitsin;
	bool omitForcedVariables;
	int forcedVariablesThreshold;
	// path_encoding: path-based (domain consistent) translation instead of Tseitin
	bool pathEncoding = false;
	// path_sharing: one path encoding per factor over all its (s, s') BDDs
	bool pathSharing = false;
	// BDD variable index -> level in the final variable order (for path_encoding)
	std::vector<int> varLevel;

	// --- BDD variable layout ---
	// Variables 0 .. num_factor_vars-1 encode (factor state | factor next state),
	// variables num_factor_vars .. bdd_num_vars-1 encode the labels.
	// num_factor_vars is 0 unless combineAllBDDsIntoOne is set.
	int bdd_num_vars = 0;
	int num_factor_vars = 0;

	/*
	  One label ordering together with the BDDs built under it.

	  The label order is the semantics: the order in which the labels of one
	  time step are applied. Which BDD variable a label gets is a separate
	  choice and only affects how compactly the BDDs represent that semantics.
	  By default the two coincide (label labelOrder[i] is BDD variable
	  num_factor_vars + i); bdd_variable_order assigns the variables
	  differently, and reordering by Cudd moves them afterwards. The variable
	  *index* of a label never changes, only its level, so labelToBDDVar and
	  bddVarToLabel stay valid under reordering.

	  Two orderings mean two sets of BDDs, built over the same Cudd variables.
	*/
	struct Ordering {
		std::vector<int> labelOrder;
		std::vector<int> labelPosition;   // label -> its index in labelOrder
		std::vector<int> labelToBDDVar;   // label -> BDD variable index
		std::vector<int> bddVarToLabel;   // BDD variable index - num_factor_vars -> label
		// exactly one of the two is populated, depending on combineAllBDDsIntoOne
		std::vector<BDD> transition_BDDs_per_factor;
		std::vector<std::vector<std::vector<BDD>>> transition_BDDs_per_factor_per_state_pair;
	};

	/*
	  Normally one entry. With alternate_label_orders there are two, and a time
	  step picks one by its parity: the first encoded step (fromTime == 1) uses
	  orderings[0], the second orderings[1], and so on. Since a step's BDDs
	  constrain only that step's label variables, neighbouring steps may use
	  different orderings without any interaction -- the state variables they
	  share carry no order information.
	*/
	std::vector<Ordering> orderings;

	const Ordering & ordering_at(int fromTime) const {
		return orderings[(fromTime - 1) % orderings.size()];
	}

	// in-degree of every BDD node, used by the omitForcedVariables optimisation
	std::map<DdNode *, int> node_indegree;

	// cube_encoding: for BDDs with at most cube_encoding_max paths to 0, the cubes of the
	// negation (signed BDD variable indices + 1: +v true, -v false). Such a BDD f under
	// conditions C is encoded as one clause per cube, -C v -cube, without Tseitin
	// variables. Keyed by the node pointer including its complement bit.
	std::unordered_map<DdNode *, std::vector<std::vector<int>>> zero_cubes;

	// label_implications: clauses over the label variables implied by the factors'
	// BDDs (signed label ids: +(l+1) / -(l+1)), per ordering. Unconditional ones hold
	// in every step; conditional ones only if the factor is in the given source state.
	struct CondClause { int factor; int source; std::vector<int> lits; };
	std::vector<std::vector<std::vector<int>>> implied_clauses;
	std::vector<std::vector<CondClause>> implied_cond_clauses;
	// label_implications_bicliques: binary clauses grouped into bicliques A x B
	// (every a in A conflicts with every b in B): a -> z, b -> -z for a fresh z
	struct Biclique { std::vector<int> A; std::vector<int> B; };
	std::vector<std::vector<Biclique>> implied_bicliques;

	// state clauses (per ordering): "conditions -> label literal", the conditions being
	// one or two factor states at the start (next=false) or end (next=true) of the step.
	// source/target units (one condition), pair units (state at start and end of one
	// factor), cross units (states of two factors). lit: +-(label + 1).
	struct CondLit { int fac; int state; bool next; };
	struct StateUnit { CondLit c[2]; int nc; int lit; };
	std::vector<std::vector<StateUnit>> state_units;
	// leave / enter clauses (per ordering): for a factor state, the labels moving out of
	// it (leave, sorted by label position) or into it (enter, sorted by descending
	// position), and the labels that cannot fire there; blocked (l, j): l needs one of
	// the first j movers
	struct MoveInfo { int fac; int state; bool enter; std::vector<int> movers;
		std::vector<std::pair<int,int>> blocked; };
	std::vector<std::vector<MoveInfo>> move_infos;
	// goal_last_label: per label, whether it can move some factor into its goal states
	bool goalLastLabel = false;
	// leave/enter clauses: 0 both parts, 1 frame clauses only, 2 order clauses only
	int moveClauseParts = 0;
	bool moveFrameSplit = false;
	// state_mutexes: unreachable (factor, state) and mutex pairs (f1, s1, f2, s2)
	std::vector<std::pair<int,int>> mutex_units;
	std::vector<std::array<int,4>> mutex_pairs;
	std::vector<char> goal_achieving;
};


/**
 * One BDD encoding instance, i.e. one SAT call / one plan length.
 *
 * State variables, the exactly-one constraints over them and init/goal come
 * from StateEncoding; the per-time label variables come from LabelEncoding.
 * What this class adds is the Tseitin translation of the factor BDDs and the
 * plan extraction, which has to reconstruct the intermediate states inside a
 * time step by DFS because the encoding does not represent them.
 */
class BDDSATEncoding : public LabelEncoding {
	std::shared_ptr<const BDDEncodingData> data;

	// Tseitin variables for the BDD nodes. Per encoding instance (the SAT
	// variable numbering is per capsule) and reset for every factor.
	std::map<DdNode *, int> tseitsinVars;

	/// map a BDD variable index onto the SAT variable of this time step
	int givevar(int bddvar,
		const BDDEncodingData::Ordering & ord,
		const std::vector<int> & factorVars,
		const std::vector<int> & labelVars,
		const std::vector<int> & nextFactorVars) const;

	/// path-based translation (NNFPath of Abio et al. 2016, for reduced BDDs with
	/// complement edges) of several BDDs sharing nodes, of which at most one is
	/// active (its conditions hold): domain consistent on the BDDs' variables.
	/// Empty conditions mean always active.
	void bdd_to_cnf_paths(const std::vector<std::pair<DdNode *, std::vector<int>>> & roots,
		const BDDEncodingData::Ordering & ord,
		const std::vector<int> & factorVars,
		const std::vector<int> & labelVars,
		const std::vector<int> & nextFactorVars);

	/// Tseitin-translate the BDD rooted at node, guarded by currentConditions
	void bdd_to_cnf(DdNode * node,
		const BDDEncodingData::Ordering & ord,
		const std::vector<int> & currentConditions,
		const std::vector<int> & factorVars,
		const std::vector<int> & labelVars,
		const std::vector<int> & nextFactorVars);

	/// reconstruct the states a factor passes through within one time step
	bool bdd_state_reconstruction_dfs(int fac,
		std::vector<std::vector<int>> & reconstructedStates,
		int depth,
		const std::vector<int> & plan,
		std::set<std::pair<int,int>> & visited) const;

public:
	explicit BDDSATEncoding(
		std::shared_ptr<sat_capsule> capsule,
		const std::shared_ptr<task_representation::FTSTask> & _fts,
		bool _forceAtLeastOneAction,
		const std::shared_ptr<const BDDEncodingData> & _data);
	~BDDSATEncoding() override = default;

	void encode(int fromTime, int toTime) override;
	void encodeGoal(int toTime, bool retractable) override;
	std::tuple<PlanState,std::vector<PlanState>,std::vector<int>,std::set<int>>
		extractSolution(int initTime, std::vector<std::pair<int,int>> time_step_order) override;
};


class BDDSATEncodingFactory : public SATEncodingFactory {
	const bool oneStepOnly;
	const bool combineAllBDDsIntoOne;
	const int bddEncodingSizeLimit;
	const bool implicationalTseitsin;
	const bool pathEncoding;
	const bool pathSharing;
	const bool impliedBicliques;
	const bool combineITE;
	const bool omitForcedVariables;
	const int forcedVariablesThreshold;
	const bool bddCutting;
	// cutbdds: 0 AND (conjoin the constraint, as before), 1 AND_LIMITED (conjoin with a
	// node limit of cut_limit_factor * size of the target BDD, keep only a smaller result),
	// 2 RESTRICT (Cudd_bddRestrict with the constraint as care set; never larger)
	const int cutMode;
	const double cutLimitFactor;
	const double cutTimeLimit;
	const bool reportLabelConjunction;
	const int cubeEncodingMax;
	// label_implications: 0 NONE, 1 UNCONDITIONAL, 2 PER_SOURCE, 3 BOTH
	const int labelImplications;
	const int labelImplicationsMax;
	const double labelImplicationsTimeLimit;
	const double cutMinOverlap;
	const bool sourceUnits, targetUnits, pairUnits, crossUnits, leaveClauses, enterClauses;
	const int crossPairLimit;
	const double stateClausesTimeLimit;
	const double stateClausesMinPhase;
	const int moveClausesMax;
	const int moveClauseParts;
	const bool moveFrameSplit;
	const bool mutexClauses, mutexCutting;
	const state_mutexes::Options mutexOptions;
	const int mutexClauseMax;
	const double mutexCutTimeLimit, mutexCutLimitFactor;
	// compatibility of the factor states (task_utils/state_mutexes); null if not computed
	std::unique_ptr<state_mutexes::StateMutexes> mutexes;
	void mutex_clauses();
	void mutex_cut(BDDEncodingData::Ordering & ord);
	const bool goalLastLabel;
	/// source/target/pair/cross units and leave/enter clauses (see BDDEncodingData)
	void compute_state_clauses();
	void compute_goal_achieving();
	/// label_implications: extract units and binary implications from the factors' BDDs
	void compute_implied_clauses();
	void cover_bicliques();
	/// cube_encoding: enumerate the 0-paths of every encoded BDD within cube_encoding_max
	void compute_zero_cubes();
	const int labelConjunctionLimit;
	const bool bddCovering;
	const bool alternateLabelOrders;
	const bool reportLabelImplications;
	const bool reportFactorStatistics;
	// budgets for the BDD construction itself, so that an instance whose BDDs
	// cannot be built is reported as such instead of running until the driver
	// kills it. -1 disables the budget.
	const int bddInitTimeLimit;
	const long bddNodeLimit;

	std::shared_ptr<label_order_finder::LabelOrderFinder> label_order_finder;
	const std::string dumpLabelOrderFile;
	// nullptr: the BDD variable order is the label order
	std::shared_ptr<label_order_finder::LabelOrderFinder> bdd_variable_order_finder;
	// Cudd reordering: 0 none, otherwise a Cudd_ReorderingType, applied once
	// after construction and, with bddReorderDynamic, also during it
	const int bddReorderMethod;
	const bool bddReorderDynamic;
	const std::string dumpBDDVariableOrderFile;

	// One per transition system, same objects the label-based encoding uses;
	// holds the per-state self-loop sets the one-step construction needs.
	// Built lazily: the full-reachability construction never looks at them, so
	// building one per factor up front is pure overhead for that mode.
	mutable std::vector<std::shared_ptr<FTSMatrix>> fts_matrices;
	const FTSMatrix & matrix_for(int fac) const;

	// The Cudd manager must outlive every BDD in data, so it is owned here and
	// never used again once initialize() has returned.
	std::unique_ptr<Cudd> _manager;
	std::shared_ptr<BDDEncodingData> data;

	// Cudd manager sizing. The defaults reproduce what was hard-coded before.
	// They matter more than they look: the cache is allocated up front and is
	// not scaled by anything, so it costs the same ~400MB on a task with 18
	// labels as on one with 59535. That is a quarter of the budget when many
	// runs share a node.
	const long cudd_init_nodes;             // initial nodes, divided by the label count
	const long cudd_init_cache_size;        // initial cache slots
	const long cudd_init_available_memory;  // hard cap in bytes, 0 = let Cudd decide

	void bdd_to_dot(const BDD & bdd, const std::string & file_name) const;
	void bdd_in_degree(DdNode * node);
	/// throws BDDBudgetExceeded once a construction budget is used up
	void check_budget(int fac, const char * where) const;

	/*
	  The two transition relations a factor can be given, as two separate
	  constructions because they answer different questions.

	  build_full_reachability_bdds: which label *sequences*, in label order,
	  take the factor from s to ss within one time step. A DP backwards over
	  the label order, so it costs one pass per label that is relevant for this
	  factor.

	  build_one_step_bdds: which label sets do so using at most one real
	  transition, framed by self loops before and after it. Every label that
	  cannot self-loop in the relevant state has to be forced false, so it
	  needs to know, per state, which labels self-loop there -- which is what
	  FTSMatrix already stores.
	*/
	std::vector<std::vector<BDD>> build_full_reachability_bdds(
		int fac,
		const task_representation::TransitionSystem & factor,
		const BDDEncodingData::Ordering & ord,
		int & num_relevant_labels);

	std::vector<std::vector<BDD>> build_one_step_bdds(
		int fac,
		const task_representation::TransitionSystem & factor,
		const FTSMatrix & matrix,
		const BDDEncodingData::Ordering & ord);

	/// build every factor's BDDs under one ordering
	void build_bdds_for_ordering(BDDEncodingData::Ordering & ord,
		long & total_nodes_sum, long & total_nodes_after_limit,
		int & overall_max_pair_nodes);
	/// summed nodeCount() of the BDDs that get encoded, as in nodes_sum_all_factors
	long encoded_nodes_sum(const BDDEncodingData::Ordering & ord) const;
	void cut_bdds_to_fixpoint(BDDEncodingData::Ordering & ord);
	/// the label sets a factor permits in one step, irrespective of source and target state
	BDD any_transition(const BDDEncodingData::Ordering & ord, int fac) const;
	/// diagnostics: AND of all factors' any_transition BDDs, under a node limit
	void report_label_conjunction(const BDDEncodingData::Ordering & ord) const;
	void report_covering_implications(const BDDEncodingData::Ordering & ord) const;
	/// mine the per-factor label BDDs for unit and binary label dependencies
	void report_label_implications(const BDDEncodingData::Ordering & ord) const;

public:
	explicit BDDSATEncodingFactory(const options::Options &opts);
	~BDDSATEncodingFactory() override = default;

	void initialize() override;
	std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
};

}

#endif
