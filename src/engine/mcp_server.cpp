// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v3.0.0 "Aegis" — Native C++20 Model Context Protocol (MCP) Server
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/engine/mcp_server.hpp"
#include "kryptos/core/simd_scanner.hpp"
#include "kryptos/engine/code_auditor.hpp"
#include "kryptos/engine/context_packer.hpp"
#include "kryptos/engine/debloater.hpp"
#include "kryptos/engine/binary_profiler.hpp"
#include "kryptos/engine/duplicate_finder.hpp"
#include "kryptos/engine/fastcdc_sync.hpp"
#include "kryptos/ui/brand_logo.hpp"
#include "kryptos/ui/tui_dashboard.hpp"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace kryptos::engine {

McpServer::McpServer(const std::string& workspace_root)
    : workspace_root_(workspace_root) {
    refresh_workspace();
}

void McpServer::refresh_workspace() {
    index_.index_directory(workspace_root_, true, 4);
    trigram_engine_.build_from_workspace(index_);
}

std::string McpServer::json_escape(std::string_view str) {
    std::string out;
    out.reserve(str.size() + 16);
    for (char c : str) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20) {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string McpServer::extract_json_string(std::string_view json, std::string_view key) {
    std::string pattern;
    pattern.reserve(key.size() + 2);
    pattern.push_back('"');
    pattern.append(key.data(), key.size());
    pattern.push_back('"');
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return {};
    pos += pattern.size();

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != ':') return {};
    ++pos;

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') return {};
    ++pos;

    std::string val;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            char next = json[pos++];
            switch (next) {
                case '"':  val += '"';  break;
                case '\\': val += '\\'; break;
                case 'n':  val += '\n'; break;
                case 'r':  val += '\r'; break;
                case 't':  val += '\t'; break;
                default:   val += next; break;
            }
        } else if (c == '"') {
            break;
        } else {
            val += c;
        }
    }
    return val;
}

std::string McpServer::extract_json_id(std::string_view json) {
    const std::string_view pattern = "\"id\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return "null";
    pos += pattern.size();

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ':')) {
        ++pos;
    }
    if (pos >= json.size()) return "null";

    if (json[pos] == '"') {
        size_t end = pos + 1;
        while (end < json.size() && json[end] != '"') {
            if (json[end] == '\\') ++end;
            ++end;
        }
        if (end < json.size()) {
            return std::string(json.substr(pos, end - pos + 1));
        }
    } else {
        size_t end = pos;
        while (end < json.size() &&
               ((json[end] >= '0' && json[end] <= '9') || json[end] == '-' ||
                (json[end] >= 'a' && json[end] <= 'z'))) {
            ++end;
        }
        if (end > pos) {
            return std::string(json.substr(pos, end - pos));
        }
    }
    return "null";
}

JsonRpcRequest McpServer::parse_jsonrpc_request(std::string_view raw_json) {
    JsonRpcRequest req{};
    if (raw_json.empty()) return req;

    req.id_raw = extract_json_id(raw_json);
    req.method = extract_json_string(raw_json, "method");
    if (req.method.empty()) return req;

    req.valid = true;

    // For `tools/call`, locate `"params"` block so `"name"` inside params is extracted
    size_t params_pos = raw_json.find("\"params\"");
    std::string_view params_view = (params_pos != std::string_view::npos)
        ? raw_json.substr(params_pos)
        : raw_json;

    req.tool_name = extract_json_string(params_view, "name");
    req.resource_uri = extract_json_string(params_view, "uri");
    req.query_arg = extract_json_string(params_view, "query");
    if (req.query_arg.empty()) {
        req.query_arg = extract_json_string(params_view, "pattern");
    }
    req.profile_arg = extract_json_string(params_view, "profile");

    const std::string max_tok_str = extract_json_string(params_view, "max_tokens");
    if (!max_tok_str.empty()) {
        req.max_tokens_arg = static_cast<size_t>(std::stoull(max_tok_str));
    }
    if (params_view.find("\"skeleton\":true") != std::string_view::npos ||
        params_view.find("\"skeleton\": true") != std::string_view::npos) {
        req.skeleton_arg = true;
    }

    return req;
}

