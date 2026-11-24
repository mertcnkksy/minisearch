#include "cli/terminal_output.hpp"
#include "minisearch/cli/repl.hpp"
#include "minisearch/version.hpp"

#include <csignal>
#include <iostream>
#include <string_view>

int main(int argc, char *argv[]) {
#ifdef SIGPIPE
    // Let writes to a closed pipe surface as stream errors.
    std::signal(SIGPIPE, SIG_IGN);
#endif
    if (argc <= 1) {
        std::cin.tie(nullptr);
        return minisearch::cli::run_repl(std::cin, std::cout, std::cerr);
    }
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::cout << "Usage: minisearch [--help | --version]\n"
                     "  Run without arguments to start an interactive session.\n"
                     "  --help     Show this help message\n"
                     "  --version  Show the MiniSearch version\n";
        return std::cout.flush() ? 0 : 1;
    }

    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << "minisearch " << minisearch::version() << '\n';
        return std::cout.flush() ? 0 : 1;
    }

    if (argc > 2) {
        std::cerr << "Expected at most one argument. See minisearch --help.\n";
    } else {
        std::cerr << "Unknown argument: ";
        minisearch::cli::write_escaped(std::cerr, argv[1]);
        std::cerr << ". See minisearch --help.\n";
    }
    return std::cerr.flush() ? 2 : 1;
}
