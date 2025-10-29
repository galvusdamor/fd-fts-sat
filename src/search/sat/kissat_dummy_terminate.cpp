#include <iostream>

extern "C" {
	void ipasir_terminate (void *){
		std::cout << "Downward tried to call terminate on SAT solver that does not support this feature." << std::endl;
	}
}
