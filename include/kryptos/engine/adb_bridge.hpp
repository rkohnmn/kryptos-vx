#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v2.0.0 "Aegis" — Native TCP/USB ADB Wire-Protocol Hardware Bridge
//   • Pure C++20 Implementation of the Android Debug Bridge (ADB) 24-Byte Binary
//     Framing Protocol (`CNXN`, `OPEN`, `OKAY`, `WRTE`, `CLSE`)
//   • Inspects and Disables Smart TV / Android TV Bloatware Packages over LAN
//     (`tcp:<ip>:5555`) with ZERO Android SDK `platform-tools` Dependencies
//   • Includes Built-In Loopback `MockAdbSmartTvDaemon` for Offline Simulation,
//     Deterministic Unit Testing, and Hardware Wire-Protocol Benchmarking
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/engine/debloater.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kryptos::engine {

// ─────────────────────────────────────────────────────────────────────────────
// Exact 24-Byte ADB Wire Protocol Header (Little-Endian)
//   magic == command ^ 0xFFFFFFFF
//   data_check == sum of all bytes in payload
// ─────────────────────────────────────────────────────────────────────────────
struct AdbPacketHeader {
    uint32_t command{0};
    uint32_t arg0{0};
    uint32_t arg1{0};
    uint32_t data_length{0};
    uint32_t data_check{0};
    uint32_t magic{0};
};
static_assert(sizeof(AdbPacketHeader) == 24, "AdbPacketHeader must be exactly 24 bytes");

struct AdbPacket {
    AdbPacketHeader header{};
    std::string payload{};
};

struct AdbCommandExecution {
    std::string shell_command;
    std::string device_stdout;
    bool success{false};
    double roundtrip_ms{0.0};
};

struct AdbSessionReport {
    std::string target_endpoint;
    std::string device_banner;
    uint32_t negotiated_maxdata{0};
    bool connected{false};
    size_t packets_sent{0};
    size_t packets_received{0};
    double handshake_latency_ms{0.0};
    double total_session_ms{0.0};
    std::vector<AdbCommandExecution> executed_commands;
};

class AdbBridge {
public:
    static constexpr uint32_t CMD_SYNC = 0x434e5953U; // "SYNC"
    static constexpr uint32_t CMD_CNXN = 0x4e584e43U; // "CNXN"
    static constexpr uint32_t CMD_OPEN = 0x4e45504fU; // "OPEN"
    static constexpr uint32_t CMD_OKAY = 0x59414b4fU; // "OKAY"
    static constexpr uint32_t CMD_WRTE = 0x45545257U; // "WRTE"
    static constexpr uint32_t CMD_CLSE = 0x45534c43U; // "CLSE"

    static constexpr uint32_t ADB_VERSION = 0x01000000U;
    static constexpr uint32_t ADB_MAXDATA = 256 * 1024U;

    // Compute official ADB wire payload checksum (sum of all unsigned bytes)
    [[nodiscard]] static uint32_t compute_payload_checksum(std::string_view data) noexcept;

    // Construct a verified 24-byte ADB wire packet + payload
    [[nodiscard]] static AdbPacket build_packet(
        uint32_t command,
        uint32_t arg0,
        uint32_t arg1,
        std::string_view payload = {}
    );

    // Verify packet header magic (`command ^ 0xffffffff`) and payload checksum
    [[nodiscard]] static bool verify_packet(const AdbPacket& pkt) noexcept;

    // Execute a sequence of shell commands against a live TCP ADB endpoint (`host:port`)
    // using the native 24-byte binary ADB state machine (`CNXN` -> `OPEN` -> `WRTE` -> `CLSE`).
    [[nodiscard]] static AdbSessionReport execute_over_tcp(
        const std::string& host,
        uint16_t port,
        const std::vector<std::string>& shell_commands,
        uint32_t timeout_ms = 1500
    );

    // Run a self-contained Smart TV ADB hardware simulation on loopback (`127.0.0.1`)
    // spawning a real TCP Mock Smart TV ADB Daemon and executing the curated
    // Smart TV de-bloater commands over the binary ADB wire protocol.
    [[nodiscard]] static AdbSessionReport run_simulated_smart_tv_debloat(
        DeviceProfile profile = DeviceProfile::SonyBraviaAndroidTV
    );

    // Render formatted ADB hardware bridge session report
    static void render_session_report(const AdbSessionReport& report, bool use_color = true);
};

} // namespace kryptos::engine
