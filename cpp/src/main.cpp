#include "cli.h"
#include "utils.h"
#include <iostream>
#include <exception>

int main(int argc, char* argv[]) {
    try {
        oleg::CLI cli;

        if (!cli.parseArgs(argc, argv)) {
            return 0; // Help or version displayed
        }

        return cli.run();

    } catch (const std::exception& e) {
        oleg::utils::terminal::printError(std::string("Fatal error: ") + e.what());
        return 1;
    } catch (...) {
        oleg::utils::terminal::printError("Unknown fatal error occurred");
        return 1;
    }
}
