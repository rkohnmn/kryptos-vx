// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v1.5 "Kronos" — SoA Parallel Directory Indexer Implementation
// Powered by KernelFs (`SYS_getdents64` + `statx` / Win32 NTFS USN + LargeFetch)
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/core/soa_index.hpp"
#include "kryptos/memory/raii_handle.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace kryptos::core {

void WorkspaceIndexSoA::reserve(size_t capacity) {
    size_bytes.reserve(capacity);
    content_hash.reserve(capacity);
    total_lines.reserve(capacity);
    code_lines.reserve(capacity);
    comment_lines.reserve(capacity);
    blank_lines.reserve(capacity);
    estimated_tokens.reserve(capacity);
    shannon_entropy.reserve(capacity);
    category.reserve(capacity);
    is_binary.reserve(capacity);
    relative_path.reserve(capacity);
    full_path.reserve(capacity);
    extension.reserve(capacity);
    language.reserve(capacity);
}

void WorkspaceIndexSoA::clear() noexcept {
    string_arena.reset();
    size_bytes.clear();
    content_hash.clear();
    total_lines.clear();
    code_lines.clear();
    comment_lines.clear();
    blank_lines.clear();
    estimated_tokens.clear();
    shannon_entropy.clear();
    category.clear();
    is_binary.clear();
    relative_path.clear();
    full_path.clear();
    extension.clear();
    language.clear();
    total_bytes_indexed = 0;
    total_lines_indexed = 0;
    total_tokens_indexed = 0;
    bloat_bytes_detected = 0;
    scan_duration_ms = 0.0;
    kernel_telemetry = KernelEnumTelemetry{};
    detected_secrets.clear();
}

uint64_t WorkspaceIndexSoA::sum_sizes_soa() const noexcept {
    uint64_t total = 0;
    const size_t n = size_bytes.size();
    const uint64_t* __restrict ptr = size_bytes.data();
    for (size_t i = 0; i < n; ++i) {
        total += ptr[i];
    }
    return total;
}

uint64_t WorkspaceIndexSoA::sum_tokens_soa() const noexcept {
    uint64_t total = 0;
    const size_t n = estimated_tokens.size();
    const uint32_t* __restrict ptr = estimated_tokens.data();
    for (size_t i = 0; i < n; ++i) {
        total += ptr[i];
    }
    return total;
}

std::string_view WorkspaceIndexSoA::classify_language(std::string_view ext) noexcept {
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".hpp" || ext == ".hh" || ext == ".h") return "C/C++";
    if (ext == ".rs") return "Rust";
    if (ext == ".py") return "Python";
    if (ext == ".ts" || ext == ".tsx") return "TypeScript";
    if (ext == ".js" || ext == ".jsx" || ext == ".mjs") return "JavaScript";
    if (ext == ".go") return "Go";
    if (ext == ".java") return "Java";
    if (ext == ".cs") return "C#";
    if (ext == ".md" || ext == ".markdown") return "Markdown";
    if (ext == ".svg") return "SVG Vector";
    if (ext == ".json" || ext == ".yaml" || ext == ".yml" || ext == ".toml" || ext == ".xml" || ext == ".desktop") return "Config/Data";
    if (ext == ".cmake" || ext == ".mk" || ext == ".sh" || ext == ".bat" || ext == ".ps1" || ext == ".rc") return "Build/Script";
    if (ext == ".sql") return "SQL";
    if (ext == ".css" || ext == ".scss") return "Stylesheet";
    return "Other/Binary";
}

FileCategory WorkspaceIndexSoA::classify_category(
    std::string_view rel_path,
    std::string_view ext,
    bool is_bin
) noexcept {
    if (rel_path.find("node_modules") != std::string_view::npos ||
        rel_path.find(".git/") != std::string_view::npos ||
        rel_path.find("__pycache__") != std::string_view::npos ||
        rel_path.find("Cache/") != std::string_view::npos ||
        rel_path.find("GPUCache") != std::string_view::npos ||
        rel_path.find("Code Cache") != std::string_view::npos ||
        rel_path.find(".gradle") != std::string_view::npos ||
        rel_path.find("target/debug") != std::string_view::npos ||
        ext == ".o" || ext == ".obj" || ext == ".pdb" || ext == ".log" || ext == ".tmp") {
        return FileCategory::BuildOrCacheBloat;
    }
    if (is_bin) {
        return FileCategory::MediaOrBinary;
    }
    const auto lang = classify_language(ext);
    if (lang == "C/C++" || lang == "Rust" || lang == "Python" || lang == "TypeScript" ||
        lang == "JavaScript" || lang == "Go" || lang == "Java" || lang == "C#" || lang == "Build/Script") {
        return FileCategory::SourceCode;
    }
    if (lang == "Markdown" || lang == "Config/Data" || lang == "SVG Vector") {
        return FileCategory::ConfigOrDoc;
    }
    return FileCategory::Other;
}

