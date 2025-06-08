#include "Application.hpp"

#include <stdexcept>
#include <iostream>

int main() {
    try {
        auto application = Application{};
        application.run();
    } catch (std::exception const& e) {
        std::cerr << "Fatal error : " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
