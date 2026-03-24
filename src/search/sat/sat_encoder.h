#ifndef sat_encoder_h_INCLUDED
#define sat_encoder_h_INCLUDED

#include <map>
#include <vector>
#include <set>
#include <string>

#ifndef NDEBUG
# define DEBUG(x) do { x; } while (0)
#else
# define DEBUG(x)
#endif

struct sat_capsule{
	void* solver;
	int number_of_variables;
	int number_of_clauses;
	int new_variable();

#ifndef NDEBUG
	std::map<int,std::string> variableNames;
	void registerVariable(int v, std::string name);
	void printVariables() const;
#endif

	sat_capsule(void* _solver);

	int get_number_of_clauses() const;
	
	void assertYes(int i);
	void assertNot(int i);
	
	void implies(int i, int j);
	void impliesAnd(int i, int j, int k);
	void impliesAnd(int i, const std::vector<int> & j);
	void impliesAndNot(int i, const std::vector<int> & j);
	void impliesNot(int i, int j);
	void orImplies(const std::vector<int> & i, int j); // any of the i's implies j
	void orImpliesNot(const std::vector<int> & i, int j); // any of the i's implies -j
	void orImpliesOr(const std::vector<int> & i, const std::vector<int> & j);
	void impliesOr(int i, const std::vector<int> & j);
	void andImpliesOr(int i, int j, const std::vector<int> & k);
	void andImpliesOr(const std::vector<int> & i, const std::vector<int> & j);
	void impliesPosAndNegImpliesOr(int i, int j, const std::vector<int> & k);
	void impliesAllNot(int i, const std::vector<int> & j);
	void notImpliesAllNot(int i, const std::vector<int> & j);
	void andImplies(int i, int j, int k);
	void orAndImplies(const std::vector<int> & i, int j, int k);
	void andImplies(const std::set<int> & i, int j);
	void andImplies(const std::vector<int> & i, int j);
	void atMostOneBinomial(const std::vector<int> & is);
	void atMostOne(const std::vector<int> & is);
	void atLeastOne(const std::vector<int> & is);
	void atMostK(int K, const std::vector<int> & is);
	void notAll(const std::set<int> & i);
	void notAll(const std::vector<int> & i);
	void allNotImpliesNot(const std::vector<int> & i, int j);
};


#endif
