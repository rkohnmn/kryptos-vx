#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v2.0.0 "Aegis" — Native C++20 Model Context Protocol (MCP) Server
//   • Zero-Dependency JSON-RPC 2.0 Parser & Dispatcher (`2024-11-05` / `2025-03-26` MCP Spec)
//   • Transports: `stdio` (newline-delimited + `Content-Length` framing) & TCP Socket
//   • Sub-Millisecond (< 0.5 ms) Live Queries against `TrigramWatcherEngine`,
//     `SimdScanner`, `ContextPacker`, `CodeAuditor`, and `Debloater`
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/soa_index.hpp"
#include "kryptos/engine/trigram_watcher.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace kryptos::engine {

struct JsonRpcRequest {
    std::string jsonrpc{"2.0"};
    std::string id_raw{"null"}; // Preserves string or integer ID verbatim
    std::string method;
    std::string tool_name;      // For tools/call
    std::string resource_uri;   // For resources/read
    std::string query_arg;      // Extracted "query" or "pattern" argument
    std::string profile_arg;    // Extracted "profile" argument
    size_t max_tokens_arg{64000};
    bool skeleton_arg{false};
    bool valid{false};
};

struct McpDispatchTelemetry {
    std::string method;
    std::string tool_or_resource;
    double execution_latency_ms{0.0};
    size_t response_bytes{0};
};

class McpServer {
    std::string workspace_root_{"."};
    core::WorkspaceIndexSoA index_{};
    TrigramWatcherEngine trigram_engine_{};
    size_t requests_handled_{0};

public:
    explicit McpServer(const std::string& workspace_root = ".");

    // Re-index or refresh workspace state
    void refresh_workspace();

    // Parse a raw JSON-RPC 2.0 request payload (zero external JSON library needed)
    [[nodiscard]] static JsonRpcRequest parse_jsonrpc_request(std::string_view raw_json);

    // Handle a single JSON-RPC 2.0 message and return the serialized JSON-RPC 2.0 response.
    // Returns empty string for notifications (e.g., `notifications/initialized`) per MCP spec.
    [[nodiscard]] std::string handle_jsonrpc_message(
        std::string_view raw_json,
        McpDispatchTelemetry* telemetry_out = nullptr
    );

    // Run MCP server over standard input/output (`stdio` transport for Claude Code / Cursor)
    int run_stdio_loop(std::istream& in = std::cin, std::ostream& out = std::cout);

    // Run MCP server over a local TCP port (`127.0.0.1:<port>`) for local LLM agents
    // If `max_requests > 0`, exits cleanly after serving `max_requests` requests.
    int run_tcp_server(uint16_t port, size_t max_requests = 0, uint16_t* bound_port_out = nullptr);

    [[nodiscard]] size_t requests_handled() const noexcept { return requests_handled_; }
    [[nodiscard]] const core::WorkspaceIndexSoA& workspace_index() const noexcept { return index_; }
    [[nodiscard]] const TrigramWatcherEngine& trigram_engine() const noexcept { return trigram_engine_; }

    // Helper to extract a string field value from a flat or nested JSON object
    [[nodiscard]] static std::string extract_json_string(std::string_view json, std::string_view key);
    [[nodiscard]] static std::string extract_json_id(std::string_view json);
    [[nodiscard]] static std::string json_escape(std::string_view str);
};

} // namespace kryptos::engine
