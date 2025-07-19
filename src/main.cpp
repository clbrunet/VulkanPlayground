#include "Application.hpp"

#include <iostream>
#ifdef _WIN32
#include <Windows.h>
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try {
        auto application = vp::Application();
        application.run();
    } catch (std::exception const& e) {
        std::cerr << "Fatal error : " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
