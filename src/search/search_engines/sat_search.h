#ifndef SEARCH_ALGORITHMS_SAT_SEARCH
#define SEARCH_ALGORITHMS_SAT_SEARCH

#include "../search_engine.h"
#include "../option_parser.h"
#include "../plugin.h"
#include "sat_encoder.h"
#include "../task_representation/transition_system.h"


// include for BDDs
#include "cuddObj.hh"

namespace plugins {
class Feature;
}


namespace sat_search{

class SATSearch : public SearchEngine {
private: 
	int planLength;
	bool do_R2_encoding = true;
	bool no_selfloop_SATvars = false;
	bool do_BDD_encoding;
	bool considerOnlyOneStepTransitions = true;
	int bddEncodingSizeLimit = -1; // -1 means no limit
	bool implicationalTseitsin;
	bool combineAllBDDsIntoOne;
	bool bddCutting;

	bool forceAtLeastOneAction;
	
	std::shared_ptr<task_representation::FTSTask> fts;

    std::vector<int> labelOrder;
    std::vector<std::vector<int>> relevantLabels;



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

	int bdd_to_cnf(DdNode * node, std::vector<int> & factorVars, std::vector<int> & labelVars, std::vector<int> & nextFactorVars, void* solver, sat_capsule & capsule);

	bool bdd_state_reconstruction_dfs(int fac, std::vector<std::vector<int>> & reconstructedStates, int depth, std::vector<int> & plan, std::set<std::pair<int,int>> & visited);

	// for iteration	
	int currentLength;

protected:
    virtual void initialize() override;
	virtual bool isIrrelevantLabel(int ts, int label);
	virtual bool containsSelfLoops(int ts, int label);
	virtual bool isAlwaysSelfLoop(int ts, int label);
	virtual void checkSolution(std::vector<std::vector<std::vector<int>>> &allTimesStateVars, std::vector<std::vector<int>> &allTimesLabelVars, 
		std::vector<std::map<int, std::map<int, std::vector<std::pair<task_representation::Transition, int>>>>> &allTimesTransitionVars, int length, void* solver);
    virtual SearchStatus step() override;
    virtual std::vector<std::vector<int>> generateStateVars(void* solver, sat_capsule & capsule/* , int timestep */);
    virtual std::map<int, std::map<int, std::vector<std::pair<task_representation::Transition, int>>>> generateTransitionVars(void* solver, sat_capsule &capsule);
    virtual std::map<int, std::map<int, std::vector<int>>> generateAuxVars(sat_capsule &capsule);
    virtual std::vector<int> generateLabelVars(void* solver, sat_capsule & capsule/* , int timestep */);
    virtual std::map<int, std::map<int, std::vector<int>>> getApplicableLabels();
    virtual std::map<int, std::map<int, std::map<int, std::vector<int>>>> getSuccessorStates(std::map<int, std::map<int, std::vector<int>>> applicableLabels);

public:
    explicit SATSearch(const Options &opts);
    virtual ~SATSearch() = default;

    virtual void print_statistics() const override;
};

extern void add_options_to_feature(plugins::Feature &feature);
}

#endif
