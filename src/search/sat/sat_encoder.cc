#include "sat_encoder.h"
#include "ipasir.h"
#include <iostream>
#include <cassert>
#include <math.h> 


std::string path_string(const std::vector<int> & path){
	std::string s = "";
	for (const int & i : path){
		if (s.size()) s+= ",";
		s+= std::to_string(i);
	}

	return s;
}

std::string path_string_no_sep(const std::vector<int> & path){
	std::string s = "";
	for (const int & i : path)
		s+= std::to_string(i);

	return s;
}

std::string pad_string(std::string s, int chars){
	while (s.size() < size_t(chars))
		s += " ";
	return s;
}

std::string pad_int(int i, int chars){
	return pad_string(std::to_string(i),chars);
}

std::string pad_path(const std::vector<int> & path, int chars){
	return pad_string(path_string(path),chars);
}

sat_capsule::sat_capsule(void* _solver) :
	solver(_solver),
	number_of_variables(0),
	number_of_clauses(0){
}


int sat_capsule::new_variable(){
	return ++number_of_variables;
}

#ifndef NDEBUG
void sat_capsule::registerVariable(int v, std::string name){
	//std::cout << "Register " << v << " " << name << std::endl;
	assert(!variableNames.contains(v));
	variableNames[v] = name;	
}

void sat_capsule::printVariables() const {
	for (auto & p : variableNames){
		std::string s = std::to_string(p.first);
		int x = 4 - s.size();
		while (x-- && x > 0) std::cout << " ";
		std::cout << s << " -> " << p.second << std::endl;
	}
}
#endif


int sat_capsule::get_number_of_clauses() const {
	return number_of_clauses;
}

