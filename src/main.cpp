#include <cstdlib>
#include <span>

#include "cli/cli.hpp"
#include "util/output.hpp"

int main(int argc, char* argv[]) {
    if (auto result{cli::run(std::span(argv, argc))}; !result) {
        util::output::error(result.error());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
