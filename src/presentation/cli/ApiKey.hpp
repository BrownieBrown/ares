#pragma once
#include <cstdlib>
#include <expected>
#include <string>
#include "core/common/Error.hpp"

namespace ares::presentation::cli {

[[nodiscard]] inline auto resolveApiKey() -> std::expected<std::string, core::Error> {
    const char* key = std::getenv("ANTHROPIC_API_KEY");
    if (key == nullptr || key[0] == '\0') {
        return std::unexpected(core::Error{core::ApiKeyMissingError{}});
    }
    return std::string{key};
}

} // namespace ares::presentation::cli
