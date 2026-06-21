# AI Spending Advisor (Session 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an `ares analyze` command that sends the user's financial data to the Claude API and prints an AI-generated spending analysis with actionable suggestions.

**Architecture:** A reusable Claude-API core in `infrastructure/ai/` (an injectable `HttpTransport` interface, a libcurl implementation, and a `ClaudeClient` that speaks the Anthropic Messages API), driven by an `AdvisorService` in the application layer that gathers repository data, serializes it to a prompt payload, and returns the model's report. Tests inject a fake transport so nothing hits the network.

**Tech Stack:** C++23, CMake + FetchContent, Catch2 v3, SQLite, fmt, CLI11, yaml-cpp, **libcurl** (new), **nlohmann/json** (new).

## Global Constraints

- C++23; build via `make build`, test via `make test`.
- Error handling: `core::Error` is a `std::variant` of structs, each with `[[nodiscard]] auto what() const -> std::string`. New error types are **structs added to the variant**, not enum values. Fallible functions return `std::expected<T, core::Error>`.
- `[[nodiscard]]` on functions with meaningful return values; `constexpr` where possible.
- Monetary amounts are `int64` cents (`core::Money::cents()`).
- Use `fmt::format` for string formatting.
- Every public API has Catch2 tests in `tests/unit/`; test files named `*Tests.cpp`. **No test may perform real network I/O.**
- Headers `.hpp`, implementation `.cpp`.
- Default model id: `claude-sonnet-4-6`. API base URL: `https://api.anthropic.com/v1/messages`. Header `anthropic-version: 2023-06-01`. (Confirm current version/schema against Anthropic docs during Task 4.)

---

### Task 1: Add libcurl + nlohmann/json dependencies

**Files:**
- Modify: `CMakeLists.txt` (FetchContent block ~lines 30–71; `ares_infrastructure` link ~line 145)
- Test: `tests/unit/infrastructure/JsonDepTests.cpp` (create)

**Interfaces:**
- Produces: nlohmann/json (`#include <nlohmann/json.hpp>`) and libcurl (`#include <curl/curl.h>`) available to `ares_infrastructure` and the test target.

- [ ] **Step 1: Add the dependencies to CMakeLists.txt**

After the existing `yaml-cpp` `FetchContent_MakeAvailable(yaml-cpp)` line, add:
```cmake
find_package(CURL REQUIRED)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)
```
In `target_link_libraries(ares_infrastructure ...)` (around line 145) add `CURL::libcurl` and `nlohmann_json::nlohmann_json` to the existing link list (keep current entries).

- [ ] **Step 2: Write a smoke test that uses both libs**

Create `tests/unit/infrastructure/JsonDepTests.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

TEST_CASE("nlohmann/json round-trips", "[deps]") {
    nlohmann::json j;
    j["model"] = "claude-sonnet-4-6";
    REQUIRE(j.dump() == R"({"model":"claude-sonnet-4-6"})");
}

TEST_CASE("libcurl is linkable", "[deps]") {
    REQUIRE(curl_version() != nullptr);
}
```
Register the file in the test target's source list in `CMakeLists.txt` (where other `tests/unit/...Tests.cpp` are listed, ~line 224).

- [ ] **Step 3: Build and run**

Run: `make build && make test 2>&1 | grep -E "deps|tests passed"`
Expected: both `[deps]` tests PASS; overall suite still passes.

- [ ] **Step 4: Commit**
```bash
git add CMakeLists.txt tests/unit/infrastructure/JsonDepTests.cpp
git commit -m "build: add libcurl and nlohmann/json for the AI advisor"
```

---

### Task 2: Add Claude-API error types

**Files:**
- Modify: `src/core/common/Error.hpp` (the error structs + the `Error` variant)
- Test: `tests/unit/core/AiErrorTests.cpp` (create)

**Interfaces:**
- Produces: `core::ApiKeyMissingError`, `core::HttpError`, `core::ApiError`, `core::JsonParseError` structs, each added to `core::Error`. `core::errorMessage(Error)` returns their `what()`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/core/AiErrorTests.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "core/common/Error.hpp"

using namespace ares::core;

