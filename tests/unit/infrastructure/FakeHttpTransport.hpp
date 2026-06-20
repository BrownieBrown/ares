#pragma once
#include "infrastructure/ai/HttpTransport.hpp"

namespace ares::test {

class FakeHttpTransport : public infrastructure::ai::HttpTransport {
public:
    // Programmed result returned by post():
    std::expected<infrastructure::ai::HttpResponse, core::Error> result{
        infrastructure::ai::HttpResponse{200, "{}"}};

    // Captured request:
    std::string lastUrl;
    std::vector<std::pair<std::string, std::string>> lastHeaders;
    std::string lastBody;

    auto post(std::string_view url,
              const std::vector<std::pair<std::string, std::string>>& headers,
              std::string_view body)
        -> std::expected<infrastructure::ai::HttpResponse, core::Error> override {
        lastUrl = std::string{url};
        lastHeaders = headers;
        lastBody = std::string{body};
        return result;
    }
};

} // namespace ares::test
