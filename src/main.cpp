#include "Application.hpp"

#include <stdexcept>
#include <iostream>

int main() {
	try {
		auto application = Application{};
		application.run();
	} catch (const std::exception& e) {
		std::cerr << "Fatal error : " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
