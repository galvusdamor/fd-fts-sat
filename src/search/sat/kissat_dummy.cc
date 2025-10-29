#include <iostream>

extern "C" {
	typedef struct kissat kissat;
	void kissat_set_external_scheduler(kissat * , bool (*) (void *)){
		std::cout << "Downward tried to set an external scheduler for a SAT solver that does not support this feature." << std::endl;
		std::cout << "Likely you tried to run a search (e.g. rintanen) that is not supported in this configuration." << std::endl;
		std::cout << "Try obtaining a modified version of kissat and build downward with --custom-kissat." << std::endl;
		exit(-1);
	}
}
