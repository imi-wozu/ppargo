#include <cstdlib>

#include "cli/cli.hpp"
#include "util/output.hpp"

int main(int argc, char* argv[]) {
    if (auto result{cli::run(argc, argv)}; !result) {
        util::output::error(result.error());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
