#include "cli.h"
#include "utils.h"
#include <iostream>
#include <exception>

int main(int argc, char* argv[]) {
    try {
        casper::CLI cli;

        if (!cli.parseArgs(argc, argv)) {
            return 0; // Help or version displayed
        }

        return cli.run();

    } catch (const std::exception& e) {
        casper::utils::terminal::printError(std::string("Fatal error: ") + e.what());
        return 1;
    } catch (...) {
        casper::utils::terminal::printError("Unknown fatal error occurred");
        return 1;
    }
}
