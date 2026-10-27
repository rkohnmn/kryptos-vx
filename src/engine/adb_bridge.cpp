// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v2.0.0 "Aegis" — Native TCP/USB ADB Wire-Protocol Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/engine/adb_bridge.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace kryptos::engine {

uint32_t AdbBridge::compute_payload_checksum(std::string_view data) noexcept {
    uint32_t sum = 0;
    for (unsigned char c : data) {
        sum += static_cast<uint32_t>(c);
    }
    return sum;
}

AdbPacket AdbBridge::build_packet(
    uint32_t command,
    uint32_t arg0,
    uint32_t arg1,
    std::string_view payload
) {
    AdbPacket pkt{};
    pkt.header.command = command;
    pkt.header.arg0 = arg0;
    pkt.header.arg1 = arg1;
    pkt.header.data_length = static_cast<uint32_t>(payload.size());
    pkt.header.data_check = compute_payload_checksum(payload);
    pkt.header.magic = command ^ 0xFFFFFFFFU;
    pkt.payload.assign(payload.data(), payload.size());
    return pkt;
}

bool AdbBridge::verify_packet(const AdbPacket& pkt) noexcept {
    if ((pkt.header.command ^ 0xFFFFFFFFU) != pkt.header.magic) {
        return false;
    }
    if (pkt.header.data_length != pkt.payload.size()) {
        return false;
    }
    if (pkt.header.data_check != compute_payload_checksum(pkt.payload)) {
        return false;
    }
    return true;
}

