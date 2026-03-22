#pragma once

#include <expected>
#include <filesystem>
#include <string_view>
#include "core/common/Error.hpp"
#include "infrastructure/config/ConfigParser.hpp"  // For UserConfig and related structs

namespace ares::infrastructure::config {

class YamlConfigParser {
public:
    YamlConfigParser() = default;

    [[nodiscard]] auto parse(const std::filesystem::path& path)
        -> std::expected<UserConfig, core::Error>;

    [[nodiscard]] auto parse(std::string_view content)
        -> std::expected<UserConfig, core::Error>;
};

} // namespace ares::infrastructure::config
