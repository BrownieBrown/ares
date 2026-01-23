#include "infrastructure/import/CsvParser.hpp"
#include <fstream>
#include <sstream>

namespace ares::infrastructure::import {

CsvParser::CsvParser(CsvParserOptions options)
    : options_{options}
{}

auto CsvParser::parse(std::string_view content) -> std::expected<CsvDocument, core::Error> {
    CsvDocument doc;
    std::istringstream stream{std::string{content}};
    std::string line;
    int lineNumber = 0;

    while (std::getline(stream, line)) {
        ++lineNumber;
        if (line.empty()) continue;

        auto row = parseLine(line, lineNumber);
        if (!row) {
            return std::unexpected(row.error());
        }

        if (options_.hasHeader && lineNumber == 1) {
            doc.headers = std::move(*row);
        } else {
            doc.rows.push_back(std::move(*row));
        }
    }

    return doc;
}

auto CsvParser::parse(const std::filesystem::path& filePath)
    -> std::expected<CsvDocument, core::Error>
{
    std::ifstream file{filePath};
    if (!file) {
        return std::unexpected(core::IoError{
            .path = filePath.string(),
            .message = "Failed to open file"
        });
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    return parse(std::string_view{content});
}

auto CsvParser::parseLine(std::string_view line, [[maybe_unused]] int lineNumber)
    -> std::expected<CsvRow, core::ParseError>
{
    CsvRow row;
    std::string field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (c == options_.quote) {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == options_.quote) {
                field += c;
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == options_.delimiter && !inQuotes) {
            row.push_back(std::move(field));
            field.clear();
        } else {
            field += c;
        }
    }

    row.push_back(std::move(field));
    return row;
}

} // namespace ares::infrastructure::import