void WorkspaceIndexSoA::index_directory(
    const std::string& root_dir,
    bool include_bloat_dirs,
    size_t num_threads
) {
    const auto t0 = std::chrono::steady_clock::now();
    clear();

    // Phase 2 v1.5 "Kronos": Use kernel-level batched directory enumerator
    std::vector<KernelFileEntry> entries = KernelFs::enumerate_fast(
        root_dir, include_bloat_dirs, &kernel_telemetry
    );

    const size_t total_files = entries.size();
    reserve(total_files);

    size_bytes.resize(total_files, 0);
    content_hash.resize(total_files, 0);
    total_lines.resize(total_files, 0);
    code_lines.resize(total_files, 0);
    comment_lines.resize(total_files, 0);
    blank_lines.resize(total_files, 0);
    estimated_tokens.resize(total_files, 0);
    shannon_entropy.resize(total_files, 0.0);
    category.resize(total_files, FileCategory::Other);
    is_binary.resize(total_files, 0);
    relative_path.resize(total_files);
    full_path.resize(total_files);
    extension.resize(total_files);
    language.resize(total_files);

    for (size_t i = 0; i < total_files; ++i) {
        size_bytes[i] = entries[i].size_bytes;
        relative_path[i] = string_arena.intern(entries[i].relative_path);
        full_path[i] = string_arena.intern(entries[i].full_path);
        extension[i] = string_arena.intern(entries[i].extension);
        language[i] = classify_language(extension[i]);
    }

    const size_t workers = (num_threads == 0)
        ? std::max<size_t>(1, std::thread::hardware_concurrency())
        : num_threads;

    std::vector<CacheAlignedCounter> thread_bytes(workers);
    std::vector<CacheAlignedCounter> thread_lines(workers);
    std::vector<CacheAlignedCounter> thread_tokens(workers);
    std::vector<CacheAlignedCounter> thread_bloat(workers);
    std::mutex secrets_mutex;

    if (total_files > 0) {
        ThreadPool pool(workers);
        const size_t chunk_size = std::max<size_t>(1, (total_files + workers * 4 - 1) / (workers * 4));

        std::vector<std::future<void>> futures;
        for (size_t start = 0; start < total_files; start += chunk_size) {
            const size_t end = std::min<size_t>(total_files, start + chunk_size);
            const size_t worker_slot = (start / chunk_size) % workers;

            futures.push_back(pool.enqueue([this, &entries, start, end, worker_slot,
                                            &thread_bytes, &thread_lines, &thread_tokens,
                                            &thread_bloat, &secrets_mutex]() {
                std::vector<SecretMatch> local_secrets;
                for (size_t idx = start; idx < end; ++idx) {
                    const uint64_t fsz = entries[idx].size_bytes;
                    thread_bytes[worker_slot].add(fsz);

                    if (fsz > 0 && fsz <= (32ULL * 1024ULL * 1024ULL)) {
                        memory::MappedFile mf(entries[idx].full_path);
                        if (mf.data() && mf.size() > 0) {
                            const ScanMetrics sm = SimdScanner::analyze_buffer(mf.data(), mf.size());
                            content_hash[idx] = sm.content_hash;
                            total_lines[idx] = static_cast<uint32_t>(sm.total_lines);
                            code_lines[idx] = static_cast<uint32_t>(sm.code_lines);
                            comment_lines[idx] = static_cast<uint32_t>(sm.comment_lines);
                            blank_lines[idx] = static_cast<uint32_t>(sm.blank_lines);
                            estimated_tokens[idx] = static_cast<uint32_t>(sm.estimated_tokens);
                            shannon_entropy[idx] = sm.shannon_entropy;
                            is_binary[idx] = sm.is_binary ? 1 : 0;

                            thread_lines[worker_slot].add(sm.total_lines);
                            thread_tokens[worker_slot].add(sm.estimated_tokens);

                            if (!sm.is_binary && fsz <= (2ULL * 1024ULL * 1024ULL)) {
                                auto sec = SimdScanner::scan_secrets(mf.view());
                                for (auto& s : sec) {
                                    s.rule_name = std::string(relative_path[idx]) + ":" +
                                                  std::to_string(s.line_number) + " [" + s.rule_name + "]";
                                    local_secrets.push_back(std::move(s));
                                }
                            }
                        }
                    } else if (fsz > (32ULL * 1024ULL * 1024ULL)) {
                        is_binary[idx] = 1;
                    }

                    const auto cat = classify_category(relative_path[idx], extension[idx], is_binary[idx] != 0);
                    category[idx] = cat;
                    if (cat == FileCategory::BuildOrCacheBloat) {
                        thread_bloat[worker_slot].add(fsz);
                    }
                }
                if (!local_secrets.empty()) {
                    std::lock_guard<std::mutex> lock(secrets_mutex);
                    for (auto& s : local_secrets) {
                        detected_secrets.push_back(std::move(s));
                    }
                }
            }));
        }
        for (auto& f : futures) {
            f.get();
        }
    }

    for (size_t w = 0; w < workers; ++w) {
        total_bytes_indexed += thread_bytes[w].load();
        total_lines_indexed += thread_lines[w].load();
        total_tokens_indexed += thread_tokens[w].load();
        bloat_bytes_detected += thread_bloat[w].load();
    }

    const auto t1 = std::chrono::steady_clock::now();
    scan_duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

} // namespace kryptos::core
