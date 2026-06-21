#pragma once
#include "infrastructure/ai/HttpTransport.hpp"

namespace ares::infrastructure::ai {

class CurlHttpTransport : public HttpTransport {
public:
    [[nodiscard]] auto post(
        std::string_view url,
        const std::vector<std::pair<std::string, std::string>>& headers,
        std::string_view body)
        -> std::expected<HttpResponse, core::Error> override;
};

} // namespace ares::infrastructure::ai
