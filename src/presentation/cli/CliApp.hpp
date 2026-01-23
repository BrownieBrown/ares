#pragma once

#include <string>
#include <vector>

namespace ares::presentation::cli {

class CliApp {
public:
    auto run(int argc, char* argv[]) -> int;

private:
    auto printHelp() -> void;
    auto printVersion() -> void;
};

} // namespace ares::presentation::cli