TEST_CASE("AI error messages", "[error][ai]") {
    REQUIRE(errorMessage(Error{ApiKeyMissingError{}})
            == "ANTHROPIC_API_KEY is not set");
    REQUIRE(errorMessage(Error{HttpError{"connection refused"}})
            == "HTTP error: connection refused");
    REQUIRE(errorMessage(Error{ApiError{401, "invalid x-api-key"}})
            == "Claude API error (401): invalid x-api-key");
    REQUIRE(errorMessage(Error{JsonParseError{"unexpected token"}})
            == "JSON error: unexpected token");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make build 2>&1 | tail -5`
Expected: FAIL to compile (`ApiKeyMissingError` not declared).

- [ ] **Step 3: Add the structs and extend the variant**

In `src/core/common/Error.hpp`, before the `using Error = std::variant<...>` declaration, add:
```cpp
struct ApiKeyMissingError {
    [[nodiscard]] auto what() const -> std::string {
        return "ANTHROPIC_API_KEY is not set";
    }
};

struct HttpError {
    std::string message;
    [[nodiscard]] auto what() const -> std::string {
        return fmt::format("HTTP error: {}", message);
    }
};

struct ApiError {
    int status{0};
    std::string message;
    [[nodiscard]] auto what() const -> std::string {
        return fmt::format("Claude API error ({}): {}", status, message);
    }
};

struct JsonParseError {
    std::string message;
    [[nodiscard]] auto what() const -> std::string {
        return fmt::format("JSON error: {}", message);
    }
};
```
Add the four type names to the `std::variant<...>` list.

- [ ] **Step 4: Run tests**

Run: `make test 2>&1 | grep -E "error.*ai|tests passed"`
Expected: `[error][ai]` PASS; suite passes.

- [ ] **Step 5: Commit**
```bash
git add src/core/common/Error.hpp tests/unit/core/AiErrorTests.cpp
git commit -m "feat(core): add Claude-API error variants"
```

---

### Task 3: HttpTransport interface + FakeHttpTransport

**Files:**
- Create: `src/infrastructure/ai/HttpTransport.hpp`
- Create: `tests/unit/infrastructure/FakeHttpTransport.hpp` (test helper)
- Test: `tests/unit/infrastructure/FakeHttpTransportTests.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace ares::infrastructure::ai {
    struct HttpResponse { int status; std::string body; };
    class HttpTransport {
    public:
      virtual ~HttpTransport() = default;
      [[nodiscard]] virtual auto post(
        std::string_view url,
        const std::vector<std::pair<std::string,std::string>>& headers,
        std::string_view body) -> std::expected<HttpResponse, core::Error> = 0;
    };
  }
  ```
  `FakeHttpTransport` records the last call and returns a programmed response or error.

- [ ] **Step 1: Create the interface header**

`src/infrastructure/ai/HttpTransport.hpp`:
```cpp
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
```

- [ ] **Step 2: Create the fake**

`tests/unit/infrastructure/FakeHttpTransport.hpp`:
```cpp
#pragma once
#include "infrastructure/ai/HttpTransport.hpp"

namespace ares::test {

class FakeHttpTransport : public infrastructure::ai::HttpTransport {
public:
    // Programmed result:
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
```

- [ ] **Step 3: Write the test**

`tests/unit/infrastructure/FakeHttpTransportTests.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "FakeHttpTransport.hpp"

using namespace ares;

TEST_CASE("FakeHttpTransport captures request and returns programmed response", "[ai][http]") {
    test::FakeHttpTransport t;
    t.result = infrastructure::ai::HttpResponse{200, R"({"ok":true})"};

    auto r = t.post("https://x/y", {{"k", "v"}}, "payload");

    REQUIRE(r.has_value());
    REQUIRE(r->status == 200);
    REQUIRE(t.lastUrl == "https://x/y");
    REQUIRE(t.lastBody == "payload");
    REQUIRE(t.lastHeaders.at(0).first == "k");
}
```
Register both new test files in the test target (`CMakeLists.txt`), and add `tests/unit/infrastructure` to the test target's include dirs so `FakeHttpTransport.hpp` resolves.

- [ ] **Step 4: Run tests**

Run: `make test 2>&1 | grep -E "ai..http|tests passed"`
Expected: `[ai][http]` PASS.

- [ ] **Step 5: Commit**
```bash
git add src/infrastructure/ai/HttpTransport.hpp tests/unit/infrastructure/FakeHttpTransport.hpp tests/unit/infrastructure/FakeHttpTransportTests.cpp CMakeLists.txt
git commit -m "feat(ai): add HttpTransport interface and test fake"
```

---

### Task 4: ClaudeClient

**Files:**
- Create: `src/infrastructure/ai/ClaudeClient.hpp`, `src/infrastructure/ai/ClaudeClient.cpp`
- Test: `tests/unit/infrastructure/ClaudeClientTests.cpp`

**Interfaces:**
- Consumes: `HttpTransport`, `HttpResponse`, `core::Error` (Task 3, Task 2).
- Produces:
  ```cpp
  class ClaudeClient {
  public:
    ClaudeClient(HttpTransport& transport, std::string apiKey, std::string model);
    [[nodiscard]] auto complete(std::string_view systemPrompt,
                                std::string_view userMessage)
        -> std::expected<std::string, core::Error>;
  };
  ```

- [ ] **Step 1: Write the failing tests**

`tests/unit/infrastructure/ClaudeClientTests.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "FakeHttpTransport.hpp"
#include "infrastructure/ai/ClaudeClient.hpp"

using namespace ares;
using ares::infrastructure::ai::ClaudeClient;

TEST_CASE("ClaudeClient builds a correct Anthropic request", "[ai][claude]") {
    test::FakeHttpTransport t;
    t.result = infrastructure::ai::HttpResponse{
        200, R"({"content":[{"type":"text","text":"hello"}]})"};
    ClaudeClient client{t, "sk-test", "claude-sonnet-4-6"};

    auto r = client.complete("be brief", "analyze this");

    REQUIRE(r.has_value());
    REQUIRE(*r == "hello");
    REQUIRE(t.lastUrl == "https://api.anthropic.com/v1/messages");
    auto body = nlohmann::json::parse(t.lastBody);
    REQUIRE(body["model"] == "claude-sonnet-4-6");
    REQUIRE(body["system"] == "be brief");
    REQUIRE(body["messages"][0]["role"] == "user");
    REQUIRE(body["messages"][0]["content"] == "analyze this");
    // headers contain the api key and version
    bool hasKey = false, hasVer = false;
    for (auto& [k, v] : t.lastHeaders) {
        if (k == "x-api-key" && v == "sk-test") hasKey = true;
        if (k == "anthropic-version") hasVer = true;
    }
    REQUIRE(hasKey);
    REQUIRE(hasVer);
}

TEST_CASE("ClaudeClient maps non-2xx to ApiError", "[ai][claude]") {
    test::FakeHttpTransport t;
    t.result = infrastructure::ai::HttpResponse{
        401, R"({"error":{"message":"invalid x-api-key"}})"};
    ClaudeClient client{t, "bad", "claude-sonnet-4-6"};

    auto r = client.complete("s", "u");
    REQUIRE_FALSE(r.has_value());
    REQUIRE(std::holds_alternative<core::ApiError>(r.error()));
    REQUIRE(std::get<core::ApiError>(r.error()).status == 401);
}

TEST_CASE("ClaudeClient maps malformed JSON to JsonParseError", "[ai][claude]") {
    test::FakeHttpTransport t;
    t.result = infrastructure::ai::HttpResponse{200, "not json"};
    ClaudeClient client{t, "sk", "claude-sonnet-4-6"};

    auto r = client.complete("s", "u");
    REQUIRE_FALSE(r.has_value());
    REQUIRE(std::holds_alternative<core::JsonParseError>(r.error()));
}
```

- [ ] **Step 2: Run to verify failure**

Run: `make build 2>&1 | tail -5`
Expected: FAIL (`ClaudeClient.hpp` not found).

- [ ] **Step 3: Write the header**

`src/infrastructure/ai/ClaudeClient.hpp`:
```cpp
#pragma once
#include <expected>
#include <string>
#include <string_view>
#include "core/common/Error.hpp"
#include "infrastructure/ai/HttpTransport.hpp"

namespace ares::infrastructure::ai {

class ClaudeClient {
public:
    ClaudeClient(HttpTransport& transport, std::string apiKey, std::string model);

    [[nodiscard]] auto complete(std::string_view systemPrompt,
                                std::string_view userMessage)
        -> std::expected<std::string, core::Error>;

private:
    HttpTransport& transport_;
    std::string apiKey_;
    std::string model_;
};

} // namespace ares::infrastructure::ai
```

- [ ] **Step 4: Write the implementation**

`src/infrastructure/ai/ClaudeClient.cpp`:
```cpp
#include "infrastructure/ai/ClaudeClient.hpp"
#include <nlohmann/json.hpp>

namespace ares::infrastructure::ai {

ClaudeClient::ClaudeClient(HttpTransport& transport, std::string apiKey, std::string model)
    : transport_(transport), apiKey_(std::move(apiKey)), model_(std::move(model)) {}

auto ClaudeClient::complete(std::string_view systemPrompt, std::string_view userMessage)
    -> std::expected<std::string, core::Error> {
    nlohmann::json body{
        {"model", model_},
        {"max_tokens", 4096},
        {"system", std::string{systemPrompt}},
        {"messages", nlohmann::json::array({
            {{"role", "user"}, {"content", std::string{userMessage}}}
        })},
    };

    std::vector<std::pair<std::string, std::string>> headers{
        {"x-api-key", apiKey_},
        {"anthropic-version", "2023-06-01"},
        {"content-type", "application/json"},
    };

    auto resp = transport_.post("https://api.anthropic.com/v1/messages", headers, body.dump());
    if (!resp) return std::unexpected(resp.error());

    if (resp->status < 200 || resp->status >= 300) {
        std::string msg = resp->body;
        if (auto parsed = nlohmann::json::parse(resp->body, nullptr, false); !parsed.is_discarded()) {
            if (parsed.contains("error") && parsed["error"].contains("message")) {
                msg = parsed["error"]["message"].get<std::string>();
            }
        }
        return std::unexpected(core::Error{core::ApiError{resp->status, msg}});
    }

    auto parsed = nlohmann::json::parse(resp->body, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("content") ||
        !parsed["content"].is_array() || parsed["content"].empty() ||
        !parsed["content"][0].contains("text")) {
        return std::unexpected(core::Error{core::JsonParseError{"unexpected response shape"}});
    }
    return parsed["content"][0]["text"].get<std::string>();
}

} // namespace ares::infrastructure::ai
```
Add both files to `ares_infrastructure` sources in `CMakeLists.txt`.

- [ ] **Step 5: Run tests**

Run: `make test 2>&1 | grep -E "ai..claude|tests passed"`
Expected: all three `[ai][claude]` PASS.

- [ ] **Step 6: Commit**
```bash
git add src/infrastructure/ai/ClaudeClient.* tests/unit/infrastructure/ClaudeClientTests.cpp CMakeLists.txt
git commit -m "feat(ai): add ClaudeClient (Anthropic Messages API)"
```

---

### Task 5: CurlHttpTransport (real implementation)

**Files:**
- Create: `src/infrastructure/ai/CurlHttpTransport.hpp`, `src/infrastructure/ai/CurlHttpTransport.cpp`
- Test: `tests/unit/infrastructure/CurlHttpTransportTests.cpp` (construction only — no network)

**Interfaces:**
- Consumes: `HttpTransport` (Task 3).
- Produces: `class CurlHttpTransport : public HttpTransport` with a working `post`.

- [ ] **Step 1: Write the construction test**
```cpp
#include <catch2/catch_test_macros.hpp>
#include "infrastructure/ai/CurlHttpTransport.hpp"

TEST_CASE("CurlHttpTransport constructs", "[ai][curl]") {
    ares::infrastructure::ai::CurlHttpTransport t;
    SUCCEED("constructed without throwing");
}
```

- [ ] **Step 2: Run to verify failure**

Run: `make build 2>&1 | tail -5`
Expected: FAIL (header missing).

- [ ] **Step 3: Write header + implementation**

`src/infrastructure/ai/CurlHttpTransport.hpp`:
```cpp
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
```

`src/infrastructure/ai/CurlHttpTransport.cpp`:
```cpp
#include "infrastructure/ai/CurlHttpTransport.hpp"
#include <curl/curl.h>

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
    if (!curl) return std::unexpected(core::Error{core::HttpError{"curl_easy_init failed"}});

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
```
Include `<fmt/format.h>` in the .cpp. Add both files to `ares_infrastructure` sources.

- [ ] **Step 4: Run tests**

Run: `make test 2>&1 | grep -E "ai..curl|tests passed"`
Expected: `[ai][curl]` PASS; suite passes.

- [ ] **Step 5: Commit**
```bash
git add src/infrastructure/ai/CurlHttpTransport.* tests/unit/infrastructure/CurlHttpTransportTests.cpp CMakeLists.txt
git commit -m "feat(ai): add libcurl HttpTransport implementation"
```

---

### Task 6: Config — read `ai.model`

**Files:**
- Modify: `src/infrastructure/config/ConfigParser.hpp` (`UserConfig` struct ~line 70)
- Modify: `src/infrastructure/config/ConfigParser.cpp` (YAML parse path)
- Test: `tests/unit/infrastructure/ConfigAiModelTests.cpp`

**Interfaces:**
- Produces: `UserConfig::aiModel` (`std::string`, default `"claude-sonnet-4-6"`), populated from YAML `ai.model`.

- [ ] **Step 1: Write the failing test**
```cpp
#include <catch2/catch_test_macros.hpp>
#include "infrastructure/config/ConfigParser.hpp"

using ares::infrastructure::config::ConfigParser;

TEST_CASE("ai.model defaults when absent", "[config][ai]") {
    auto cfg = ConfigParser::parseString("categorization: []\n");
    REQUIRE(cfg.has_value());
    REQUIRE(cfg->aiModel == "claude-sonnet-4-6");
}

TEST_CASE("ai.model is read from yaml", "[config][ai]") {
    auto cfg = ConfigParser::parseString("ai:\n  model: claude-opus-4-8\n");
    REQUIRE(cfg.has_value());
    REQUIRE(cfg->aiModel == "claude-opus-4-8");
}
```
> Note: if `ConfigParser` has no `parseString`, use the existing parse entry point (check `ConfigParser.hpp`); write the YAML to a temp file and parse that instead. Keep the assertions identical.

- [ ] **Step 2: Run to verify failure**

Run: `make build 2>&1 | tail -5`
Expected: FAIL (`aiModel` missing).

- [ ] **Step 3: Add the field**

In `UserConfig` (ConfigParser.hpp), add after the existing members:
```cpp
std::string aiModel{"claude-sonnet-4-6"};
```

- [ ] **Step 4: Parse it**

In `ConfigParser.cpp`, where the top-level YAML node is read into `UserConfig`, add (yaml-cpp):
```cpp
if (root["ai"] && root["ai"]["model"]) {
    config.aiModel = root["ai"]["model"].as<std::string>();
}
```
(Match the surrounding style/variable names in that function.)

- [ ] **Step 5: Run tests**

Run: `make test 2>&1 | grep -E "config..ai|tests passed"`
Expected: both `[config][ai]` PASS.

- [ ] **Step 6: Commit**
```bash
git add src/infrastructure/config/ConfigParser.* tests/unit/infrastructure/ConfigAiModelTests.cpp
git commit -m "feat(config): read ai.model (default claude-sonnet-4-6)"
```

---

### Task 7: AdvisorService — payload + orchestration

**Files:**
- Create: `src/application/services/AdvisorService.hpp`, `src/application/services/AdvisorService.cpp`
- Test: `tests/unit/application/AdvisorServiceTests.cpp`

**Interfaces:**
- Consumes: `core::TransactionRepository` (`findAll()`), `core::AccountRepository`, `core::CreditRepository`, `infrastructure::config::UserConfig` (budgets), `infrastructure::ai::ClaudeClient` (Task 4).
- Produces:
  ```cpp
  class AdvisorService {
  public:
    // Pure, deterministic — testable without network:
    [[nodiscard]] static auto buildPayload(
        const std::vector<core::Transaction>& txns,
        const std::vector<core::Account>& accounts,
        const std::vector<core::Credit>& credits,
        const infrastructure::config::UserConfig& config,
        int months) -> std::string;          // returns JSON string

    [[nodiscard]] static auto systemPrompt() -> std::string;

    // Orchestration:
    [[nodiscard]] static auto generateReport(
        infrastructure::ai::ClaudeClient& client,
        const std::vector<core::Transaction>& txns,
        const std::vector<core::Account>& accounts,
        const std::vector<core::Credit>& credits,
        const infrastructure::config::UserConfig& config,
        int months) -> std::expected<std::string, core::Error>;
  };
  ```

- [ ] **Step 1: Write the failing tests**
```cpp
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "application/services/AdvisorService.hpp"
#include "infrastructure/ai/ClaudeClient.hpp"
#include "FakeHttpTransport.hpp"

using namespace ares;
using application::services::AdvisorService;

// Helper builds one expense transaction; adjust ctor to match Transaction.hpp.
static core::Transaction makeTxn(std::string date, int64_t cents,
                                 core::TransactionCategory cat,
                                 std::string merchant);

TEST_CASE("buildPayload emits currency, accounts, and transactions", "[ai][advisor]") {
    std::vector<core::Transaction> txns{
        makeTxn("2026-05-01", -2647, core::TransactionCategory::Groceries, "REWE")};
    auto json = nlohmann::json::parse(
        AdvisorService::buildPayload(txns, {}, {}, {}, 12));
    REQUIRE(json["currency"] == "EUR");
    REQUIRE(json["transactions"].size() == 1);
    REQUIRE(json["transactions"][0]["cents"] == -2647);
    REQUIRE(json["transactions"][0]["merchant"] == "REWE");
}

TEST_CASE("generateReport returns the model text", "[ai][advisor]") {
    test::FakeHttpTransport t;
    t.result = infrastructure::ai::HttpResponse{
        200, R"({"content":[{"type":"text","text":"You overspend on subscriptions."}]})"};
    infrastructure::ai::ClaudeClient client{t, "sk", "claude-sonnet-4-6"};

    auto r = AdvisorService::generateReport(client, {}, {}, {}, {}, 12);
    REQUIRE(r.has_value());
    REQUIRE(*r == "You overspend on subscriptions.");
}

TEST_CASE("generateReport propagates client errors", "[ai][advisor]") {
    test::FakeHttpTransport t;
    t.result = std::unexpected(core::Error{core::HttpError{"offline"}});
    infrastructure::ai::ClaudeClient client{t, "sk", "claude-sonnet-4-6"};

    auto r = AdvisorService::generateReport(client, {}, {}, {}, {}, 12);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(std::holds_alternative<core::HttpError>(r.error()));
}
```
> Implement `makeTxn` using the real `Transaction` constructor/builders (see `src/core/transaction/Transaction.hpp`). Use the accessors that exist there (`date()`, `amount().cents()`, `category()`, `counterpartyName()`).

- [ ] **Step 2: Run to verify failure**

Run: `make build 2>&1 | tail -5`
Expected: FAIL (`AdvisorService.hpp` missing).

- [ ] **Step 3: Write the header** (signatures exactly as in **Interfaces** above).

- [ ] **Step 4: Write the implementation**

`src/application/services/AdvisorService.cpp` — key pieces:
```cpp
#include "application/services/AdvisorService.hpp"
#include <nlohmann/json.hpp>
#include "core/transaction/Transaction.hpp"   // categoryName()

namespace ares::application::services {

auto AdvisorService::systemPrompt() -> std::string {
    return
      "You are a personal financial advisor analyzing ING-DE and PayPal data. "
      "All amounts are integer cents in EUR (negative = expense). Produce a concise "
      "report with four sections: (1) spending breakdown, (2) trends over time, "
      "(3) anomalies and forgotten subscriptions, (4) prioritized, actionable "
      "suggestions with euro impact. Be specific and reference real categories/merchants.";
}

auto AdvisorService::buildPayload(
    const std::vector<core::Transaction>& txns,
    const std::vector<core::Account>& accounts,
    const std::vector<core::Credit>& credits,
    const infrastructure::config::UserConfig& config,
    int months) -> std::string {
    nlohmann::json j;
    j["currency"] = "EUR";

    j["accounts"] = nlohmann::json::array();
    for (const auto& a : accounts) {
        j["accounts"].push_back({{"name", a.name()},
                                 {"balance_cents", a.balance().cents()}});
    }
    j["credits"] = nlohmann::json::array();
    for (const auto& c : credits) {
        j["credits"].push_back({{"name", c.name()},
                                {"balance_cents", c.currentBalance().cents()}});
    }
    j["budgets"] = nlohmann::json::array();
    for (const auto& b : config.budgets) {
        j["budgets"].push_back({{"category", std::string{core::categoryName(b.category)}},
                                {"limit_cents", b.limit.cents()}});
    }

    // category_totals + transactions (respect `months` if you have a date cutoff helper;
    // otherwise include all and note the count).
    nlohmann::json cat = nlohmann::json::object();
    j["transactions"] = nlohmann::json::array();
    for (const auto& t : txns) {
        std::string c{core::categoryName(t.category())};
        cat[c] = cat.value(c, int64_t{0}) + t.amount().cents();
        j["transactions"].push_back({
            {"date", t.date()},
            {"cents", t.amount().cents()},
            {"category", c},
            {"merchant", t.counterpartyName().value_or("")},
        });
    }
    j["category_totals"] = cat;
    j["months"] = months;
    return j.dump();
}

auto AdvisorService::generateReport(
    infrastructure::ai::ClaudeClient& client,
    const std::vector<core::Transaction>& txns,
    const std::vector<core::Account>& accounts,
    const std::vector<core::Credit>& credits,
    const infrastructure::config::UserConfig& config,
    int months) -> std::expected<std::string, core::Error> {
    return client.complete(systemPrompt(), buildPayload(txns, accounts, credits, config, months));
}

} // namespace ares::application::services
```
> Use the actual accessor names from the headers (`a.name()`, `a.balance()`, `c.currentBalance()`, `t.date()`, etc.). If `t.date()` returns a non-string type, format it to `YYYY-MM-DD`. Add both files to `ares_application` sources and ensure it links `nlohmann_json` (add to `ares_application` link if not transitive).

- [ ] **Step 5: Run tests**

Run: `make test 2>&1 | grep -E "ai..advisor|tests passed"`
Expected: all `[ai][advisor]` PASS.

- [ ] **Step 6: Commit**
```bash
git add src/application/services/AdvisorService.* tests/unit/application/AdvisorServiceTests.cpp CMakeLists.txt
git commit -m "feat(application): add AdvisorService (payload + orchestration)"
```

---

### Task 8: `ares analyze` CLI command

**Files:**
- Modify: `src/presentation/cli/CliApp.cpp` (add subcommand, near other `add_subcommand` calls)
- Test: `tests/unit/application/ApiKeyTests.cpp` (helper for env-key resolution)

**Interfaces:**
- Consumes: `AdvisorService`, `ClaudeClient`, `CurlHttpTransport`, repositories, `ConfigService`.
- Produces: a small free function `[[nodiscard]] auto resolveApiKey() -> std::expected<std::string, core::Error>` (in CliApp.cpp or a tiny header) that reads `ANTHROPIC_API_KEY`.

- [ ] **Step 1: Write the failing test for key resolution**
```cpp
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include "presentation/cli/ApiKey.hpp"   // declares resolveApiKey()

using ares::presentation::cli::resolveApiKey;

TEST_CASE("resolveApiKey errors when env var unset", "[ai][cli]") {
    unsetenv("ANTHROPIC_API_KEY");
    auto r = resolveApiKey();
    REQUIRE_FALSE(r.has_value());
    REQUIRE(std::holds_alternative<ares::core::ApiKeyMissingError>(r.error()));
}

TEST_CASE("resolveApiKey returns the value when set", "[ai][cli]") {
    setenv("ANTHROPIC_API_KEY", "sk-xyz", 1);
    auto r = resolveApiKey();
    REQUIRE(r.has_value());
    REQUIRE(*r == "sk-xyz");
}
```

- [ ] **Step 2: Run to verify failure**

Run: `make build 2>&1 | tail -5`
Expected: FAIL (`ApiKey.hpp` missing).

- [ ] **Step 3: Add the key helper**

`src/presentation/cli/ApiKey.hpp`:
```cpp
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
```
Register the test in `CMakeLists.txt`.

- [ ] **Step 4: Wire the `analyze` subcommand**

In `CliApp.cpp`, near other `add_subcommand` calls, add (match local naming for db/config/repos used by neighboring commands):
```cpp
auto* analyze_cmd = app.add_subcommand("analyze", "AI-powered spending analysis");
int analyze_months = 12;
analyze_cmd->add_option("--months", analyze_months, "Months of history to analyze");
analyze_cmd->callback([&]() {
    auto key = presentation::cli::resolveApiKey();
    if (!key) { fmt::print("Error: {}\n", core::errorMessage(key.error())); return; }

    auto dbResult = getDatabase();
    if (!dbResult) { fmt::print("Error: {}\n", core::errorMessage(dbResult.error())); return; }

    infrastructure::persistence::SqliteTransactionRepository txnRepo{*dbResult};
    infrastructure::persistence::SqliteAccountRepository acctRepo{*dbResult};
    infrastructure::persistence::SqliteCreditRepository creditRepo{*dbResult};
    auto txns = txnRepo.findAll();
    auto accts = acctRepo.findAll();
    auto credits = creditRepo.findAll();
    if (!txns) { fmt::print("Error: {}\n", core::errorMessage(txns.error())); return; }

    application::services::ConfigService configService;
    auto cfg = configService.loadConfig();
    infrastructure::config::UserConfig config = cfg ? *cfg : infrastructure::config::UserConfig{};

    infrastructure::ai::CurlHttpTransport transport;
    infrastructure::ai::ClaudeClient client{transport, *key, config.aiModel};

    fmt::print("Analyzing {} months with {}...\n\n", analyze_months, config.aiModel);
    auto report = application::services::AdvisorService::generateReport(
        client, *txns, accts.value_or(std::vector<core::Account>{}),
        credits.value_or(std::vector<core::Credit>{}), config, analyze_months);
    if (!report) { fmt::print("Error: {}\n", core::errorMessage(report.error())); return; }
    fmt::print("{}\n", *report);
});
```
Add includes for the new headers at the top of `CliApp.cpp`.

- [ ] **Step 5: Build and run tests**

Run: `make test 2>&1 | grep -E "ai..cli|tests passed"`
Expected: both `[ai][cli]` PASS; full suite green.

- [ ] **Step 6: Commit**
```bash
git add src/presentation/cli/ApiKey.hpp src/presentation/cli/CliApp.cpp tests/unit/application/ApiKeyTests.cpp CMakeLists.txt
git commit -m "feat(cli): add `ares analyze` AI advisor command"
```

---

### Task 9: End-to-end manual verification

**Files:** none (verification only).

- [ ] **Step 1: Build**

Run: `make build` → Expected: success, 0 warnings from `ares_warnings`.

- [ ] **Step 2: Verify the no-key path**

Run: `unset ANTHROPIC_API_KEY; ./build/ares analyze`
Expected: prints `Error: ANTHROPIC_API_KEY is not set` and exits cleanly (no crash, no network).

- [ ] **Step 3: Verify a real run** (requires a key)

Run: `export ANTHROPIC_API_KEY=sk-...; ./build/ares analyze --months 6`
Expected: prints a four-section report (breakdown, trends, anomalies, suggestions) referencing real categories/merchants.

- [ ] **Step 4: Verify full suite**

Run: `make test 2>&1 | tail -2`
Expected: `100% tests passed` (was 165; now 165 + new `[ai]`/`[config][ai]`/`[deps]` tests).

- [ ] **Step 5: Update CLAUDE.md**

Add `analyze` to any command list and note the new `ai.model` config key + `ANTHROPIC_API_KEY` requirement. Commit:
```bash
git add CLAUDE.md
git commit -m "docs: document ares analyze and ai.model config"
```

---

## Notes for the executing agent

- Exact accessor/constructor names for `Transaction`, `Account`, `Credit` come from their headers in `src/core/`. Where this plan guesses (e.g. `t.date()`), confirm against the header and adjust — the tests will catch mismatches at compile time.
- If `ConfigParser` exposes no `parseString`, parse a temp file instead (Task 6 note).
- Keep each task's commit green: `make test` must pass before moving on.
- Confirm the current Anthropic `anthropic-version` header value and request/response schema against docs while doing Task 4.
