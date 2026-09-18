#ifndef SAT_BDD_ENCODING_H
#define SAT_BDD_ENCODING_H

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "label_encoding.h"

// include for BDDs
#include "cuddObj.hh"

namespace label_order_finder {
	class LabelOrderFinder;
}

namespace sat_search {

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

	// --- BDD variable layout ---
	// Variables 0 .. num_factor_vars-1 encode (factor state | factor next state),
	// variables num_factor_vars .. bdd_num_vars-1 encode the labels, in labelOrder.
	// num_factor_vars is 0 unless combineAllBDDsIntoOne is set.
	int bdd_num_vars = 0;
	int num_factor_vars = 0;

	// The label order is the BDD variable order: label labelOrder[i] is BDD
	// variable num_factor_vars + i. labelToBDDVar is the inverse mapping,
	// i.e. labelToBDDVar[labelOrder[i]] == num_factor_vars + i.
	std::vector<int> labelOrder;
	std::vector<int> labelToBDDVar;

	// exactly one of the two is populated, depending on combineAllBDDsIntoOne
	std::vector<BDD> transition_BDDs_per_factor;
	std::vector<std::vector<std::vector<BDD>>> transition_BDDs_per_factor_per_state_pair;

	// in-degree of every BDD node, used by the omitForcedVariables optimisation
	std::map<DdNode *, int> node_indegree;
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
		const std::vector<int> & factorVars,
		const std::vector<int> & labelVars,
		const std::vector<int> & nextFactorVars) const;

	/// Tseitin-translate the BDD rooted at node, guarded by currentConditions
	void bdd_to_cnf(DdNode * node,
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
	std::tuple<PlanState,std::vector<PlanState>,std::vector<int>,std::set<int>>
		extractSolution(int initTime, std::vector<std::pair<int,int>> time_step_order) override;
};


class BDDSATEncodingFactory : public SATEncodingFactory {
	const bool oneStepOnly;
	const bool combineAllBDDsIntoOne;
	const int bddEncodingSizeLimit;
	const bool implicationalTseitsin;
	const bool omitForcedVariables;
	const int forcedVariablesThreshold;
	const bool bddCutting;
	const bool bddCovering;
	// budgets for the BDD construction itself, so that an instance whose BDDs
	// cannot be built is reported as such instead of running until the driver
	// kills it. -1 disables the budget.
	const int bddInitTimeLimit;
	const long bddNodeLimit;

	std::shared_ptr<label_order_finder::LabelOrderFinder> label_order_finder;

	// The Cudd manager must outlive every BDD in data, so it is owned here and
	// never used again once initialize() has returned.
	std::unique_ptr<Cudd> _manager;
	std::shared_ptr<BDDEncodingData> data;

	// TODO: read from command line arguments
	const long cudd_init_nodes = 16000000;          // Number of initial nodes
	const long cudd_init_cache_size = 16000000;     // Initial cache size
	const long cudd_init_available_memory = 0;      // Maximum available memory (bytes)

	void bdd_to_dot(const BDD & bdd, const std::string & file_name) const;
	void bdd_in_degree(DdNode * node);
	/// throws BDDBudgetExceeded once a construction budget is used up
	void check_budget(int fac, const char * where) const;
	void cut_bdds_to_fixpoint();
	void report_covering_implications() const;

public:
	explicit BDDSATEncodingFactory(const options::Options &opts);
	~BDDSATEncodingFactory() override = default;

	void initialize() override;
	std::unique_ptr<SATEncoding> createEncodingInstance(std::shared_ptr<sat_capsule> capsule) override;
};

}

#endif
