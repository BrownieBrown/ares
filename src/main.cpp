#include "presentation/cli/CliApp.hpp"

int main(int argc, char* argv[]) {
    ares::presentation::cli::CliApp app;
    return app.run(argc, argv);
}