void sat_capsule::assertYes(int i){
	ipasir_add(solver,i);
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::assertNot(int i){
	ipasir_add(solver,-i);
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::implies(int i, int j){
	assert(i != 0);
	assert(j != 0);
	//DEBUG(std::cout << "Adding " << -i << " " << j << " " << 0 << std::endl);
	ipasir_add(solver,-i);
	ipasir_add(solver,j);
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::impliesAnd(int i, int j, int k){
	assert(i != 0);
	assert(j != 0);
	assert(k != 0);
	//DEBUG(std::cout << "Adding " << -i << " " << j << " " << 0 << std::endl);
	ipasir_add(solver,-i);
	ipasir_add(solver,-j);
	ipasir_add(solver,k);
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::impliesAnd(int i, const std::vector<int> & j){
	for (const int & x : j){
		assert(x);
		ipasir_add(solver,-i);
		ipasir_add(solver,x);
		ipasir_add(solver,0);
		number_of_clauses++;
	}
}

void sat_capsule::impliesNot(int i, int j){
	assert(i != 0);
	assert(j != 0);
	//DEBUG(std::cout << "Adding " << -i << " " << j << " " << 0 << std::endl);
	ipasir_add(solver,-i);
	ipasir_add(solver,-j);
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::impliesOr(int i, const std::vector<int> & j){
	assert(i);
	ipasir_add(solver,-i);
	for (const int & x : j){
		assert(x);
		ipasir_add(solver,x);
	}
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::andImpliesOr(int i, int j, const std::vector<int> & k){
	assert(i);
	ipasir_add(solver,-i);
	assert(j);
	ipasir_add(solver,-j);
	for (const int & x : k){
		assert(x);
		ipasir_add(solver,x);
	}
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::andImpliesOr(const std::vector<int> & i, const std::vector<int> & j){
	for (const int & x : i){
		assert(x);
		ipasir_add(solver,-x);
	}
	for (const int & x : j){
		assert(x);
		ipasir_add(solver,x);
	}
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::impliesPosAndNegImpliesOr(int i, int j, const std::vector<int> & k){
	ipasir_add(solver,-i);
	ipasir_add(solver,j);
	for (const int & x : k)
		ipasir_add(solver,x);
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::impliesAllNot(int i, const std::vector<int> & j){
	for (const int & x : j){
		ipasir_add(solver,-i);
		ipasir_add(solver,-x);
		ipasir_add(solver,0);
		number_of_clauses++;
	}
}

void sat_capsule::notImpliesAllNot(int i, const std::vector<int> & j){
	for (const int & x : j){
		ipasir_add(solver,i);
		ipasir_add(solver,-x);
		ipasir_add(solver,0);
		number_of_clauses++;
	}
}

void sat_capsule::andImplies(int i, int j, int k){
	ipasir_add(solver,-i);
	ipasir_add(solver,-j);
	ipasir_add(solver,k);
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::andImplies(const std::set<int> & i, int j){
	for (const int & x : i)
		ipasir_add(solver,-x);
	ipasir_add(solver,j);
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::andImplies(const std::vector<int> & i, int j){
	for (const int & x : i)
		ipasir_add(solver,-x);
	ipasir_add(solver,j);
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::notAll(const std::set<int> & i){
	for (const int & x : i)
		ipasir_add(solver,-x);
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::notAll(const std::vector<int> & i){
	for (const int & x : i)
		ipasir_add(solver,-x);
	ipasir_add(solver,0);
	number_of_clauses++;
}

void sat_capsule::allNotImpliesNot(const std::vector<int> & i, int j){
	for (const int & x : i)
		ipasir_add(solver,x);
	ipasir_add(solver,-j);
	ipasir_add(solver,0);
	number_of_clauses++;
	
}

void sat_capsule::atMostOneBinomial(const std::vector<int> & is){
	for (size_t i = 0; i < is.size(); i++){
		int ii = is[i];
		for (size_t j = i+1; j < is.size(); j++){
			impliesNot(ii,is[j]);
		}
	}
}


void sat_capsule::atMostOne(const std::vector<int> & is){
	if (is.size() <= 1) return; // nothing to do

	if (is.size() < 256){
		atMostOneBinomial(is);
		return;
	}

	int bits = (int) ceil(log(is.size()) / log(2));

	int baseVar = new_variable();
	DEBUG(registerVariable(baseVar,"at-most-one " + pad_int(0)));

	for (int b = 1; b < bits; b++){
#ifndef NDEBUG
		int r =
#endif
			new_variable(); // ignore return, they will be incremental
		assert(r == baseVar + b);
		DEBUG(registerVariable(baseVar + b,"at-most-one " + pad_int(b)));
	}


	for (size_t i = 0; i < is.size(); i++){
		const int & var = is[i];

		for (int b = 0; b < bits; b++){
			ipasir_add(solver,-var);
			if (i & (1 << b))
				ipasir_add(solver,-(baseVar + b));
			else	
				ipasir_add(solver,  baseVar + b );
			ipasir_add(solver,0);
			number_of_clauses++;
		}
	}
}


void sat_capsule::atMostK(int K, const std::vector<int> & is){
	std::vector<int> vars;
	for (int x = 0; x < static_cast<int>(is.size()); x++){
		int base = new_variable();
		vars.push_back(base);
		DEBUG(registerVariable(base,"at-most-K " + pad_int(0)+"-"+pad_int(x)));
		for (int i = 1; i <= K+1; i++){
			__attribute__((unused)) int v = new_variable();
			DEBUG(registerVariable(v,"at-most-K " + pad_int(i)+"-"+pad_int(x)));
			// id will not be needed
		}
	}

	int base = new_variable();
	vars.push_back(base);
	DEBUG(registerVariable(base,"at-most-K " + pad_int(0)));
	for (int i = 1; i <= K+1; i++){
		__attribute__((unused)) int v = new_variable();
		DEBUG(registerVariable(v,"at-most-K " + pad_int(i)));
		// id will not be needed
	}


	for (int i = 0; i < int(is.size()); i++){
		for (int j = 0; j <= K; j++){
			ipasir_add(solver,-is[i]);
			ipasir_add(solver,-(vars[i] + j));
			ipasir_add(solver,(vars[i+1] + j + 1));
			ipasir_add(solver,0);
			
			ipasir_add(solver,-(vars[i] + j));
			ipasir_add(solver,(vars[i+1] + j));
			ipasir_add(solver,0);
		}
		ipasir_add(solver,-(vars[i+1]+K+1));
		ipasir_add(solver,0);
	}
	
	ipasir_add(solver,vars[0]);
	ipasir_add(solver,0);
}

void sat_capsule::atLeastOne(const std::vector<int> & is){
	for (const int & i : is)
		ipasir_add(solver, i);
	ipasir_add(solver,0);
	number_of_clauses++;
}

