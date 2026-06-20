#include "infrastructure/ai/CurlHttpTransport.hpp"
#include <curl/curl.h>
#include <fmt/format.h>

namespace ares::infrastructure::ai {

namespace {
auto writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}
} // namespace

auto CurlHttpTransport::post(
    std::string_view url,
    const std::vector<std::pair<std::string, std::string>>& headers,
    std::string_view body)
    -> std::expected<HttpResponse, core::Error> {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return std::unexpected(core::Error{core::HttpError{"curl_easy_init failed"}});
    }

    std::string response;
    std::string urlStr{url};
    std::string bodyStr{body};

    curl_slist* hdrs = nullptr;
    for (const auto& [k, v] : headers) {
        hdrs = curl_slist_append(hdrs, fmt::format("{}: {}", k, v).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, urlStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        return std::unexpected(core::Error{core::HttpError{curl_easy_strerror(rc)}});
    }
    return HttpResponse{static_cast<int>(status), response};
}

} // namespace ares::infrastructure::ai