std::string McpServer::handle_jsonrpc_message(
    std::string_view raw_json,
    McpDispatchTelemetry* telemetry_out
) {
    const auto t0 = std::chrono::steady_clock::now();
    ++requests_handled_;

    const JsonRpcRequest req = parse_jsonrpc_request(raw_json);
    McpDispatchTelemetry tel{};
    tel.method = req.method;
    tel.tool_or_resource = !req.tool_name.empty() ? req.tool_name : req.resource_uri;

    if (!req.valid) {
        std::string err = "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}";
        const auto t1 = std::chrono::steady_clock::now();
        tel.execution_latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        tel.response_bytes = err.size();
        if (telemetry_out) *telemetry_out = tel;
        return err;
    }

    // Notifications produce no response in JSON-RPC 2.0 / MCP
    if (req.method.starts_with("notifications/")) {
        const auto t1 = std::chrono::steady_clock::now();
        tel.execution_latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        tel.response_bytes = 0;
        if (telemetry_out) *telemetry_out = tel;
        return {};
    }

    std::ostringstream resp;
    resp << "{\"jsonrpc\":\"2.0\",\"id\":" << req.id_raw << ",";

    if (req.method == "initialize") {
        resp << "\"result\":{"
             << "\"protocolVersion\":\"2024-11-05\","
             << "\"capabilities\":{"
             << "\"tools\":{\"listChanged\":false},"
             << "\"resources\":{\"subscribe\":false,\"listChanged\":false}"
             << "},"
             << "\"serverInfo\":{"
             << "\"name\":\"kryptos-vx-aegis-mcp\","
             << "\"version\":\"3.0.0\""
             << "}"
             << "}}";
    } else if (req.method == "ping") {
        resp << "\"result\":{}}";
    } else if (req.method == "tools/list") {
        resp << "\"result\":{\"tools\":["
             << "{\"name\":\"kryptos_trigram_query\","
             << "\"description\":\"Sub-millisecond (<0.5ms) 24-bit inverted trigram + Multi-ISA SIMD code search\","
             << "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}},\"required\":[\"query\"]}},"
             << "{\"name\":\"kryptos_simd_search\","
             << "\"description\":\"Hardware AVX-512/AVX2/NEON substring search across workspace files\","
             << "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"string\"}},\"required\":[\"pattern\"]}},"
             << "{\"name\":\"kryptos_pack_context\","
             << "\"description\":\"Pack repository into an LLM context bundle with automatic 8-rule secret scrubbing\","
             << "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"skeleton\":{\"type\":\"boolean\"}}}},"
             << "{\"name\":\"kryptos_security_audit\","
             << "\"description\":\"Run Senior Engineering 100-point quality & credential security scorecard\","
             << "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
             << "{\"name\":\"kryptos_debloat_plan\","
             << "\"description\":\"Generate Smart TV (ADB/SDB/webOS) & Desktop Electron de-bloat manifest\","
             << "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"profile\":{\"type\":\"string\"}}}},"
             << "{\"name\":\"kryptos_fastcdc_dedup\","
             << "\"description\":\"Run FastCDC 64-bit Rolling Gear Hash sub-file deduplication analysis\","
             << "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
             << "{\"name\":\"kryptos_binary_profile\","
             << "\"description\":\"Profile ELF64/PE/Mach-O binary sections and strip reclaimable bytes\","
             << "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}}}},"
             << "{\"name\":\"kryptos_lan_delta_sync\","
             << "\"description\":\"Run FastCDC TCP/LAN zero-copy delta sync & KVXD bit-exact reconstruction\","
             << "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
             << "]}}";
    } else if (req.method == "resources/list") {
        resp << "\"result\":{\"resources\":["
             << "{\"uri\":\"kryptos://workspace/telemetry\","
             << "\"name\":\"Workspace Hardware & Kernel Telemetry\","
             << "\"mimeType\":\"application/json\"},"
             << "{\"uri\":\"kryptos://workspace/logo.svg\","
             << "\"name\":\"KRYPTOS-VX Embedded Vector SVG Logo\","
             << "\"mimeType\":\"image/svg+xml\"}"
             << "]}}";
    } else if (req.method == "resources/read") {
        if (req.resource_uri == "kryptos://workspace/logo.svg") {
            resp << "\"result\":{\"contents\":[{\"uri\":\"kryptos://workspace/logo.svg\","
                 << "\"mimeType\":\"image/svg+xml\","
                 << "\"text\":\"" << json_escape(ui::BrandLogo::EMBEDDED_SVG) << "\"}]}}";
        } else {
            const auto scorecard = CodeAuditor::evaluate(index_);
            const auto dups = DuplicateFinder::find_duplicates(index_);
            const std::string js = ui::TuiDashboard::format_json_report(index_, scorecard, dups);
            resp << "\"result\":{\"contents\":[{\"uri\":\"kryptos://workspace/telemetry\","
                 << "\"mimeType\":\"application/json\","
                 << "\"text\":\"" << json_escape(js) << "\"}]}}";
        }
    } else if (req.method == "tools/call") {
        std::ostringstream tool_text;
        bool is_error = false;

        if (req.tool_name == "kryptos_trigram_query" || req.tool_name == "kryptos_simd_search") {
            const auto qres = trigram_engine_.query(req.query_arg);
            tool_text << "[KRYPTOS-VX v2.0 Aegis MCP | Trigram+SIMD Search]\n"
                      << "Query: `" << req.query_arg << "` | Matches: " << qres.total_matches
                      << " | Candidates Evaluated: " << qres.candidate_files_evaluated
                      << "/" << qres.total_files_in_index
                      << " | Latency: " << std::fixed << std::setprecision(3) << qres.query_latency_ms << " ms\n";
            for (const auto& h : qres.hits) {
                for (size_t i = 0; i < h.line_numbers.size(); ++i) {
                    tool_text << "  - " << h.relative_path << ":" << h.line_numbers[i]
                              << " (byte " << h.byte_offsets[i] << ")\n";
                }
            }
        } else if (req.tool_name == "kryptos_pack_context") {
            PackConfig cfg{};
            cfg.max_token_budget = req.max_tokens_arg;
            cfg.redact_secrets = true;
            cfg.compress_signatures_only = req.skeleton_arg;
            const auto pres = ContextPacker::pack_workspace(index_, cfg);
            tool_text << "[KRYPTOS-VX v2.0 Aegis MCP | Context Pack]\n"
                      << "Files Packed: " << pres.total_files_packed
                      << " | Tokens: " << pres.total_tokens
                      << " | Secrets Redacted: " << pres.total_secrets_redacted << "\n\n"
                      << pres.formatted_bundle.substr(0, std::min<size_t>(pres.formatted_bundle.size(), 4096));
        } else if (req.tool_name == "kryptos_security_audit") {
            const auto scorecard = CodeAuditor::evaluate(index_);
            const auto dups = DuplicateFinder::find_duplicates(index_);
            tool_text << ui::TuiDashboard::format_json_report(index_, scorecard, dups);
        } else if (req.tool_name == "kryptos_debloat_plan") {
            const auto drep = Debloater::analyze_and_generate(index_, DeviceProfile::All);
            tool_text << "[KRYPTOS-VX v3.0 Apex MCP | De-Bloat Plan]\n"
                      << "Estimated Idle RAM Saved: ~" << drep.total_estimated_ram_saved_mb << " MB\n"
                      << drep.generated_reprogram_script;
        } else if (req.tool_name == "kryptos_fastcdc_dedup") {
            const auto cdc = FastCdcSync::deduplicate_workspace(index_);
            tool_text << "[KRYPTOS-VX v3.0 Apex MCP | FastCDC Sub-File Deduplication]\n"
                      << "Files: " << cdc.files_processed << " | Chunks: " << cdc.total_chunks
                      << " (" << cdc.unique_chunks << " unique) | Saved: " << cdc.subfile_dedup_saved_bytes << " bytes\n";
        } else if (req.tool_name == "kryptos_lan_delta_sync") {
            std::string b1 = "KRYPTOS_BASE_PAYLOAD_0123456789";
            std::string b2 = "KRYPTOS_INSERTED_PAYLOAD_0123456789";
            const auto sync_rep = FastCdcSync::run_lan_sync_session(b1, b2);
            tool_text << "[KRYPTOS-VX v3.0 Apex MCP | LAN Zero-Copy Sync]\n"
                      << "Bit-Exact Verified: " << (sync_rep.bit_exact_verified ? "true" : "false")
                      << " | Latency: " << sync_rep.session_latency_ms << " ms\n";
        } else if (req.tool_name == "kryptos_binary_profile") {
            std::string bin_target = req.query_arg.empty() ? "./bin/kryptos_vx" : req.query_arg;
            const auto bp = BinaryProfiler::profile_file(bin_target);
            tool_text << "[KRYPTOS-VX v3.0 Apex MCP | Binary Section Profiler]\n"
                      << "Binary: " << bp.file_path << " | Format: " << BinaryProfiler::format_name(bp.format)
                      << " | Arch: " << bp.architecture << " | Sections: " << bp.sections.size()
                      << " | Strip Reclaimable: " << bp.strip_reclaimable_bytes << " bytes\n";
        } else {
            is_error = true;
            tool_text << "Unknown MCP tool: " << req.tool_name;
        }

        resp << "\"result\":{\"content\":[{\"type\":\"text\",\"text\":\""
             << json_escape(tool_text.str()) << "\"}],\"isError\":"
             << (is_error ? "true" : "false") << "}}";
    } else {
        resp << "\"error\":{\"code\":-32601,\"message\":\"Method not found: "
             << json_escape(req.method) << "\"}}";
    }

    std::string final_str = resp.str();
    const auto t1 = std::chrono::steady_clock::now();
    tel.execution_latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    tel.response_bytes = final_str.size();
    if (telemetry_out) *telemetry_out = tel;
    return final_str;
}

