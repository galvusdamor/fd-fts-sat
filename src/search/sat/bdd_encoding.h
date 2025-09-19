#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "sat_encoding.h"
#include "../search_engines/sat_encoder.h"
#include "../task_representation/label_equivalence_relation.h"


// include for BDDs
#include "cuddObj.hh"
#include "../task_utils/label_order_finder.h"


namespace sat_search{

class BDDSATEncoding : public SAT_encoding {
private:
	
	bool considerOnlyOneStepTransitions = true;
	int bddEncodingSizeLimit = -1; // -1 means no limit
	bool implicationalTseitsin;
	bool omitForcedVariables;
	int forcedVariablesThreshold;
	bool combineAllBDDsIntoOne;
	bool bddCutting;
	bool bddCovering;

	bool forceAtLeastOneAction;
	
	std::shared_ptr<task_representation::FTSTask> fts;
    std::vector<int> labelOrder;

	std::unique_ptr<Cudd> _manager; //_manager associated with this symbolic search
	void bdd_to_dot(const BDD &bdd, const std::string &file_name) const;


	// TODO: read from command line arguments
	const long cudd_init_nodes = 16000000; //Number of initial nodes
    const long cudd_init_cache_size = 16000000; //Initial cache size
    const long cudd_init_available_memory = 0; //Maximum available memory (bytes)
	int bdd_num_vars;
	int num_factor_vars;
	
	// for the combined encoding
	std::vector<BDD> transition_BDDs_per_factor;
	std::vector<std::vector<std::vector<BDD>>> transition_BDDs_per_factor_per_state_pair;
	std::vector<std::vector<std::vector<BDD>>> one_step_transition_BDDs_per_factor_per_state_pair;

	int givevar(int bddvar, std::vector<int> & factorVars, std::vector<int> & labelVars, std::vector<int> & nextFactorVars);

	void bdd_in_degree(DdNode * node);

	void bdd_to_cnf(DdNode * node, std::vector<int> & currentConditions, std::vector<int> & factorVars, std::vector<int> & labelVars, std::vector<int> & nextFactorVars, void* solver, sat_capsule & capsule);

	bool bdd_state_reconstruction_dfs(int fac, std::vector<std::vector<int>> & reconstructedStates, int depth, std::vector<int> & plan, std::set<std::pair<int,int>> & visited);



	std::vector<std::vector<int>> generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */);
	std::vector<int> generateLabelVars(__attribute__((unused)) void* solver, sat_capsule & capsule/* , int timestep */);
public:
    explicit BDDSATEncoding(const Options &opts,std::shared_ptr<task_representation::FTSTask> main_task);
    virtual ~BDDSATEncoding() = default;


	void initialize() override;
	void encode(int currentLength, int stepTimeLimit) override;
};

//extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
