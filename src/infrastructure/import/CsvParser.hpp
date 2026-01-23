#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>
#include "core/common/Error.hpp"

namespace ares::infrastructure::import {

using CsvRow = std::vector<std::string>;

struct CsvDocument {
    std::vector<std::string> headers;
    std::vector<CsvRow> rows;
};

struct CsvParserOptions {
    char delimiter{','};
    char quote{'"'};
    bool hasHeader{true};
};

class CsvParser {
public:
    explicit CsvParser(CsvParserOptions options = {});

    [[nodiscard]] auto parse(std::string_view content)
        -> std::expected<CsvDocument, core::Error>;

    [[nodiscard]] auto parse(const std::filesystem::path& filePath)
        -> std::expected<CsvDocument, core::Error>;

private:
    CsvParserOptions options_;

    [[nodiscard]] auto parseLine(std::string_view line, int lineNumber)
        -> std::expected<CsvRow, core::ParseError>;
};

} // namespace ares::infrastructure::import