int McpServer::run_stdio_loop(std::istream& in, std::ostream& out) {
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;

        // Support both Content-Length LSP framing and newline-delimited JSON-RPC
        if (line.starts_with("Content-Length:")) {
            const size_t len = static_cast<size_t>(std::stoull(line.substr(15)));
            // Consume blank separator line
            std::string sep;
            std::getline(in, sep);
            std::string body(len, '\0');
            in.read(body.data(), static_cast<std::streamsize>(len));
            std::string reply = handle_jsonrpc_message(body);
            if (!reply.empty()) {
                out << "Content-Length: " << reply.size() << "\r\n\r\n" << reply << std::flush;
            }
        } else if (line.front() == '{') {
            std::string reply = handle_jsonrpc_message(line);
            if (!reply.empty()) {
                out << reply << "\n" << std::flush;
            }
        }
    }
    return 0;
}

int McpServer::run_tcp_server(uint16_t port, size_t max_requests, uint16_t* bound_port_out) {
#if !defined(_WIN32)
    int srv_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) return 1;

    int opt = 1;
    ::setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (::bind(srv_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(srv_fd);
        return 1;
    }

    if (bound_port_out) {
        sockaddr_in actual{};
        socklen_t alen = sizeof(actual);
        if (::getsockname(srv_fd, reinterpret_cast<sockaddr*>(&actual), &alen) == 0) {
            *bound_port_out = ntohs(actual.sin_port);
        }
    }

    if (::listen(srv_fd, 8) < 0) {
        ::close(srv_fd);
        return 1;
    }

    size_t served = 0;
    while (max_requests == 0 || served < max_requests) {
        sockaddr_in cli_addr{};
        socklen_t cli_len = sizeof(cli_addr);
        int cli_fd = ::accept(srv_fd, reinterpret_cast<sockaddr*>(&cli_addr), &cli_len);
        if (cli_fd < 0) break;

        char buf[16384];
        std::string accum;
        while (true) {
            ssize_t n = ::recv(cli_fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            accum.append(buf, static_cast<size_t>(n));

            size_t nl_pos = 0;
            while ((nl_pos = accum.find('\n')) != std::string::npos) {
                std::string msg = accum.substr(0, nl_pos);
                accum.erase(0, nl_pos + 1);
                if (!msg.empty() && msg.back() == '\r') msg.pop_back();
                if (msg.empty()) continue;

                std::string reply = handle_jsonrpc_message(msg);
                ++served;
                if (!reply.empty()) {
                    reply += "\n";
                    (void)::send(cli_fd, reply.data(), reply.size(), 0);
                }
                if (max_requests > 0 && served >= max_requests) break;
            }
            if (max_requests > 0 && served >= max_requests) break;
        }
        ::close(cli_fd);
    }

    ::close(srv_fd);
    return 0;
#else
    (void)port;
    (void)max_requests;
    (void)bound_port_out;
    return 0;
#endif
}

} // namespace kryptos::engine
