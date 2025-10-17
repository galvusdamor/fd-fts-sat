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


#define INTPAD 4
#define PATHPAD 15
#define STRINGPAD 0

std::string path_string(std::vector<int> & path);
std::string path_string_no_sep(std::vector<int> & path);
std::string pad_string(std::string s, int chars = STRINGPAD);
std::string pad_int(int i, int chars = INTPAD);
std::string pad_path(std::vector<int> & path, int chars = PATHPAD);


struct sat_capsule{
	void* solver;
	int number_of_variables;
	int number_of_clauses;
	int new_variable();

#ifndef NDEBUG
	std::map<int,std::string> variableNames;
	void registerVariable(int v, std::string name);
	void printVariables();
#endif

	sat_capsule(void* _solver);

	void reset_number_of_clauses();
	int get_number_of_clauses();
	
	void assertYes(int i);
	void assertNot(int i);
	
	void implies(int i, int j);
	void impliesAnd(int i, int j, int k);
	void impliesAnd(int i, std::vector<int> j);
	void impliesNot(int i, int j);
	void impliesOr(int i, std::vector<int> & j);
	void andImpliesOr(int i, int j, std::vector<int> & k);
	void andImpliesOr(std::vector<int> & i, std::vector<int> & j);
	void impliesPosAndNegImpliesOr(int i, int j, std::vector<int> & k);
	void impliesAllNot(int i, std::vector<int> & j);
	void notImpliesAllNot(int i, std::vector<int> & j);
	void andImplies(int i, int j, int k);
	void andImplies(std::set<int> i, int j);
	void andImplies(std::vector<int> i, int j);
	void atMostOneBinomial(const std::vector<int> & is);
	void atMostOne(const std::vector<int> & is);
	void atLeastOne(const std::vector<int> & is);
	void atMostK(int K, std::vector<int> & is);
	void notAll(std::set<int> & i);
	void notAll(std::vector<int> & i);
	void allNotImpliesNot(std::vector<int> & i, int j);
};


#endif
