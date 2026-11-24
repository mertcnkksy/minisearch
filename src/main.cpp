#include "minisearch/version.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char *argv[]) {
    if (argc == 1 || (argc == 2 && std::string_view{argv[1]} == "--help")) {
        std::cout << "Usage: minisearch [--help | --version]\n"
                     "  --help     Show this help message\n"
                     "  --version  Show the MiniSearch version\n";
        return 0;
    }

    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << "minisearch " << minisearch::version() << '\n';
        return 0;
    }

    if (argc > 2) {
        std::cerr << "Expected at most one argument. See minisearch --help.\n";
    } else {
        std::cerr << "Unknown argument: " << argv[1] << ". See minisearch --help.\n";
    }
    return 2;
}
