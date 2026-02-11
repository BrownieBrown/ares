#pragma once

#include <string>
#include <variant>
#include <fmt/format.h>

namespace ares::core {

struct ParseError {
    std::string message;
    int line{0};
    int column{0};
    std::string sourceLine;

    [[nodiscard]] auto what() const -> std::string {
        if (line > 0 && !sourceLine.empty()) {
            return fmt::format("Line {}: {}\n  > {}", line, message, sourceLine);
        }
        if (line > 0) {
            return fmt::format("Parse error at line {}: {}", line, message);
        }
        return fmt::format("Parse error: {}", message);
    }
};

struct ValidationError {
    std::string field;
    std::string message;

    [[nodiscard]] auto what() const -> std::string {
        return fmt::format("Validation error for '{}': {}", field, message);
    }
};

struct IoError {
    std::string path;
    std::string message;

    [[nodiscard]] auto what() const -> std::string {
        return fmt::format("I/O error for '{}': {}", path, message);
    }
};

struct DatabaseError {
    std::string operation;
    std::string message;

    [[nodiscard]] auto what() const -> std::string {
        return fmt::format("Database error in '{}': {}", operation, message);
    }
};

struct CurrencyMismatchError {
    std::string message{"Cannot perform operation on different currencies"};

    [[nodiscard]] auto what() const -> std::string {
        return message;
    }
};

struct NotFoundError {
    std::string entity;
    std::string id;

    [[nodiscard]] auto what() const -> std::string {
        return fmt::format("{} not found: {}", entity, id);
    }
};

using Error = std::variant<
    ParseError,
    ValidationError,
    IoError,
    DatabaseError,
    CurrencyMismatchError,
    NotFoundError
>;

[[nodiscard]] inline auto errorMessage(const Error& error) -> std::string {
    return std::visit([](const auto& e) { return e.what(); }, error);
}

} // namespace ares::core