namespace {

#if !defined(_WIN32)
bool send_exact(int fd, const void* buf, size_t len) {
    const auto* p = static_cast<const uint8_t*>(buf);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, p + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool recv_exact_timeout(int fd, void* buf, size_t len, uint32_t timeout_ms) {
    auto* p = static_cast<uint8_t*>(buf);
    size_t got = 0;
    while (got < len) {
        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int pret = ::poll(&pfd, 1, static_cast<int>(timeout_ms));
        if (pret <= 0) return false;

        ssize_t n = ::recv(fd, p + got, len - got, 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

bool send_adb_packet(int fd, const AdbPacket& pkt) {
    if (!send_exact(fd, &pkt.header, sizeof(AdbPacketHeader))) {
        return false;
    }
    if (!pkt.payload.empty()) {
        if (!send_exact(fd, pkt.payload.data(), pkt.payload.size())) {
            return false;
        }
    }
    return true;
}

bool recv_adb_packet(int fd, AdbPacket& out_pkt, uint32_t timeout_ms) {
    out_pkt = AdbPacket{};
    if (!recv_exact_timeout(fd, &out_pkt.header, sizeof(AdbPacketHeader), timeout_ms)) {
        return false;
    }
    if (out_pkt.header.data_length > (4U * 1024U * 1024U)) {
        return false;
    }
    if (out_pkt.header.data_length > 0) {
        out_pkt.payload.resize(out_pkt.header.data_length);
        if (!recv_exact_timeout(fd, out_pkt.payload.data(), out_pkt.header.data_length, timeout_ms)) {
            return false;
        }
    }
    return AdbBridge::verify_packet(out_pkt);
}
#endif

} // namespace

AdbSessionReport AdbBridge::execute_over_tcp(
    const std::string& host,
    uint16_t port,
    const std::vector<std::string>& shell_commands,
    uint32_t timeout_ms
) {
    const auto t0 = std::chrono::steady_clock::now();
    AdbSessionReport report{};
    report.target_endpoint = host + ":" + std::to_string(port);

#if !defined(_WIN32)
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return report;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        ::close(sock);
        return report;
    }

    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(sock);
        return report;
    }

    // 1. Send ADB CNXN handshake
    std::string host_banner = "host::features=shell_v2,cmd,stat_v2";
    host_banner.push_back('\0');
    const AdbPacket cnxn_pkt = build_packet(CMD_CNXN, ADB_VERSION, ADB_MAXDATA, host_banner);
    if (!send_adb_packet(sock, cnxn_pkt)) {
        ::close(sock);
        return report;
    }
    ++report.packets_sent;

    AdbPacket cnxn_resp{};
    if (!recv_adb_packet(sock, cnxn_resp, timeout_ms) || cnxn_resp.header.command != CMD_CNXN) {
        ::close(sock);
        return report;
    }
    ++report.packets_received;
    report.connected = true;
    report.negotiated_maxdata = cnxn_resp.header.arg1;
    report.device_banner = cnxn_resp.payload;
    if (!report.device_banner.empty() && report.device_banner.back() == '\0') {
        report.device_banner.pop_back();
    }

    const auto t_hs = std::chrono::steady_clock::now();
    report.handshake_latency_ms = std::chrono::duration<double, std::milli>(t_hs - t0).count();

    // 2. Execute each shell command over a dedicated ADB local stream ID (`OPEN` -> `OKAY` -> `WRTE` -> `CLSE`)
    uint32_t local_id = 1;
    for (const auto& raw_cmd : shell_commands) {
        const auto cmd_t0 = std::chrono::steady_clock::now();
        AdbCommandExecution exec{};
        exec.shell_command = raw_cmd;

        // Strip leading `adb shell ` prefix if present in curated rule
        std::string clean_cmd = raw_cmd;
        const auto pos = clean_cmd.find("shell ");
        if (pos != std::string::npos) {
            clean_cmd = clean_cmd.substr(pos + 6);
        }

        std::string open_dest = "shell:" + clean_cmd;
        open_dest.push_back('\0');

        const AdbPacket open_pkt = build_packet(CMD_OPEN, local_id, 0, open_dest);
        if (!send_adb_packet(sock, open_pkt)) break;
        ++report.packets_sent;

        uint32_t remote_id = 0;
        while (true) {
            AdbPacket incoming{};
            if (!recv_adb_packet(sock, incoming, timeout_ms)) break;
            ++report.packets_received;

            if (incoming.header.command == CMD_OKAY) {
                remote_id = incoming.header.arg0;
            } else if (incoming.header.command == CMD_WRTE) {
                remote_id = incoming.header.arg0;
                exec.device_stdout += incoming.payload;
                // Acknowledge WRTE with OKAY per ADB wire specification
                const AdbPacket ack = build_packet(CMD_OKAY, local_id, remote_id);
                (void)send_adb_packet(sock, ack);
                ++report.packets_sent;
            } else if (incoming.header.command == CMD_CLSE) {
                const AdbPacket clse_ack = build_packet(CMD_CLSE, local_id, remote_id);
                (void)send_adb_packet(sock, clse_ack);
                ++report.packets_sent;
                exec.success = true;
                break;
            }
        }

        const auto cmd_t1 = std::chrono::steady_clock::now();
        exec.roundtrip_ms = std::chrono::duration<double, std::milli>(cmd_t1 - cmd_t0).count();
        report.executed_commands.push_back(std::move(exec));
        ++local_id;
    }

    ::close(sock);
#else
    (void)timeout_ms;
#endif

    const auto t1 = std::chrono::steady_clock::now();
    report.total_session_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return report;
}

AdbSessionReport AdbBridge::run_simulated_smart_tv_debloat(DeviceProfile profile) {
    const auto rules = Debloater::get_curated_device_rules(profile);
    std::vector<std::string> cmds;
    cmds.reserve(rules.size());
    for (const auto& r : rules) {
        cmds.push_back(r.removal_command);
    }

#if !defined(_WIN32)
    int srv_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) return {};

    int opt = 1;
    ::setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // Ephemeral kernel-assigned loopback port

    if (::bind(srv_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(srv_fd);
        return {};
    }

    sockaddr_in actual{};
    socklen_t alen = sizeof(actual);
    ::getsockname(srv_fd, reinterpret_cast<sockaddr*>(&actual), &alen);
    const uint16_t bound_port = ntohs(actual.sin_port);

    ::listen(srv_fd, 4);

    // Spawn mock Smart TV adbd wire-protocol responder thread
    std::thread tv_daemon([srv_fd]() {
        sockaddr_in cli{};
        socklen_t clen = sizeof(cli);
        int cfd = ::accept(srv_fd, reinterpret_cast<sockaddr*>(&cli), &clen);
        if (cfd >= 0) {
            AdbPacket pkt{};
            // Expect CNXN
            if (recv_adb_packet(cfd, pkt, 1500) && pkt.header.command == CMD_CNXN) {
                std::string banner = "device::ro.product.name=BRAVIA_4K_VH2;ro.product.model=Sony_XR_A95L;features=shell_v2,cmd";
                banner.push_back('\0');
                const AdbPacket resp_cnxn = build_packet(CMD_CNXN, ADB_VERSION, ADB_MAXDATA, banner);
                (void)send_adb_packet(cfd, resp_cnxn);

                uint32_t device_stream_id = 100;
                while (recv_adb_packet(cfd, pkt, 1500)) {
                    if (pkt.header.command == CMD_OPEN) {
                        const uint32_t client_id = pkt.header.arg0;
                        // 1. Send OKAY
                        (void)send_adb_packet(cfd, build_packet(CMD_OKAY, device_stream_id, client_id));

                        // 2. Send WRTE with simulated package manager output
                        std::string out_msg = "Package state updated: disabled-user [0] for stream (" +
                                              std::string(pkt.payload.c_str()) + ")\n";
                        (void)send_adb_packet(cfd, build_packet(CMD_WRTE, device_stream_id, client_id, out_msg));

                        // Expect OKAY from client
                        AdbPacket ack{};
                        (void)recv_adb_packet(cfd, ack, 1500);

                        // 3. Send CLSE
                        (void)send_adb_packet(cfd, build_packet(CMD_CLSE, device_stream_id, client_id));

                        // Expect CLSE from client
                        (void)recv_adb_packet(cfd, ack, 1500);
                        ++device_stream_id;
                    }
                }
            }
            ::close(cfd);
        }
        ::close(srv_fd);
    });

    AdbSessionReport rep = execute_over_tcp("127.0.0.1", bound_port, cmds, 1500);
    tv_daemon.join();
    return rep;
#else
    AdbSessionReport rep{};
    rep.target_endpoint = "127.0.0.1:5555 (simulated)";
    rep.connected = true;
    rep.device_banner = "device::ro.product.name=BRAVIA_4K_VH2;ro.product.model=Sony_XR_A95L;features=shell_v2,cmd";
    rep.negotiated_maxdata = ADB_MAXDATA;
    rep.packets_sent = static_cast<uint32_t>(1 + cmds.size() * 2);
    rep.packets_received = static_cast<uint32_t>(1 + cmds.size() * 2);
    rep.handshake_latency_ms = 0.5;
    rep.total_session_ms = 1.5;
    for (const auto& cmd : cmds) {
        rep.executed_commands.push_back(AdbCommandExecution{
            cmd,
            "Package state updated: disabled-user [0] for " + cmd + "\n",
            true,
            0.2
        });
    }
    return rep;
#endif
}

void AdbBridge::render_session_report(const AdbSessionReport& report, bool use_color) {
    const char* b   = use_color ? "\033[1m"        : "";
    const char* cy  = use_color ? "\033[38;5;51m"  : "";
    const char* gr  = use_color ? "\033[38;5;48m"  : "";
    const char* rd  = use_color ? "\033[38;5;196m" : "";
    const char* rst = use_color ? "\033[0m"        : "";

    std::cout << "\n" << cy << b
              << "╔══════════════════════════════════════════════════════════════════════════════╗\n"
              << "║  🔌 KRYPTOS-VX v2.0 \"Aegis\" — Native ADB Wire-Protocol Hardware Bridge       ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════╝"
              << rst << "\n";
    std::cout << "  • Target Endpoint      : " << b << report.target_endpoint << rst << "\n"
              << "  • Connection Status    : " << (report.connected ? (std::string(gr) + "CONNECTED (CNXN Verified)" + rst)
                                                                    : (std::string(rd) + "UNREACHABLE" + rst)) << "\n"
              << "  • Device Identity      : `" << report.device_banner << "`\n"
              << "  • Negotiated MaxData   : " << report.negotiated_maxdata << " bytes\n"
              << "  • Wire Packets (TX/RX) : " << gr << report.packets_sent << " TX / "
              << report.packets_received << " RX" << rst << " (24-Byte Binary Framing)\n"
              << "  • Handshake / Total    : " << cy << std::fixed << std::setprecision(3)
              << report.handshake_latency_ms << " ms / " << report.total_session_ms << " ms" << rst << "\n\n";

    for (size_t i = 0; i < report.executed_commands.size(); ++i) {
        const auto& ex = report.executed_commands[i];
        std::cout << "  [" << (i + 1) << "] " << b << ex.shell_command << rst
                  << " (" << cy << std::fixed << std::setprecision(3) << ex.roundtrip_ms << " ms" << rst << ")\n"
                  << "      → " << gr << ex.device_stdout << rst;
    }
    std::cout << "\n";
}

} // namespace kryptos::engine
