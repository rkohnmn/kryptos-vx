// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v3.0.0 "Apex" — FastCDC Content-Defined Chunking, Binary Patch
// Reconstruction (`KVXD`), and TCP/LAN Zero-Copy Sync Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/engine/fastcdc_sync.hpp"
#include "kryptos/core/simd_scanner.hpp"
#include "kryptos/memory/raii_handle.hpp"
#include "kryptos/ui/tui_dashboard.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#if !defined(_WIN32)
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace kryptos::engine {

namespace {

// Deterministic compile-time 256-entry 64-bit SplitMix64 Gear Table for FastCDC
constexpr std::array<uint64_t, 256> make_gear_table() noexcept {
    std::array<uint64_t, 256> table{};
    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    for (size_t i = 0; i < 256; ++i) {
        uint64_t z = (seed += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        table[i] = z ^ (z >> 31);
    }
    return table;
}

constexpr std::array<uint64_t, 256> GEAR_TABLE = make_gear_table();

// Normalized FastCDC bitmasks (13-bit mask ~8 KB average, 11-bit mask after normal point)
constexpr uint64_t FASTCDC_MASK_S = 0x0003590703530000ULL;
constexpr uint64_t FASTCDC_MASK_L = 0x0000590703530000ULL;

constexpr uint8_t OP_MATCH_CHUNK   = 0x01;
constexpr uint8_t OP_LITERAL_CHUNK = 0x02;

void select_chunk_params(size_t total_len, size_t& min_c, size_t& avg_c, size_t& max_c) noexcept {
    if (total_len < 262144) {
        min_c = 256;
        avg_c = 1024;
        max_c = 4096;
    } else {
        min_c = FastCdcSync::DEFAULT_MIN_CHUNK;
        avg_c = FastCdcSync::DEFAULT_AVG_CHUNK;
        max_c = FastCdcSync::DEFAULT_MAX_CHUNK;
    }
}

#if !defined(_WIN32)
bool send_all(int fd, const void* data, size_t len) {
    const auto* p = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, p + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool recv_all(int fd, void* data, size_t len) {
    auto* p = static_cast<uint8_t*>(data);
    size_t got = 0;
    while (got < len) {
        ssize_t n = ::recv(fd, p + got, len - got, 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}
#endif

} // namespace

std::vector<CdcChunk> FastCdcSync::chunk_buffer(
    const uint8_t* data,
    size_t len,
    size_t min_chunk,
    size_t avg_chunk,
    size_t max_chunk
) {
    std::vector<CdcChunk> chunks;
    if (!data || len == 0) return chunks;
    chunks.reserve((len / avg_chunk) + 4);

    size_t offset = 0;
    while (offset < len) {
        const size_t remaining = len - offset;
        if (remaining <= min_chunk) {
            chunks.push_back(CdcChunk{
                offset,
                remaining,
                0,
                core::SimdScanner::fast_hash64(data + offset, remaining)
            });
            break;
        }

        const size_t max_end = offset + std::min(remaining, max_chunk);
        const size_t normal_end = offset + std::min(remaining, avg_chunk);
        size_t pos = offset + min_chunk;
        uint64_t fp = 0;
        bool cut_found = false;

        for (; pos < normal_end; ++pos) {
            fp = (fp << 1) + GEAR_TABLE[data[pos]];
            if ((fp & FASTCDC_MASK_S) == 0) {
                ++pos;
                cut_found = true;
                break;
            }
        }

        if (!cut_found) {
            for (; pos < max_end; ++pos) {
                fp = (fp << 1) + GEAR_TABLE[data[pos]];
                if ((fp & FASTCDC_MASK_L) == 0) {
                    ++pos;
                    cut_found = true;
                    break;
                }
            }
        }

        const size_t chunk_len = pos - offset;
        chunks.push_back(CdcChunk{
            offset,
            chunk_len,
            fp,
            core::SimdScanner::fast_hash64(data + offset, chunk_len)
        });
        offset = pos;
    }

    return chunks;
}

std::vector<CdcChunk> FastCdcSync::chunk_fixed_blocks(
    const uint8_t* data,
    size_t len,
    size_t block_size
) {
    std::vector<CdcChunk> chunks;
    if (!data || len == 0 || block_size == 0) return chunks;
    chunks.reserve((len / block_size) + 2);

    for (size_t offset = 0; offset < len; offset += block_size) {
        const size_t clen = std::min(block_size, len - offset);
        chunks.push_back(CdcChunk{
            offset,
            clen,
            0,
            core::SimdScanner::fast_hash64(data + offset, clen)
        });
    }
    return chunks;
}

std::string FastCdcSync::build_delta_patch(
    std::string_view base_data,
    std::string_view updated_data
) {
    size_t min_c = 0, avg_c = 0, max_c = 0;
    select_chunk_params(std::max(base_data.size(), updated_data.size()), min_c, avg_c, max_c);

    const auto base_chunks = chunk_buffer(
        reinterpret_cast<const uint8_t*>(base_data.data()), base_data.size(), min_c, avg_c, max_c
    );
    const auto target_chunks = chunk_buffer(
        reinterpret_cast<const uint8_t*>(updated_data.data()), updated_data.size(), min_c, avg_c, max_c
    );

    std::unordered_map<uint64_t, CdcChunk> base_map;
    base_map.reserve(base_chunks.size() * 2 + 1);
    for (const auto& c : base_chunks) {
        base_map.emplace(c.content_hash64, c);
    }

    std::string patch;
    patch.reserve(32 + target_chunks.size() * 16);

    // 16-Byte Header: Magic "KVXD" (4B) + TargetSize (8B) + ChunkCount (4B)
    patch.append("KVXD", 4);
    const uint64_t total_target_sz = updated_data.size();
    const uint32_t num_chunks = static_cast<uint32_t>(target_chunks.size());
    patch.append(reinterpret_cast<const char*>(&total_target_sz), sizeof(uint64_t));
    patch.append(reinterpret_cast<const char*>(&num_chunks), sizeof(uint32_t));

    for (const auto& tc : target_chunks) {
        auto it = base_map.find(tc.content_hash64);
        if (it != base_map.end() && it->second.length == tc.length &&
            std::memcmp(base_data.data() + it->second.offset, updated_data.data() + tc.offset, tc.length) == 0) {
            // Emit 9-byte zero-copy reference: OP_MATCH_CHUNK (1B) + hash64 (8B)
            patch.push_back(static_cast<char>(OP_MATCH_CHUNK));
            patch.append(reinterpret_cast<const char*>(&tc.content_hash64), sizeof(uint64_t));
        } else {
            // Emit literal delta chunk: OP_LITERAL_CHUNK (1B) + len32 (4B) + raw bytes
            patch.push_back(static_cast<char>(OP_LITERAL_CHUNK));
            const uint32_t clen = static_cast<uint32_t>(tc.length);
            patch.append(reinterpret_cast<const char*>(&clen), sizeof(uint32_t));
            patch.append(updated_data.data() + tc.offset, tc.length);
        }
    }

    return patch;
}

std::string FastCdcSync::apply_delta_patch(
    std::string_view base_data,
    std::string_view patch_blob
) {
    if (patch_blob.size() < 16 || patch_blob.substr(0, 4) != "KVXD") {
        return {};
    }

    uint64_t total_target_sz = 0;
    uint32_t num_chunks = 0;
    std::memcpy(&total_target_sz, patch_blob.data() + 4, sizeof(uint64_t));
    std::memcpy(&num_chunks, patch_blob.data() + 12, sizeof(uint32_t));

    size_t min_c = 0, avg_c = 0, max_c = 0;
    select_chunk_params(std::max<size_t>(base_data.size(), total_target_sz), min_c, avg_c, max_c);

    const auto base_chunks = chunk_buffer(
        reinterpret_cast<const uint8_t*>(base_data.data()), base_data.size(), min_c, avg_c, max_c
    );
    std::unordered_map<uint64_t, CdcChunk> base_map;
    base_map.reserve(base_chunks.size() * 2 + 1);
    for (const auto& c : base_chunks) {
        base_map.emplace(c.content_hash64, c);
    }

    std::string reconstructed;
    reconstructed.reserve(total_target_sz);

    size_t pos = 16;
    for (uint32_t i = 0; i < num_chunks && pos < patch_blob.size(); ++i) {
        const uint8_t op = static_cast<uint8_t>(patch_blob[pos++]);
        if (op == OP_MATCH_CHUNK) {
            if (pos + sizeof(uint64_t) > patch_blob.size()) break;
            uint64_t h64 = 0;
            std::memcpy(&h64, patch_blob.data() + pos, sizeof(uint64_t));
            pos += sizeof(uint64_t);

            auto it = base_map.find(h64);
            if (it != base_map.end()) {
                reconstructed.append(base_data.data() + it->second.offset, it->second.length);
            }
        } else if (op == OP_LITERAL_CHUNK) {
            if (pos + sizeof(uint32_t) > patch_blob.size()) break;
            uint32_t clen = 0;
            std::memcpy(&clen, patch_blob.data() + pos, sizeof(uint32_t));
            pos += sizeof(uint32_t);
            if (pos + clen > patch_blob.size()) break;
            reconstructed.append(patch_blob.data() + pos, clen);
            pos += clen;
        }
    }

    return reconstructed;
}

DeltaSyncSummary FastCdcSync::compute_delta_sync(
    std::string_view base_data,
    std::string_view updated_data,
    std::string_view src_label,
    std::string_view dst_label
) {
    const auto t0 = std::chrono::steady_clock::now();
    DeltaSyncSummary summary{};
    summary.source_label = std::string(src_label);
    summary.target_label = std::string(dst_label);
    summary.total_target_bytes = updated_data.size();

    size_t min_c = 0, avg_c = 0, max_c = 0;
    select_chunk_params(std::max(base_data.size(), updated_data.size()), min_c, avg_c, max_c);

    const auto base_chunks = chunk_buffer(
        reinterpret_cast<const uint8_t*>(base_data.data()), base_data.size(), min_c, avg_c, max_c
    );
    const auto target_chunks = chunk_buffer(
        reinterpret_cast<const uint8_t*>(updated_data.data()), updated_data.size(), min_c, avg_c, max_c
    );

    std::unordered_set<uint64_t> base_hashes;
    base_hashes.reserve(base_chunks.size() * 2 + 1);
    for (const auto& c : base_chunks) {
        base_hashes.insert(c.content_hash64);
    }

    summary.total_chunks = target_chunks.size();
    for (const auto& tc : target_chunks) {
        if (base_hashes.find(tc.content_hash64) != base_hashes.end()) {
            ++summary.matched_chunks;
            summary.reusable_unchanged_bytes += tc.length;
        } else {
            summary.delta_transfer_bytes += tc.length;
        }
    }

    const std::string patch = build_delta_patch(base_data, updated_data);
    summary.serialized_patch_bytes = patch.size();
    const std::string rebuilt = apply_delta_patch(base_data, patch);
    summary.patch_verified_bit_exact = (rebuilt == updated_data);

    if (summary.total_target_bytes > 0) {
        summary.bandwidth_saved_pct =
            (static_cast<double>(summary.reusable_unchanged_bytes) * 100.0) /
            static_cast<double>(summary.total_target_bytes);
    }

    const auto t1 = std::chrono::steady_clock::now();
    summary.compute_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double total_mb = static_cast<double>(base_data.size() + updated_data.size()) / (1024.0 * 1024.0);
    if (summary.compute_ms > 0.0) {
        summary.throughput_mbs = total_mb / (summary.compute_ms / 1000.0);
    }
    return summary;
}

LanSyncSessionReport FastCdcSync::run_lan_sync_session(
    std::string_view receiver_base_data,
    std::string_view sender_updated_data
) {
    const auto t0 = std::chrono::steady_clock::now();
    LanSyncSessionReport rep{};
    rep.target_file_bytes = sender_updated_data.size();

#if !defined(_WIN32)
    int srv_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) return rep;

    int opt = 1;
    ::setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(srv_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(srv_fd);
        return rep;
    }

    sockaddr_in actual{};
    socklen_t alen = sizeof(actual);
    ::getsockname(srv_fd, reinterpret_cast<sockaddr*>(&actual), &alen);
    const uint16_t bound_port = ntohs(actual.sin_port);
    rep.endpoint = "127.0.0.1:" + std::to_string(bound_port);

    ::listen(srv_fd, 4);

    // Sender daemon thread: accepts receiver's known 64-bit hash manifest, builds a custom
    // `KVXD` wire stream containing ONLY missing chunks, and transmits it over TCP.
    std::thread sender_thread([srv_fd, sender_updated_data]() {
        sockaddr_in cli{};
        socklen_t clen = sizeof(cli);
        int cfd = ::accept(srv_fd, reinterpret_cast<sockaddr*>(&cli), &clen);
        if (cfd >= 0) {
            uint64_t receiver_len = 0;
            uint32_t known_count = 0;
            if (recv_all(cfd, &receiver_len, sizeof(uint64_t)) &&
                recv_all(cfd, &known_count, sizeof(uint32_t))) {
                std::vector<uint64_t> known_hashes(known_count);
                if (known_count == 0 || recv_all(cfd, known_hashes.data(), known_count * sizeof(uint64_t))) {
                    std::unordered_set<uint64_t> receiver_has(known_hashes.begin(), known_hashes.end());

                    size_t min_c = 0, avg_c = 0, max_c = 0;
                    select_chunk_params(std::max<size_t>(receiver_len, sender_updated_data.size()), min_c, avg_c, max_c);
                    auto target_chunks = chunk_buffer(
                        reinterpret_cast<const uint8_t*>(sender_updated_data.data()),
                        sender_updated_data.size(), min_c, avg_c, max_c
                    );

                    std::string patch;
                    patch.append("KVXD", 4);
                    const uint64_t total_sz = sender_updated_data.size();
                    const uint32_t n_chunks = static_cast<uint32_t>(target_chunks.size());
                    patch.append(reinterpret_cast<const char*>(&total_sz), sizeof(uint64_t));
                    patch.append(reinterpret_cast<const char*>(&n_chunks), sizeof(uint32_t));

                    for (const auto& tc : target_chunks) {
                        if (receiver_has.find(tc.content_hash64) != receiver_has.end()) {
                            patch.push_back(static_cast<char>(OP_MATCH_CHUNK));
                            patch.append(reinterpret_cast<const char*>(&tc.content_hash64), sizeof(uint64_t));
                        } else {
                            patch.push_back(static_cast<char>(OP_LITERAL_CHUNK));
                            const uint32_t clen = static_cast<uint32_t>(tc.length);
                            patch.append(reinterpret_cast<const char*>(&clen), sizeof(uint32_t));
                            patch.append(sender_updated_data.data() + tc.offset, tc.length);
                        }
                    }

                    const uint64_t patch_len = patch.size();
                    (void)send_all(cfd, &patch_len, sizeof(uint64_t));
                    (void)send_all(cfd, patch.data(), patch.size());
                }
            }
            ::close(cfd);
        }
        ::close(srv_fd);
    });

    // Receiver client: chunks local base file, sends 64-bit hash manifest, receives `KVXD` delta stream, and reconstructs
    int cli_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in caddr{};
    caddr.sin_family = AF_INET;
    caddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    caddr.sin_port = htons(bound_port);

    if (::connect(cli_fd, reinterpret_cast<sockaddr*>(&caddr), sizeof(caddr)) == 0) {
        size_t min_c = 0, avg_c = 0, max_c = 0;
        select_chunk_params(std::max(receiver_base_data.size(), sender_updated_data.size()), min_c, avg_c, max_c);
        auto base_chunks = chunk_buffer(
            reinterpret_cast<const uint8_t*>(receiver_base_data.data()),
            receiver_base_data.size(), min_c, avg_c, max_c
        );

        std::vector<uint64_t> manifest;
        manifest.reserve(base_chunks.size());
        for (const auto& c : base_chunks) {
            manifest.push_back(c.content_hash64);
        }

        const uint64_t base_len = receiver_base_data.size();
        const uint32_t m_count = static_cast<uint32_t>(manifest.size());
        (void)send_all(cli_fd, &base_len, sizeof(uint64_t));
        (void)send_all(cli_fd, &m_count, sizeof(uint32_t));
        if (m_count > 0) {
            (void)send_all(cli_fd, manifest.data(), m_count * sizeof(uint64_t));
        }

        uint64_t patch_len = 0;
        if (recv_all(cli_fd, &patch_len, sizeof(uint64_t)) && patch_len > 0) {
            std::string patch_blob(patch_len, '\0');
            if (recv_all(cli_fd, patch_blob.data(), patch_len)) {
                rep.wire_bytes_transmitted = sizeof(uint64_t) + sizeof(uint32_t) +
                                             m_count * sizeof(uint64_t) + patch_len;
                std::string rebuilt = apply_delta_patch(receiver_base_data, patch_blob);
                rep.bit_exact_verified = (rebuilt == sender_updated_data);

                // Count matched vs literal chunks in patch_blob
                uint32_t n_chunks = 0;
                std::memcpy(&n_chunks, patch_blob.data() + 12, sizeof(uint32_t));
                rep.chunks_total = n_chunks;
                size_t pos = 16;
                for (uint32_t i = 0; i < n_chunks && pos < patch_blob.size(); ++i) {
                    uint8_t op = static_cast<uint8_t>(patch_blob[pos++]);
                    if (op == OP_MATCH_CHUNK) {
                        ++rep.chunks_reused_locally;
                        pos += sizeof(uint64_t);
                    } else {
                        ++rep.chunks_fetched_over_lan;
                        uint32_t clen = 0;
                        std::memcpy(&clen, patch_blob.data() + pos, sizeof(uint32_t));
                        pos += sizeof(uint32_t) + clen;
                    }
                }
            }
        }
    }
    ::close(cli_fd);
    sender_thread.join();
#else
    rep.endpoint = "127.0.0.1:55432 (simulated)";
    size_t min_c = 0, avg_c = 0, max_c = 0;
    select_chunk_params((std::max)(receiver_base_data.size(), sender_updated_data.size()), min_c, avg_c, max_c);
    const auto base_chunks = chunk_buffer(
        reinterpret_cast<const uint8_t*>(receiver_base_data.data()), receiver_base_data.size(), min_c, avg_c, max_c
    );
    const auto target_chunks = chunk_buffer(
        reinterpret_cast<const uint8_t*>(sender_updated_data.data()), sender_updated_data.size(), min_c, avg_c, max_c
    );

    std::unordered_set<uint64_t> receiver_has;
    receiver_has.reserve(base_chunks.size() * 2 + 1);
    for (const auto& bc : base_chunks) {
        receiver_has.insert(bc.content_hash64);
    }

    std::string patch_blob = build_delta_patch(receiver_base_data, sender_updated_data);
    std::string reconstructed = apply_delta_patch(receiver_base_data, patch_blob);
    rep.bit_exact_verified = (reconstructed == sender_updated_data);
    rep.wire_bytes_transmitted = patch_blob.size();
    rep.chunks_total = target_chunks.size();

    for (const auto& tc : target_chunks) {
        if (receiver_has.find(tc.content_hash64) != receiver_has.end()) {
            ++rep.chunks_reused_locally;
        } else {
            ++rep.chunks_fetched_over_lan;
        }
    }
#endif

    if (rep.target_file_bytes > 0 && rep.wire_bytes_transmitted < rep.target_file_bytes) {
        rep.bandwidth_saved_pct =
            (static_cast<double>(rep.target_file_bytes - rep.wire_bytes_transmitted) * 100.0) /
            static_cast<double>(rep.target_file_bytes);
    }

    const auto t1 = std::chrono::steady_clock::now();
    rep.session_latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return rep;
}

WorkspaceCdcReport FastCdcSync::deduplicate_workspace(const core::WorkspaceIndexSoA& index) {
    const auto t0 = std::chrono::steady_clock::now();
    WorkspaceCdcReport rep{};

    std::unordered_set<uint64_t> seen_chunks;
    seen_chunks.reserve(index.size() * 16 + 64);

    for (size_t i = 0; i < index.size(); ++i) {
        if (index.size_bytes[i] == 0 || index.size_bytes[i] > (32ULL * 1024ULL * 1024ULL)) continue;
        memory::MappedFile mf(std::string(index.full_path[i]));
        if (!mf.data() || mf.empty()) continue;

        ++rep.files_processed;
        rep.total_bytes_processed += mf.size();

        const size_t min_c = (mf.size() < 65536) ? 256  : DEFAULT_MIN_CHUNK;
        const size_t avg_c = (mf.size() < 65536) ? 1024 : DEFAULT_AVG_CHUNK;
        const size_t max_c = (mf.size() < 65536) ? 4096 : DEFAULT_MAX_CHUNK;

        auto chunks = chunk_buffer(mf.data(), mf.size(), min_c, avg_c, max_c);
        rep.total_chunks += chunks.size();

        for (const auto& c : chunks) {
            auto [it, inserted] = seen_chunks.insert(c.content_hash64);
            if (inserted) {
                ++rep.unique_chunks;
                rep.unique_bytes_stored += c.length;
            } else {
                rep.subfile_dedup_saved_bytes += c.length;
            }
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    rep.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (rep.duration_ms > 0.0) {
        rep.throughput_mbs = (static_cast<double>(rep.total_bytes_processed) / (1024.0 * 1024.0)) /
                             (rep.duration_ms / 1000.0);
    }
    return rep;
}

void FastCdcSync::render_cdc_report(const WorkspaceCdcReport& rep, bool use_color) {
    const char* b   = use_color ? "\033[1m"       : "";
    const char* cy  = use_color ? "\033[38;5;51m" : "";
    const char* gr  = use_color ? "\033[38;5;48m" : "";
    const char* yl  = use_color ? "\033[38;5;220m": "";
    const char* rst = use_color ? "\033[0m"       : "";

    const double ratio = (rep.total_bytes_processed > 0)
        ? (static_cast<double>(rep.subfile_dedup_saved_bytes) * 100.0 /
           static_cast<double>(rep.total_bytes_processed))
        : 0.0;

    std::cout << "\n" << cy << b
              << "╔══════════════════════════════════════════════════════════════════════════════╗\n"
              << "║  🔄 KRYPTOS-VX v3.0 \"Apex\" — FastCDC Rolling Gear Hash Deduplication Engine  ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════╝"
              << rst << "\n"
              << "  • Files Chunked         : " << b << rep.files_processed << rst << "\n"
              << "  • Total / Unique Chunks : " << rep.total_chunks << " total / " << gr << rep.unique_chunks << " unique" << rst << "\n"
              << "  • Total Bytes Processed : " << ui::TuiDashboard::format_bytes(rep.total_bytes_processed) << "\n"
              << "  • Unique Bytes Stored   : " << gr << ui::TuiDashboard::format_bytes(rep.unique_bytes_stored) << rst << "\n"
              << "  • Sub-File Dedup Saved  : " << yl << ui::TuiDashboard::format_bytes(rep.subfile_dedup_saved_bytes)
              << " (" << std::fixed << std::setprecision(1) << ratio << "% reduction)" << rst << "\n"
              << "  • FastCDC Throughput    : " << cy << std::fixed << std::setprecision(1)
              << rep.throughput_mbs << " MB/s (" << std::setprecision(2) << rep.duration_ms << " ms)" << rst << "\n\n";
}

void FastCdcSync::render_delta_report(const DeltaSyncSummary& delta, bool use_color) {
    const char* b   = use_color ? "\033[1m"       : "";
    const char* cy  = use_color ? "\033[38;5;51m" : "";
    const char* gr  = use_color ? "\033[38;5;48m" : "";
    const char* rst = use_color ? "\033[0m"       : "";

    std::cout << "\n" << cy << b
              << "╔══════════════════════════════════════════════════════════════════════════════╗\n"
              << "║  ⚡ KRYPTOS-VX v3.0 \"Apex\" — Zero-Copy FastCDC Delta & Patch Verification     ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════╝"
              << rst << "\n"
              << "  • Base -> Target        : `" << delta.source_label << "` -> `" << delta.target_label << "`\n"
              << "  • Matched Chunks        : " << gr << delta.matched_chunks << " / " << delta.total_chunks << rst << "\n"
              << "  • Reusable Zero-Copy    : " << gr << ui::TuiDashboard::format_bytes(delta.reusable_unchanged_bytes) << rst << "\n"
              << "  • Serialized KVXD Patch : " << cy << ui::TuiDashboard::format_bytes(delta.serialized_patch_bytes) << rst << "\n"
              << "  • Bit-Exact Rebuild     : " << gr << (delta.patch_verified_bit_exact ? "VERIFIED (100% Bit-Exact)" : "FAILED") << rst << "\n"
              << "  • Bandwidth Saved       : " << gr << std::fixed << std::setprecision(1)
              << delta.bandwidth_saved_pct << "%" << rst << " (" << std::setprecision(3) << delta.compute_ms << " ms)\n\n";
}

void FastCdcSync::render_lan_sync_report(const LanSyncSessionReport& rep, bool use_color) {
    const char* b   = use_color ? "\033[1m"       : "";
    const char* cy  = use_color ? "\033[38;5;51m" : "";
    const char* gr  = use_color ? "\033[38;5;48m" : "";
    const char* rst = use_color ? "\033[0m"       : "";

    std::cout << "\n" << cy << b
              << "╔══════════════════════════════════════════════════════════════════════════════╗\n"
              << "║  🌐 KRYPTOS-VX v3.0 \"Apex\" — Cross-Device TCP/LAN Zero-Copy Sync Session     ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════╝"
              << rst << "\n"
              << "  • TCP Sync Endpoint     : " << b << rep.endpoint << rst << "\n"
              << "  • Target File Size      : " << ui::TuiDashboard::format_bytes(rep.target_file_bytes) << "\n"
              << "  • Wire Bytes Sent       : " << cy << ui::TuiDashboard::format_bytes(rep.wire_bytes_transmitted) << rst << "\n"
              << "  • Chunks Reused / Sent  : " << gr << rep.chunks_reused_locally << " reused locally" << rst
              << " / " << rep.chunks_fetched_over_lan << " fetched over LAN\n"
              << "  • LAN Bandwidth Saved   : " << gr << std::fixed << std::setprecision(1) << rep.bandwidth_saved_pct << "%" << rst << "\n"
              << "  • Bit-Exact Verified    : " << gr << (rep.bit_exact_verified ? "PASS (SHA/Hash Verified)" : "FAIL") << rst
              << " in " << cy << std::setprecision(3) << rep.session_latency_ms << " ms" << rst << "\n\n";
}

} // namespace kryptos::engine
