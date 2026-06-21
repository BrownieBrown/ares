# AI Spending Advisor — Design Spec

- **Date:** 2026-06-20
- **Status:** Approved for implementation
- **Scope:** Session 1 of 3 (Core + `ares analyze`)

## Goal

Add an AI advisor to ares that reads the user's financial data, analyzes spending
patterns, and produces actionable money-saving suggestions — automating the manual
analysis workflow. This spec covers **Session 1 only**: the shared Claude-API core
plus the first surface, a one-shot `ares analyze` report.

Later sessions (separate specs): Session 2 = `ares ask` (interactive Q&A);
Session 3 = proactive alerts in `overview`. Both reuse the core built here.

## Design decisions (locked)

| Decision | Choice |
|---|---|
| Provider | Anthropic Claude API (Messages API) |
| Data sent | Full transaction detail (user accepted the privacy tradeoff) |
| Model | Configurable via `config.yaml` `ai.model:`, default `claude-sonnet-4-6` |
| Auth | `ANTHROPIC_API_KEY` environment variable |
| HTTP | libcurl (system lib, `find_package`) |
| JSON | nlohmann/json (header-only, CMake FetchContent) |
| Error handling | `std::expected<T, core::Error>` (matches codebase) |

## Architecture

New code follows the existing clean-architecture layering.

```
infrastructure/ai/
  HttpTransport.hpp            # abstract interface
  CurlHttpTransport.{hpp,cpp}  # libcurl implementation (real HTTP/TLS)
  ClaudeClient.{hpp,cpp}       # builds Anthropic request, parses response
application/services/
  AdvisorService.{hpp,cpp}     # orchestrates: gather data -> prompt -> client -> report
presentation/cli/
  CliApp.cpp                   # add `analyze` subcommand (existing file, extended)
infrastructure/config/
  ConfigParser / ConfigService # extended to read `ai.model`
```

### Components & interfaces

**`HttpTransport` (interface)** — the seam that keeps tests off the network.
```cpp
struct HttpResponse { int status; std::string body; };
class HttpTransport {
public:
  virtual ~HttpTransport() = default;
  [[nodiscard]] virtual auto post(
      std::string_view url,
      const std::vector<std::pair<std::string,std::string>>& headers,
      std::string_view body) -> std::expected<HttpResponse, core::Error> = 0;
};
```
- `CurlHttpTransport` — real implementation using libcurl.
- `FakeHttpTransport` (tests only) — returns canned `HttpResponse`/errors.

**`ClaudeClient`** — knows the Anthropic Messages API wire format; knows nothing
about finance.
- Constructed with `HttpTransport&`, an API key, and a model id.
- `complete(systemPrompt, userMessage) -> std::expected<std::string, core::Error>`.
- Builds JSON body (`model`, `max_tokens`, `system`, `messages`), sets headers
  (`x-api-key`, `anthropic-version: 2023-06-01`, `content-type: application/json`),
  POSTs to `https://api.anthropic.com/v1/messages`, extracts `content[0].text`.
- Maps non-2xx / API error bodies to `core::Error` (`ApiError`).

**`AdvisorService`** — the finance-aware orchestrator.
- Pulls categorized transactions, accounts, credits, budgets, and computed
  summaries (monthly totals, category breakdown) from existing repositories.
- Serializes them into the prompt payload (see Prompt Design).
- Calls `ClaudeClient::complete`, returns the report text (or `Error`).

### Data flow

```
ares analyze [--months N]
  -> AdvisorService.generateReport(months)
     -> read transactions/accounts/credits/budgets via repositories
     -> compute summaries (monthly trend, per-category totals)
     -> build payload (structured JSON inside the user message)
     -> ClaudeClient.complete(SYSTEM_PROMPT, payload)
        -> CurlHttpTransport.post(...)  (libcurl -> Anthropic)
     -> return report text
  -> CLI prints formatted report
```

## Prompt design

- **System prompt** establishes role and output contract: a personal financial
  advisor analyzing ING-DE / PayPal data; amounts are int64 cents EUR; produce a
  structured report with four sections — (1) spending breakdown, (2) trends over
  time, (3) anomalies / forgotten subscriptions, (4) prioritized, actionable
  suggestions with euro impact.
- **User message** carries a compact JSON payload:
  ```json
  {
    "currency": "EUR",
    "period": {"from": "YYYY-MM", "to": "YYYY-MM"},
    "accounts": [{"name": "...", "type": "...", "balance_cents": -120677}],
    "credits": [{"name": "...", "balance_cents": 2332974, "rate": 5.3}],
    "budgets": [{"category": "restaurants", "limit_cents": 25000}],
    "monthly_totals": [{"month": "2026-05", "income_cents": ..., "spend_cents": ...}],
    "category_totals": [{"category": "shopping", "cents": ...}],
    "transactions": [{"date": "...", "cents": -2647, "category": "...", "merchant": "..."}]
  }
  ```
- `--months N` bounds how many recent months of transactions are included
  (default: all). Keeps token cost and relevance in check.

## Config & auth

- `ANTHROPIC_API_KEY` read from environment. If absent, `analyze` fails fast with a
  clear message (no network call).
- `config.yaml` gains an optional `ai:` block:
  ```yaml
  ai:
    model: claude-sonnet-4-6   # optional; this is the default
  ```
- Config loading extends the existing `ConfigParser`/`ConfigService`; absence of the
  block yields the default model.

## Error handling

All fallible calls return `std::expected<T, core::Error>`. New `Error` variants
(extend the existing enum), each with a user-facing message:

| Variant | Cause |
|---|---|
| `ApiKeyMissing` | `ANTHROPIC_API_KEY` not set |
| `HttpError` | libcurl/transport failure (no response) |
| `ApiError` | non-2xx response or Anthropic error body (include status + message) |
| `JsonParseError` | malformed request/response JSON |

The CLI surfaces `core::errorMessage(...)` exactly as other commands do.

## Testing strategy

Per CLAUDE.md ("every public API must have tests"); **no test touches the network**.

- **`FakeHttpTransport`** returns canned responses/errors.
- `ClaudeClient`: request shape (correct URL, headers, JSON body for a given input),
  happy-path response parsing, and each error path (non-2xx, malformed JSON).
- `AdvisorService`: deterministic payload serialization from fixture transactions;
  correct summary computation; error propagation when the client fails.
- Command wiring: `analyze` resolves config/model, reports `ApiKeyMissing` cleanly.

## Build / CMake notes

- Add `find_package(CURL REQUIRED)`; link `CURL::libcurl` into the infrastructure lib.
- Add nlohmann/json via `FetchContent` (matches how fmt/CLI11/Catch2 are fetched).
- New sources join the existing `ares_infrastructure` / `ares_application` targets.

## Out of scope (Session 1)

Streaming, interactive Q&A, proactive alerts, response caching, retry/backoff beyond
a single attempt, multi-provider support. Deferred to Sessions 2–3 or later.

## Risks / notes

- **Token cost** on large histories — mitigated by `--months` and sending summaries
  alongside (not instead of) transactions.
- **API/version drift** — the implementer should confirm the current
  `anthropic-version` header and request schema against Anthropic docs at build time.
- **Privacy** — full transaction detail leaves the machine by explicit user choice;
  the report itself is printed, not stored.
