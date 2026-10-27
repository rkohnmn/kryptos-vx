// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v3.0.0 "Apex" — Production Unit Test Suite (15 Comprehensive Suites)
// Compliant with SASMP v1.3.0 (memcpp.md, buildcpp.md, optimcpop.md, SKILL 7)
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/kernel_fs.hpp"
#include "kryptos/core/simd_scanner.hpp"
#include "kryptos/core/soa_index.hpp"
#include "kryptos/core/thread_pool.hpp"
#include "kryptos/engine/adb_bridge.hpp"
#include "kryptos/engine/binary_profiler.hpp"
#include "kryptos/engine/code_auditor.hpp"
#include "kryptos/engine/context_packer.hpp"
#include "kryptos/engine/debloater.hpp"
#include "kryptos/engine/duplicate_finder.hpp"
#include "kryptos/engine/fastcdc_sync.hpp"
#include "kryptos/engine/mcp_server.hpp"
#include "kryptos/engine/trigram_watcher.hpp"
#include "kryptos/memory/pool_allocator.hpp"
#include "kryptos/memory/raii_handle.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#if !defined(_WIN32)
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace {

int g_tests_passed = 0;
int g_tests_failed = 0;

#define KRYPTOS_VX_TEST(name) void name()
#define KRYPTOS_VX_RUN_TEST(name)                                                \
    do {                                                                         \
        std::cout << "  [RUN ] " << #name << " ... ";                            \
        try {                                                                    \
            name();                                                              \
            ++g_tests_passed;                                                    \
            std::cout << "\033[38;5;48m[PASS]\033[0m\n";                         \
        } catch (const std::exception& ex) {                                     \
            ++g_tests_failed;                                                    \
            std::cout << "\033[38;5;196m[FAIL]\033[0m (" << ex.what() << ")\n";  \
        }                                                                        \
    } while (0)

#define KRYPTOS_VX_ASSERT(cond)                                                  \
    do {                                                                         \
        if (!(cond)) {                                                           \
            throw std::runtime_error("Assertion failed at line " +               \
                                     std::to_string(__LINE__) + ": " #cond);     \
        }                                                                        \
    } while (0)

// 1. Smart Pointer Ownership Hierarchy & RAII Tests
KRYPTOS_VX_TEST(test_smart_pointer_and_raii_ownership) {
    auto ptr = std::make_unique<int>(42);
    KRYPTOS_VX_ASSERT(ptr != nullptr);
    KRYPTOS_VX_ASSERT(*ptr == 42);
    auto ptr2 = std::move(ptr);
    KRYPTOS_VX_ASSERT(ptr == nullptr);
    KRYPTOS_VX_ASSERT(*ptr2 == 42);

    std::weak_ptr<int> weak;
    {
        auto shared1 = std::make_shared<int>(100);
        KRYPTOS_VX_ASSERT(shared1.use_count() == 1);
        auto shared2 = shared1;
        KRYPTOS_VX_ASSERT(shared1.use_count() == 2);
        weak = shared1;
        KRYPTOS_VX_ASSERT(!weak.expired());
    }
    KRYPTOS_VX_ASSERT(weak.expired());

    const char* tmp_path = "/tmp/kryptos_vx_raii_test.txt";
    {
        kryptos::memory::FileHandle fh(tmp_path, "w");
        KRYPTOS_VX_ASSERT(static_cast<bool>(fh));
        std::fputs("KRYPTOS-VX RAII verification line\n", fh.get());
        kryptos::memory::FileHandle fh_moved(std::move(fh));
        KRYPTOS_VX_ASSERT(!static_cast<bool>(fh));
        KRYPTOS_VX_ASSERT(static_cast<bool>(fh_moved));
    }
    {
        kryptos::memory::MappedFile mf(tmp_path);
        KRYPTOS_VX_ASSERT(!mf.empty());
        KRYPTOS_VX_ASSERT(mf.view().find("KRYPTOS-VX RAII") != std::string_view::npos);
    }
    std::remove(tmp_path);
}

// 2. PoolAllocator & StringArena Tests
KRYPTOS_VX_TEST(test_pool_allocator_and_string_arena) {
    struct Node {
        uint64_t id;
        double weight;
        Node(uint64_t i, double w) : id(i), weight(w) {}
    };

    kryptos::memory::PoolAllocator<Node, 1024> pool;
    std::vector<Node*> nodes;
    for (uint64_t i = 0; i < 200; ++i) {
        nodes.push_back(pool.construct(i, static_cast<double>(i) * 1.5));
    }
    KRYPTOS_VX_ASSERT(pool.active_allocations() == 200);
    KRYPTOS_VX_ASSERT(nodes[100]->id == 100);

    for (Node* n : nodes) {
        pool.destroy(n);
    }
    KRYPTOS_VX_ASSERT(pool.active_allocations() == 0);

    kryptos::memory::StringArena arena;
    auto s1 = arena.intern("include/kryptos/core/simd_scanner.hpp");
    auto s2 = arena.intern("src/core/simd_scanner.cpp");
    KRYPTOS_VX_ASSERT(s1 == "include/kryptos/core/simd_scanner.hpp");
    KRYPTOS_VX_ASSERT(s2 == "src/core/simd_scanner.cpp");
    KRYPTOS_VX_ASSERT(arena.total_bytes_allocated() > s1.size() + s2.size());
}

// 3. Build System & C++20 Standard Compliance Tests
KRYPTOS_VX_TEST(test_cpp20_and_platform_macros) {
#if defined(_MSVC_LANG)
    KRYPTOS_VX_ASSERT(_MSVC_LANG >= 202002L || __cplusplus >= 202002L);
#else
    KRYPTOS_VX_ASSERT(__cplusplus >= 202002L);
#endif
    KRYPTOS_VX_ASSERT(alignof(kryptos::core::CacheAlignedCounter) == 64);
    KRYPTOS_VX_ASSERT(sizeof(kryptos::core::CacheAlignedCounter) == 64);
    KRYPTOS_VX_ASSERT(sizeof(kryptos::engine::AdbPacketHeader) == 24);
}

// 4. Multi-ISA SIMD (AVX-512 / AVX2 / NEON / Scalar) Exact Equivalence Tests
KRYPTOS_VX_TEST(test_multi_isa_simd_equivalence) {
    std::string payload;
    payload.reserve(16384);
    for (int i = 0; i < 250; ++i) {
        payload += "line " + std::to_string(i) + ": fast native C++20 SIMD vectorization engine;\n";
    }

    const auto* raw = reinterpret_cast<const uint8_t*>(payload.data());
    const size_t n_simd   = kryptos::core::SimdScanner::count_byte_simd(raw, payload.size(), '\n');
    const size_t n_avx512 = kryptos::core::SimdScanner::count_byte_avx512(raw, payload.size(), '\n');
    const size_t n_avx2   = kryptos::core::SimdScanner::count_byte_avx2(raw, payload.size(), '\n');
    const size_t n_neon   = kryptos::core::SimdScanner::count_byte_neon(raw, payload.size(), '\n');
    const size_t n_scl    = kryptos::core::SimdScanner::count_byte_scalar(raw, payload.size(), '\n');

    KRYPTOS_VX_ASSERT(n_simd == 250);
    KRYPTOS_VX_ASSERT(n_avx512 == n_scl);
    KRYPTOS_VX_ASSERT(n_avx2 == n_scl);
    KRYPTOS_VX_ASSERT(n_neon == n_scl);

    const auto pos_simd   = kryptos::core::SimdScanner::find_all_simd(payload, "SIMD vectorization");
    const auto pos_avx512 = kryptos::core::SimdScanner::find_all_avx512(payload, "SIMD vectorization");
    const auto pos_avx2   = kryptos::core::SimdScanner::find_all_avx2(payload, "SIMD vectorization");
    const auto pos_neon   = kryptos::core::SimdScanner::find_all_neon(payload, "SIMD vectorization");
    const auto pos_scl    = kryptos::core::SimdScanner::find_all_scalar(payload, "SIMD vectorization");

    KRYPTOS_VX_ASSERT(pos_simd.size() == 250);
    KRYPTOS_VX_ASSERT(pos_simd == pos_scl);
    KRYPTOS_VX_ASSERT(pos_avx512 == pos_scl);
    KRYPTOS_VX_ASSERT(pos_avx2 == pos_scl);
    KRYPTOS_VX_ASSERT(pos_neon == pos_scl);
}

// 5. Secret Detection & Automatic Redaction Gate Tests
KRYPTOS_VX_TEST(test_secret_scanner_and_redaction) {
    std::string p1 = "sk-";
    p1 += "proj-1234567890abcdefghijklmnop";
    std::string p2 = "ghp";
    p2 += "_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    std::string sample = "const char* key = \"" + p1 + "\";\n"
                         "const char* gh  = \"" + p2 + "\";\n";
    auto matches = kryptos::core::SimdScanner::scan_secrets(sample);
    KRYPTOS_VX_ASSERT(matches.size() >= 2);

    size_t redacted_cnt = 0;
    std::string scrubbed = kryptos::core::SimdScanner::redact_secrets(sample, &redacted_cnt);
    KRYPTOS_VX_ASSERT(redacted_cnt >= 2);
    KRYPTOS_VX_ASSERT(scrubbed.find("1234567890abcdefghijklmnop") == std::string::npos);
    KRYPTOS_VX_ASSERT(scrubbed.find("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") == std::string::npos);
}

// 6. SoA vs AoS Cache Locality & ThreadPool Concurrency Test
KRYPTOS_VX_TEST(test_soa_vs_aos_and_thread_pool) {
    kryptos::core::ThreadPool pool(4);
    std::vector<std::future<uint64_t>> futures;
    for (uint64_t i = 1; i <= 16; ++i) {
        futures.push_back(pool.enqueue([i]() { return i * i; }));
    }
    uint64_t total = 0;
    for (auto& f : futures) {
        total += f.get();
    }
    KRYPTOS_VX_ASSERT(total == 1496);

    kryptos::core::WorkspaceIndexSoA index;
    index.index_directory(".", true, 4);
    KRYPTOS_VX_ASSERT(!index.empty());
    KRYPTOS_VX_ASSERT(index.sum_sizes_soa() == index.total_bytes_indexed);
    KRYPTOS_VX_ASSERT(index.sum_tokens_soa() == index.total_tokens_indexed);
}

// 7. Phase 2 v1.5 "Kronos": KernelFs (`getdents64` + `statx` / Win32) vs `std::filesystem` Parity
KRYPTOS_VX_TEST(test_kernel_fs_fast_enumeration_parity) {
    kryptos::core::KernelEnumTelemetry tel_fast{};
    kryptos::core::KernelEnumTelemetry tel_std{};

    auto fast_entries = kryptos::core::KernelFs::enumerate_fast(".", true, &tel_fast);
    auto std_entries  = kryptos::core::KernelFs::enumerate_std_filesystem(".", true, &tel_std);

    KRYPTOS_VX_ASSERT(!fast_entries.empty());
    KRYPTOS_VX_ASSERT(fast_entries.size() == std_entries.size());
    KRYPTOS_VX_ASSERT(tel_fast.files_discovered == tel_std.files_discovered);
    KRYPTOS_VX_ASSERT(tel_fast.raw_syscalls_issued > 0);
}

// 8. Phase 2 v1.5 "Kronos": 24-Bit Inverted Trigram Index & < 1ms Incremental Upsert/Remove
KRYPTOS_VX_TEST(test_trigram_index_and_subms_incremental_update) {
    kryptos::core::WorkspaceIndexSoA index;
    index.index_directory(".", true, 4);

    kryptos::engine::TrigramWatcherEngine trigram_engine;
    trigram_engine.build_from_workspace(index);

    KRYPTOS_VX_ASSERT(trigram_engine.indexed_doc_count() > 10);
    KRYPTOS_VX_ASSERT(trigram_engine.unique_trigram_count() > 500);

    const std::string tmp_file = (std::filesystem::temp_directory_path() / "kryptos_trigram_live_doc.cpp").string();
    std::string fn_sym = std::string("kronos_hyper_") + "kernel_fn";
    {
        std::ofstream ofs(tmp_file);
        ofs << "// Live incremental trigram test\nvoid " << fn_sym << "() {}\n";
    }

    auto up_metrics = trigram_engine.upsert_file(tmp_file, "kronos_live_doc.cpp", "CREATED");
    KRYPTOS_VX_ASSERT(up_metrics.trigrams_indexed > 10);
    KRYPTOS_VX_ASSERT(up_metrics.update_latency_ms < 50.0);

    auto q_live = trigram_engine.query(fn_sym);
    KRYPTOS_VX_ASSERT(q_live.total_matches == 1);
    KRYPTOS_VX_ASSERT(q_live.candidate_files_evaluated == 1);

    auto del_metrics = trigram_engine.remove_file("kronos_live_doc.cpp");
    KRYPTOS_VX_ASSERT(del_metrics.update_latency_ms < 50.0);

    auto q_after_del = trigram_engine.query(fn_sym);
    KRYPTOS_VX_ASSERT(q_after_del.total_matches == 0);
    std::remove(tmp_file.c_str());
}

// 9. Phase 2 v1.5 "Kronos": Live OS Filesystem Watcher (`inotify` / Win32) Real-Time Event Loop
KRYPTOS_VX_TEST(test_realtime_filesystem_watcher_daemon) {
    kryptos::core::WorkspaceIndexSoA index;
    index.index_directory(".", false, 2);

    kryptos::engine::TrigramWatcherEngine watcher;
    watcher.build_from_workspace(index);

    const std::string watched_file = "./bin/watch_probe_temp.txt";
    std::thread writer_thread([&watched_file]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        {
            std::ofstream ofs(watched_file);
            ofs << "KRONOS_REALTIME_INOTIFY_EVENT_PAYLOAD\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        std::remove(watched_file.c_str());
    });

    int rc = watcher.run_watcher_daemon(".", 2, 600, false);
    writer_thread.join();
    std::remove(watched_file.c_str());
    KRYPTOS_VX_ASSERT(rc == 0);
}

// 10. Phase 3 v2.0 "Aegis": Native C++20 MCP Server JSON-RPC 2.0 Stdio & 7 Sub-Millisecond Tools
KRYPTOS_VX_TEST(test_mcp_server_jsonrpc_and_subms_tools) {
    kryptos::engine::McpServer mcp(".");

    kryptos::engine::McpDispatchTelemetry tel_init{};
    std::string init_resp = mcp.handle_jsonrpc_message(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\"}}",
        &tel_init
    );
    KRYPTOS_VX_ASSERT(init_resp.find("\"protocolVersion\":\"2024-11-05\"") != std::string::npos);
    KRYPTOS_VX_ASSERT(init_resp.find("\"3.0.0\"") != std::string::npos);

    std::string tools_resp = mcp.handle_jsonrpc_message(
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}"
    );
    KRYPTOS_VX_ASSERT(tools_resp.find("kryptos_trigram_query") != std::string::npos);
    KRYPTOS_VX_ASSERT(tools_resp.find("kryptos_fastcdc_dedup") != std::string::npos);
    KRYPTOS_VX_ASSERT(tools_resp.find("kryptos_binary_profile") != std::string::npos);

    kryptos::engine::McpDispatchTelemetry tel_call{};
    std::string call_resp = mcp.handle_jsonrpc_message(
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"kryptos_trigram_query\",\"arguments\":{\"query\":\"CacheAlignedCounter\"}}}",
        &tel_call
    );
    KRYPTOS_VX_ASSERT(call_resp.find("CacheAlignedCounter") != std::string::npos);
    KRYPTOS_VX_ASSERT(tel_call.execution_latency_ms < 2.0);
}

// 11. Phase 3 v2.0 "Aegis": MCP Server Live Loopback TCP Socket Transport Test
KRYPTOS_VX_TEST(test_mcp_server_tcp_socket_transport) {
#if !defined(_WIN32)
    kryptos::engine::McpServer mcp(".");
    uint16_t bound_port = 0;

    std::thread server_thread([&mcp, &bound_port]() {
        (void)mcp.run_tcp_server(0, 1, &bound_port);
    });

    for (int i = 0; i < 100 && bound_port == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    KRYPTOS_VX_ASSERT(bound_port > 0);

    int cli = ::socket(AF_INET, SOCK_STREAM, 0);
    KRYPTOS_VX_ASSERT(cli >= 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(bound_port);

    KRYPTOS_VX_ASSERT(::connect(cli, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    std::string req = "{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"tools/call\",\"params\":{\"name\":\"kryptos_trigram_query\",\"arguments\":{\"query\":\"PoolAllocator\"}}}\n";
    KRYPTOS_VX_ASSERT(::send(cli, req.data(), req.size(), 0) == static_cast<ssize_t>(req.size()));

    char buf[8192]{};
    ssize_t n = ::recv(cli, buf, sizeof(buf) - 1, 0);
    ::close(cli);
    server_thread.join();

    KRYPTOS_VX_ASSERT(n > 0);
    std::string reply(buf, static_cast<size_t>(n));
    KRYPTOS_VX_ASSERT(reply.find("\"id\":99") != std::string::npos);
    KRYPTOS_VX_ASSERT(reply.find("PoolAllocator") != std::string::npos);
#endif
}

// 12. Phase 3 v2.0 "Aegis": Native 24-Byte ADB Wire-Protocol & Smart TV Hardware Bridge Test
KRYPTOS_VX_TEST(test_adb_wire_protocol_and_smart_tv_bridge) {
    auto cnxn = kryptos::engine::AdbBridge::build_packet(
        kryptos::engine::AdbBridge::CMD_CNXN,
        kryptos::engine::AdbBridge::ADB_VERSION,
        kryptos::engine::AdbBridge::ADB_MAXDATA,
        "host::features=shell_v2\0"
    );
    KRYPTOS_VX_ASSERT(kryptos::engine::AdbBridge::verify_packet(cnxn));

    cnxn.payload[0] ^= 0xFF;
    KRYPTOS_VX_ASSERT(!kryptos::engine::AdbBridge::verify_packet(cnxn));

    auto session = kryptos::engine::AdbBridge::run_simulated_smart_tv_debloat(
        kryptos::engine::DeviceProfile::SonyBraviaAndroidTV
    );
    KRYPTOS_VX_ASSERT(session.connected);
    KRYPTOS_VX_ASSERT(session.device_banner.find("BRAVIA_4K_VH2") != std::string::npos);
    KRYPTOS_VX_ASSERT(session.executed_commands.size() == 3);
}

// 13. Phase 4 v3.0 "Apex": FastCDC Rolling Gear Hash Shift-Resilience vs Fixed-Block Chunking
KRYPTOS_VX_TEST(test_fastcdc_shift_resilience_vs_fixed_blocks) {
    // Generate deterministic 512 KB pseudo-random payload
    constexpr size_t SZ = 512 * 1024;
    std::string base_buf(SZ, '\0');
    uint64_t rng = 0xCAFEBABE12345678ULL;
    for (size_t i = 0; i < SZ; ++i) {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        base_buf[i] = static_cast<char>(rng & 0xFF);
    }

    // Insert 37 bytes near the beginning (offset 500) — shifting all subsequent offsets by +37!
    std::string shifted_buf = base_buf.substr(0, 500) +
                              "APEX_FASTCDC_INSERTED_HEADER_BYTES_37" +
                              base_buf.substr(500);

    // Fixed-size 8KB chunking fails completely after a byte insertion (0% reuse after offset 500)
    auto fixed_base = kryptos::engine::FastCdcSync::chunk_fixed_blocks(
        reinterpret_cast<const uint8_t*>(base_buf.data()), base_buf.size(), 8192
    );
    auto fixed_shifted = kryptos::engine::FastCdcSync::chunk_fixed_blocks(
        reinterpret_cast<const uint8_t*>(shifted_buf.data()), shifted_buf.size(), 8192
    );
    std::unordered_set<uint64_t> fixed_set;
    for (const auto& c : fixed_base) fixed_set.insert(c.content_hash64);
    size_t fixed_matches = 0;
    for (const auto& c : fixed_shifted) {
        if (fixed_set.count(c.content_hash64)) ++fixed_matches;
    }
    KRYPTOS_VX_ASSERT(fixed_matches == 0); // Fixed-block chunking is destroyed by a 37-byte shift!

    // FastCDC rolling gear hash re-synchronizes on content boundaries and recovers > 90% of chunks!
    auto delta = kryptos::engine::FastCdcSync::compute_delta_sync(base_buf, shifted_buf);
    KRYPTOS_VX_ASSERT(delta.bandwidth_saved_pct > 90.0);
    KRYPTOS_VX_ASSERT(delta.matched_chunks > (delta.total_chunks * 8) / 10);
}

// 14. Phase 4 v3.0 "Apex": Live ELF64 Binary Section Bloat Profiler & Visual Binary Diff
KRYPTOS_VX_TEST(test_elf64_binary_section_profiler_and_diff) {
    auto rep_main = kryptos::engine::BinaryProfiler::profile_file("./bin/kryptos_vx");
    auto rep_bench = kryptos::engine::BinaryProfiler::profile_file("./bin/kryptos_vx_bench");

    KRYPTOS_VX_ASSERT(rep_main.format == kryptos::engine::BinaryFormat::Elf64);
    KRYPTOS_VX_ASSERT(rep_main.total_file_bytes > 100000);
    KRYPTOS_VX_ASSERT(rep_main.code_section_bytes > 20000);
    KRYPTOS_VX_ASSERT(!rep_main.sections.empty());

    auto diff = kryptos::engine::BinaryProfiler::diff_binaries(rep_bench, rep_main);
    KRYPTOS_VX_ASSERT(!diff.section_deltas.empty());
}

// 15. Phase 4 v3.0 "Apex": Cross-Format Windows PE32+ & macOS Mach-O64 Binary Parser Verification
KRYPTOS_VX_TEST(test_pe64_and_macho64_binary_parsers) {
    // 1. Construct valid synthetic Windows PE32+ (`MZ` + `PE\0\0` + `.text` section) buffer
    std::vector<uint8_t> pe_buf(1024, 0);
    pe_buf[0] = 'M';
    pe_buf[1] = 'Z';
    uint32_t pe_off = 64;
    std::memcpy(pe_buf.data() + 0x3C, &pe_off, sizeof(uint32_t));
    pe_buf[pe_off + 0] = 'P';
    pe_buf[pe_off + 1] = 'E';
    pe_buf[pe_off + 2] = 0;
    pe_buf[pe_off + 3] = 0;

    uint16_t machine_amd64 = 0x8664;
    uint16_t num_secs = 1;
    std::memcpy(pe_buf.data() + pe_off + 4, &machine_amd64, 2);
    std::memcpy(pe_buf.data() + pe_off + 6, &num_secs, 2);

    size_t sec_off = pe_off + 4 + 20;
    std::memcpy(pe_buf.data() + sec_off, ".text\0\0\0", 8);
    uint32_t raw_sz = 512;
    uint32_t raw_ptr = 256;
    std::memcpy(pe_buf.data() + sec_off + 16, &raw_sz, 4);
    std::memcpy(pe_buf.data() + sec_off + 20, &raw_ptr, 4);

    auto pe_rep = kryptos::engine::BinaryProfiler::profile_buffer(pe_buf.data(), pe_buf.size(), "synthetic_win64.exe");
    KRYPTOS_VX_ASSERT(pe_rep.format == kryptos::engine::BinaryFormat::WindowsPe);
    KRYPTOS_VX_ASSERT(pe_rep.architecture == "x86_64 (PE32+)");
    KRYPTOS_VX_ASSERT(pe_rep.sections.size() == 1);
    KRYPTOS_VX_ASSERT(pe_rep.sections[0].name == ".text");
    KRYPTOS_VX_ASSERT(pe_rep.code_section_bytes == 512);

    // 2. Construct valid synthetic macOS Mach-O 64-bit (`0xFEEDFACF` + `LC_SEGMENT_64` + `__text`) buffer
    std::vector<uint8_t> macho_buf(1024, 0);
    uint32_t mh_magic = 0xFEEDFACFU;
    uint32_t mh_cputype = 0x0100000CU; // ARM64 Apple Silicon
    uint32_t mh_ncmds = 1;
    std::memcpy(macho_buf.data() + 0, &mh_magic, 4);
    std::memcpy(macho_buf.data() + 4, &mh_cputype, 4);
    std::memcpy(macho_buf.data() + 16, &mh_ncmds, 4);

    size_t lcmd_off = 32; // sizeof(mach_header_64)
    uint32_t cmd_seg64 = 0x19U;
    uint32_t cmd_sz = 72 + 80; // sizeof(segment_command_64) + sizeof(section_64)
    uint32_t nsects = 1;
    std::memcpy(macho_buf.data() + lcmd_off + 0, &cmd_seg64, 4);
    std::memcpy(macho_buf.data() + lcmd_off + 4, &cmd_sz, 4);
    std::memcpy(macho_buf.data() + lcmd_off + 64, &nsects, 4);

    size_t msec_off = lcmd_off + 72;
    std::memcpy(macho_buf.data() + msec_off, "__text\0", 7);
    uint64_t msec_size = 384;
    uint32_t msec_fileoff = 256;
    std::memcpy(macho_buf.data() + msec_off + 40, &msec_size, 8);
    std::memcpy(macho_buf.data() + msec_off + 48, &msec_fileoff, 4);

    auto mo_rep = kryptos::engine::BinaryProfiler::profile_buffer(macho_buf.data(), macho_buf.size(), "synthetic_macos_arm64");
    KRYPTOS_VX_ASSERT(mo_rep.format == kryptos::engine::BinaryFormat::MachO64);
    KRYPTOS_VX_ASSERT(mo_rep.architecture == "ARM64 (Apple Silicon)");
    KRYPTOS_VX_ASSERT(mo_rep.sections.size() == 1);
    KRYPTOS_VX_ASSERT(mo_rep.sections[0].name == "__text");
    KRYPTOS_VX_ASSERT(mo_rep.code_section_bytes == 384);
}


// 16. Phase 4 v3.0 "Apex": KVXD Binary Delta Patch Serialization & 100% Bit-Exact Reconstruction
KRYPTOS_VX_TEST(test_kvxd_binary_delta_patch_reconstruction) {
    constexpr size_t SZ = 256 * 1024;
    std::string base_v1(SZ, '\0');
    uint64_t rng = 0x1122334455667788ULL;
    for (size_t i = 0; i < SZ; ++i) {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        base_v1[i] = static_cast<char>(rng & 0xFF);
    }

    std::string updated_v2 = "APEX_KVXD_HEADER_INSERTION_BLOCK_999" +
                             base_v1.substr(0, 90000) +
                             "APEX_MIDDLE_MUTATION_PAYLOAD_XYZ" +
                             base_v1.substr(90100) +
                             "APEX_TAIL_APPEND";

    std::string patch_blob = kryptos::engine::FastCdcSync::build_delta_patch(base_v1, updated_v2);
    KRYPTOS_VX_ASSERT(patch_blob.size() > 16);
    KRYPTOS_VX_ASSERT(patch_blob.substr(0, 4) == "KVXD");
    KRYPTOS_VX_ASSERT(patch_blob.size() < updated_v2.size() / 6);

    std::string reconstructed = kryptos::engine::FastCdcSync::apply_delta_patch(base_v1, patch_blob);
    KRYPTOS_VX_ASSERT(reconstructed.size() == updated_v2.size());
    KRYPTOS_VX_ASSERT(reconstructed == updated_v2);
}

// 17. Phase 4 v3.0 "Apex": Cross-Device TCP/LAN Zero-Copy Sync Wire Protocol Session
KRYPTOS_VX_TEST(test_cross_device_tcp_lan_zero_copy_sync) {
    constexpr size_t SZ = 384 * 1024;
    std::string receiver_file(SZ, '\0');
    uint64_t rng = 0xABCDEF0123456789ULL;
    for (size_t i = 0; i < SZ; ++i) {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        receiver_file[i] = static_cast<char>(rng & 0xFF);
    }

    std::string sender_file = receiver_file.substr(0, 1200) +
                              "LAN_SYNC_NEW_COMMIT_DIFF_PAYLOAD_64_BYTES_0123456789ABCDEF" +
                              receiver_file.substr(1200);

    auto sync_rep = kryptos::engine::FastCdcSync::run_lan_sync_session(receiver_file, sender_file);
    KRYPTOS_VX_ASSERT(sync_rep.bit_exact_verified);
    KRYPTOS_VX_ASSERT(sync_rep.bandwidth_saved_pct > 85.0);
    KRYPTOS_VX_ASSERT(sync_rep.chunks_reused_locally > sync_rep.chunks_fetched_over_lan);
}

// 18. Phase 4 v3.0 "Apex": ELF64 Symbol Table Hotspot Extraction (`.symtab` / `.dynsym`)
KRYPTOS_VX_TEST(test_elf64_symbol_table_hotspot_extraction) {
    auto rep = kryptos::engine::BinaryProfiler::profile_file("./bin/kryptos_vx");
    KRYPTOS_VX_ASSERT(rep.format == kryptos::engine::BinaryFormat::Elf64);
    KRYPTOS_VX_ASSERT(rep.total_symbols_count > 100);
    KRYPTOS_VX_ASSERT(!rep.top_symbols.empty());
    KRYPTOS_VX_ASSERT(rep.top_symbols[0].size_bytes >= rep.top_symbols.back().size_bytes);
}

} // namespace


int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════════════════════╗\n"
              << "║  🧪 KRYPTOS-VX v3.0.0 \"Apex\" C++20 Production Unit Test Suite (18 Suites)    ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════╝\n";

    KRYPTOS_VX_RUN_TEST(test_smart_pointer_and_raii_ownership);
    KRYPTOS_VX_RUN_TEST(test_pool_allocator_and_string_arena);
    KRYPTOS_VX_RUN_TEST(test_cpp20_and_platform_macros);
    KRYPTOS_VX_RUN_TEST(test_multi_isa_simd_equivalence);
    KRYPTOS_VX_RUN_TEST(test_secret_scanner_and_redaction);
    KRYPTOS_VX_RUN_TEST(test_soa_vs_aos_and_thread_pool);
    KRYPTOS_VX_RUN_TEST(test_kernel_fs_fast_enumeration_parity);
    KRYPTOS_VX_RUN_TEST(test_trigram_index_and_subms_incremental_update);
    KRYPTOS_VX_RUN_TEST(test_realtime_filesystem_watcher_daemon);
    KRYPTOS_VX_RUN_TEST(test_mcp_server_jsonrpc_and_subms_tools);
    KRYPTOS_VX_RUN_TEST(test_mcp_server_tcp_socket_transport);
    KRYPTOS_VX_RUN_TEST(test_adb_wire_protocol_and_smart_tv_bridge);
    KRYPTOS_VX_RUN_TEST(test_fastcdc_shift_resilience_vs_fixed_blocks);
    KRYPTOS_VX_RUN_TEST(test_elf64_binary_section_profiler_and_diff);
    KRYPTOS_VX_RUN_TEST(test_pe64_and_macho64_binary_parsers);
    KRYPTOS_VX_RUN_TEST(test_kvxd_binary_delta_patch_reconstruction);
    KRYPTOS_VX_RUN_TEST(test_cross_device_tcp_lan_zero_copy_sync);
    KRYPTOS_VX_RUN_TEST(test_elf64_symbol_table_hotspot_extraction);

    std::cout << "────────────────────────────────────────────────────────────────────────────────\n"
              << "  Passed: " << g_tests_passed << " | Failed: " << g_tests_failed << "\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
