#pragma once
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "core/common/Error.hpp"

namespace ares::infrastructure::ai {

struct HttpResponse {
    int status{0};
    std::string body;
};

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    [[nodiscard]] virtual auto post(
        std::string_view url,
        const std::vector<std::pair<std::string, std::string>>& headers,
        std::string_view body)
        -> std::expected<HttpResponse, core::Error> = 0;
};

} // namespace ares::infrastructure::ai
