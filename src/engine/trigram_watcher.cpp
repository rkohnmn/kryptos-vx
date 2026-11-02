// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX v1.5 "Kronos" — In-Memory Trigram Index & OS Watcher Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "kryptos/engine/trigram_watcher.hpp"
#include "kryptos/core/simd_scanner.hpp"
#include "kryptos/memory/raii_handle.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <array>
#include <thread>

#if defined(__linux__)
    #include <poll.h>
    #include <sys/inotify.h>
    #include <unistd.h>
#elif defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace fs = std::filesystem;

namespace kryptos::engine {

std::vector<uint32_t> TrigramWatcherEngine::extract_unique_trigrams(std::string_view text) {
    std::vector<uint32_t> tris;
    if (text.size() < 3) return tris;

    tris.reserve(std::min<size_t>(text.size(), 16384));
    const auto* ptr = reinterpret_cast<const uint8_t*>(text.data());
    const size_t limit = text.size() - 3;

    for (size_t i = 0; i <= limit; ++i) {
        tris.push_back(pack_trigram(ptr[i], ptr[i + 1], ptr[i + 2]));
    }

    std::sort(tris.begin(), tris.end());
    tris.erase(std::unique(tris.begin(), tris.end()), tris.end());
    return tris;
}

void TrigramWatcherEngine::build_from_workspace(const core::WorkspaceIndexSoA& index) {
    const auto t0 = std::chrono::steady_clock::now();
    docs_.clear();
    path_to_doc_id_.clear();
    posting_lists_.clear();
    total_trigrams_stored_ = 0;

    docs_.reserve(index.size());
    posting_lists_.reserve(16384);

    for (size_t i = 0; i < index.size(); ++i) {
        if (index.is_binary[i] != 0) continue;
        if (index.size_bytes[i] == 0 || index.size_bytes[i] > (4ULL * 1024ULL * 1024ULL)) continue;

        const std::string full_p(index.full_path[i]);
        const std::string rel_p(index.relative_path[i]);
        memory::MappedFile mf(full_p);
        if (!mf.data() || mf.empty()) continue;

        const uint32_t doc_id = static_cast<uint32_t>(docs_.size());
        std::string content_copy(mf.view());
        auto tris = extract_unique_trigrams(content_copy);

        for (uint32_t tri : tris) {
            posting_lists_[tri].push_back(doc_id);
        }
        total_trigrams_stored_ += tris.size();

        path_to_doc_id_[rel_p] = doc_id;
        docs_.push_back(IndexedDoc{
            full_p,
            rel_p,
            std::move(content_copy),
            index.content_hash[i],
            std::move(tris),
            true
        });
    }

    const auto t1 = std::chrono::steady_clock::now();
    initial_build_ms_ = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

IncrementalUpdateMetrics TrigramWatcherEngine::upsert_file(
    const std::string& full_path,
    const std::string& relative_path,
    std::string_view event_type
) {
    const auto t0 = std::chrono::steady_clock::now();
    IncrementalUpdateMetrics metrics{relative_path, std::string(event_type), 0, 0.0};

    memory::MappedFile mf(full_path);
    std::string new_content = (!mf.empty() && mf.data()) ? std::string(mf.view()) : std::string{};
    const uint64_t new_hash = core::SimdScanner::fast_hash64(
        reinterpret_cast<const uint8_t*>(new_content.data()), new_content.size()
    );

    auto it = path_to_doc_id_.find(relative_path);
    if (it != path_to_doc_id_.end()) {
        const uint32_t doc_id = it->second;
        IndexedDoc& doc = docs_[doc_id];
        if (doc.active && doc.content_hash == new_hash) {
            metrics.trigrams_indexed = doc.unique_trigrams.size();
            const auto t1 = std::chrono::steady_clock::now();
            metrics.update_latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            return metrics;
        }

        // Remove old trigrams for doc_id
        for (uint32_t old_tri : doc.unique_trigrams) {
            auto pit = posting_lists_.find(old_tri);
            if (pit != posting_lists_.end()) {
                auto& vec = pit->second;
                auto pos = std::lower_bound(vec.begin(), vec.end(), doc_id);
                if (pos != vec.end() && *pos == doc_id) {
                    vec.erase(pos);
                    if (total_trigrams_stored_ > 0) --total_trigrams_stored_;
                }
            }
        }

        auto new_tris = extract_unique_trigrams(new_content);
        for (uint32_t tri : new_tris) {
            auto& vec = posting_lists_[tri];
            auto pos = std::lower_bound(vec.begin(), vec.end(), doc_id);
            if (pos == vec.end() || *pos != doc_id) {
                vec.insert(pos, doc_id);
                ++total_trigrams_stored_;
            }
        }

        metrics.trigrams_indexed = new_tris.size();
        doc.full_path = full_path;
        doc.content = std::move(new_content);
        doc.content_hash = new_hash;
        doc.unique_trigrams = std::move(new_tris);
        doc.active = true;
    } else {
        const uint32_t doc_id = static_cast<uint32_t>(docs_.size());
        auto new_tris = extract_unique_trigrams(new_content);
        for (uint32_t tri : new_tris) {
            posting_lists_[tri].push_back(doc_id);
        }
        total_trigrams_stored_ += new_tris.size();
        metrics.trigrams_indexed = new_tris.size();

        path_to_doc_id_[relative_path] = doc_id;
        docs_.push_back(IndexedDoc{
            full_path,
            relative_path,
            std::move(new_content),
            new_hash,
            std::move(new_tris),
            true
        });
    }

    const auto t1 = std::chrono::steady_clock::now();
    metrics.update_latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return metrics;
}

IncrementalUpdateMetrics TrigramWatcherEngine::remove_file(
    const std::string& relative_path
) {
    const auto t0 = std::chrono::steady_clock::now();
    IncrementalUpdateMetrics metrics{relative_path, "DELETED", 0, 0.0};

    auto it = path_to_doc_id_.find(relative_path);
    if (it != path_to_doc_id_.end()) {
        const uint32_t doc_id = it->second;
        IndexedDoc& doc = docs_[doc_id];
        if (doc.active) {
            for (uint32_t old_tri : doc.unique_trigrams) {
                auto pit = posting_lists_.find(old_tri);
                if (pit != posting_lists_.end()) {
                    auto& vec = pit->second;
                    auto pos = std::lower_bound(vec.begin(), vec.end(), doc_id);
                    if (pos != vec.end() && *pos == doc_id) {
                        vec.erase(pos);
                        if (total_trigrams_stored_ > 0) --total_trigrams_stored_;
                    }
                }
            }
            doc.unique_trigrams.clear();
            doc.content.clear();
            doc.active = false;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    metrics.update_latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return metrics;
}

TrigramQueryResult TrigramWatcherEngine::query(std::string_view needle) const {
    const auto t0 = std::chrono::steady_clock::now();
    TrigramQueryResult res{};
    res.total_files_in_index = docs_.size();

    if (needle.empty()) return res;

    std::vector<uint32_t> candidates;
    if (needle.size() >= 3) {
        auto query_tris = extract_unique_trigrams(needle);
        // Sort query trigrams by posting list frequency (rarest trigram first for fastest intersection)
        std::vector<const std::vector<uint32_t>*> lists;
        lists.reserve(query_tris.size());

        for (uint32_t tri : query_tris) {
            auto it = posting_lists_.find(tri);
            if (it == posting_lists_.end() || it->second.empty()) {
                // Zero files contain this required trigram — instant O(1) short-circuit!
                const auto t1 = std::chrono::steady_clock::now();
                res.query_latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                return res;
            }
            lists.push_back(&it->second);
        }

        std::sort(lists.begin(), lists.end(), [](const auto* a, const auto* b) {
            return a->size() < b->size();
        });

        candidates = *lists[0];
        std::vector<uint32_t> next_intersection;
        next_intersection.reserve(candidates.size());

        for (size_t i = 1; i < lists.size() && !candidates.empty(); ++i) {
            next_intersection.clear();
            std::set_intersection(
                candidates.begin(), candidates.end(),
                lists[i]->begin(), lists[i]->end(),
                std::back_inserter(next_intersection)
            );
            candidates.swap(next_intersection);
        }
    } else {
        candidates.reserve(docs_.size());
        for (uint32_t id = 0; id < docs_.size(); ++id) {
            if (docs_[id].active) candidates.push_back(id);
        }
    }

    res.candidate_files_evaluated = candidates.size();

    // Verify exact substring matches on pruned candidate set using Multi-ISA SIMD
    for (uint32_t doc_id : candidates) {
        const auto& doc = docs_[doc_id];
        if (!doc.active || doc.content.empty()) continue;

        auto offsets = core::SimdScanner::find_all_simd(doc.content, needle);
        if (!offsets.empty()) {
            TrigramQueryResult::FileHit hit{};
            hit.relative_path = doc.relative_path;
            hit.byte_offsets = std::move(offsets);
            hit.line_numbers.reserve(hit.byte_offsets.size());

            const auto* raw = reinterpret_cast<const uint8_t*>(doc.content.data());
            for (size_t off : hit.byte_offsets) {
                hit.line_numbers.push_back(1 + core::SimdScanner::count_byte_simd(raw, off, '\n'));
            }
            res.total_matches += hit.byte_offsets.size();
            res.hits.push_back(std::move(hit));
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    res.query_latency_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return res;
}

int TrigramWatcherEngine::run_watcher_daemon(
    const std::string& root_dir,
    uint32_t max_events,
    uint32_t timeout_ms,
    bool use_color,
    WatcherTelemetry* telemetry,
    std::function<void()> on_ready
) {
    WatcherTelemetry local;
    auto& report = telemetry ? *telemetry : local;
    report = {};
    std::error_code root_error;
    if (!fs::is_directory(root_dir, root_error) || root_error) return 1;
    const char* cy  = use_color ? "\033[38;5;51m" : "";
    const char* gr  = use_color ? "\033[38;5;48m" : "";
    const char* b   = use_color ? "\033[1m"       : "";
    const char* rst = use_color ? "\033[0m"       : "";

    std::cout << cy << b
              << "╔══════════════════════════════════════════════════════════════════════════════╗\n"
              << "║  👁️  KRYPTOS-VX v1.5 \"Kronos\" — Real-Time Trigram Watcher Daemon             ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════╝"
              << rst << "\n";
    std::cout << "  • Indexed Documents   : " << gr << indexed_doc_count() << rst << "\n"
              << "  • Unique 24-Bit Tris  : " << gr << unique_trigram_count() << rst
              << " (" << total_postings_count() << " postings)\n"
              << "  • Initial Build Time  : " << cy << std::fixed << std::setprecision(2)
              << initial_build_ms() << " ms" << rst << "\n";

#if defined(__linux__)
    report.backend = "Linux inotify";
    int inotify_fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) {
        std::cerr << "  [Warn] Failed to initialize Linux inotify descriptor.\n";
        return 1;
    }

    std::error_code ec;
    const fs::path base_path = fs::canonical(root_dir, ec);
    const fs::path root_p = ec ? fs::path(root_dir) : base_path;

    std::unordered_map<int, fs::path> wd_to_path;
    constexpr uint32_t WATCH_MASK = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_CLOSE_WRITE;

    int root_wd = ::inotify_add_watch(inotify_fd, root_p.c_str(), WATCH_MASK);
    if (root_wd < 0) { ::close(inotify_fd); return 1; }
    wd_to_path[root_wd] = root_p;

    for (auto it = fs::recursive_directory_iterator(root_p, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it->is_directory(ec)) {
            const std::string dn = it->path().filename().string();
            if (dn == ".git" || dn == "node_modules" || dn == "build") {
                it.disable_recursion_pending();
                continue;
            }
            int wd = ::inotify_add_watch(inotify_fd, it->path().c_str(), WATCH_MASK);
            if (wd >= 0) {
                wd_to_path[wd] = it->path();
            }
        }
    }

    std::cout << "  • OS Event Backend    : " << b << "Linux Kernel `inotify` ("
              << wd_to_path.size() << " directories watched)" << rst << "\n"
              << "  • Incremental Target  : " << gr << "< 1.00 ms per file update" << rst << "\n\n";

    struct pollfd pfd{};
    pfd.fd = inotify_fd;
    pfd.events = POLLIN;

    if (on_ready) on_ready();
    uint32_t processed = 0;
    alignas(struct inotify_event) char buf[8192];

    const auto start_wait = std::chrono::steady_clock::now();
    while (max_events == 0 || processed < max_events) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_wait
        ).count();
        if (elapsed >= static_cast<long long>(timeout_ms)) { report.timed_out = true; break; }

        int remain_ms = static_cast<int>(timeout_ms - elapsed);
        int pret = ::poll(&pfd, 1, std::min(remain_ms, 200));
        if (pret <= 0) continue;

        ssize_t len = ::read(inotify_fd, buf, sizeof(buf));
        if (len <= 0) continue;

        for (char* ptr = buf; ptr < buf + len;) {
            const auto* ev = reinterpret_cast<const struct inotify_event*>(ptr);
            ptr += sizeof(struct inotify_event) + ev->len;

            if (ev->len == 0 || (ev->mask & IN_ISDIR)) continue;
            auto dir_it = wd_to_path.find(ev->wd);
            if (dir_it == wd_to_path.end()) continue;

            fs::path full_f = dir_it->second / ev->name;
            std::string rel_f = fs::relative(full_f, root_p, ec).generic_string();
            if (ec || rel_f.empty()) rel_f = ev->name;

            IncrementalUpdateMetrics m{};
            if (ev->mask & IN_DELETE) {
                m = remove_file(rel_f);
            } else {
                m = upsert_file(full_f.generic_string(), rel_f,
                                (ev->mask & IN_CREATE) ? "CREATED" : "MODIFIED");
            }
            ++processed;
            report.events_processed = processed;
            std::cout << "  [" << gr << m.event_type << rst << "] `" << m.relative_path << "` -> "
                      << m.trigrams_indexed << " trigrams updated in "
                      << cy << std::fixed << std::setprecision(3) << m.update_latency_ms << " ms" << rst << "\n";
            if (max_events != 0 && processed >= max_events) break;
        }
    }

    ::close(inotify_fd);
    return 0;
#elif defined(_WIN32)
    report.backend = "Windows ReadDirectoryChangesW";
    std::array<DWORD, 16384> buffer{}; // DWORD alignment required by Win32
    struct DirectoryWatch {
        HANDLE directory{INVALID_HANDLE_VALUE};
        HANDLE event{nullptr};
        OVERLAPPED overlap{};
        bool pending{false};
        ~DirectoryWatch() {
            if (pending) {
                CancelIoEx(directory, &overlap);
                DWORD ignored = 0;
                GetOverlappedResult(directory, &overlap, &ignored, TRUE);
            }
            if (event) CloseHandle(event);
            if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
        }
    } watch;
    const fs::path base = fs::absolute(root_dir);
    watch.directory = CreateFileW(base.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (watch.directory == INVALID_HANDLE_VALUE) return 1;
    watch.event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!watch.event) return 1;
    watch.overlap.hEvent = watch.event;
    bool announced = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (max_events == 0 || report.events_processed < max_events) {
        ResetEvent(watch.event);
        if (!ReadDirectoryChangesW(watch.directory, buffer.data(), static_cast<DWORD>(sizeof(buffer)), TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
            nullptr, &watch.overlap, nullptr)) return 1;
        watch.pending = true;
        if (!announced) { announced = true; if (on_ready) on_ready(); }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) { report.timed_out = true; break; }
        const DWORD wait = WaitForSingleObject(watch.event, static_cast<DWORD>(remaining));
        if (wait == WAIT_TIMEOUT) { report.timed_out = true; break; }
        if (wait != WAIT_OBJECT_0) return 1;
        DWORD bytes = 0;
        if (!GetOverlappedResult(watch.directory, &watch.overlap, &bytes, FALSE)) return 1;
        watch.pending = false;
        if (bytes == 0) return 1; // overflow: never claim the index is current
        size_t offset = 0;
        while (offset + offsetof(FILE_NOTIFY_INFORMATION, FileName) <= bytes) {
            const auto* entry = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(reinterpret_cast<const char*>(buffer.data()) + offset);
            if (entry->FileNameLength % sizeof(wchar_t) != 0 ||
                entry->FileNameLength > bytes - offset - offsetof(FILE_NOTIFY_INFORMATION, FileName)) return 1;
            const fs::path relative(std::wstring(entry->FileName, entry->FileNameLength / sizeof(wchar_t)));
            const std::string key = relative.generic_string();
            if (entry->Action == FILE_ACTION_REMOVED || entry->Action == FILE_ACTION_RENAMED_OLD_NAME) {
                remove_file(key);
                ++report.events_processed;
            } else {
                std::error_code ec;
                const auto full = base / relative;
                if (fs::is_regular_file(full, ec) && !ec) {
                    upsert_file(full.string(), key, entry->Action == FILE_ACTION_ADDED ? "CREATED" : "MODIFIED");
                    ++report.events_processed;
                }
            }
            if (max_events != 0 && report.events_processed >= max_events) break;
            if (entry->NextEntryOffset == 0) break;
            if (entry->NextEntryOffset < offsetof(FILE_NOTIFY_INFORMATION, FileName) || entry->NextEntryOffset > bytes - offset) return 1;
            offset += entry->NextEntryOffset;
        }
    }
    return 0;
#else
    report.backend = "Portable metadata polling (50 ms)";
    using Stamp = std::pair<fs::file_time_type, uintmax_t>;
    using Snapshot = std::unordered_map<std::string, Stamp>;
    const auto base = fs::absolute(root_dir);
    auto scan = [&](Snapshot& snapshot) {
        std::error_code ec;
        auto it = fs::recursive_directory_iterator(base, ec);
        if (ec) return false;
        for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) return false;
            if (it->is_symlink(ec)) { it.disable_recursion_pending(); continue; }
            if (it->is_directory(ec)) {
                const auto name = it->path().filename().string();
                if (name == ".git" || name == "build" || name == "node_modules") it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            const auto modified = it->last_write_time(ec);
            if (ec) return false;
            const auto size = it->file_size(ec);
            if (ec) return false;
            snapshot[it->path().lexically_relative(base).generic_string()] = {modified, size};
        }
        return !ec;
    };
    Snapshot previous;
    if (!scan(previous)) return 1;
    if (on_ready) on_ready();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_until(std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(50)));
        Snapshot current;
        if (!scan(current)) return 1;
        for (const auto& [path, stamp] : current) {
            const auto old = previous.find(path);
            if (old == previous.end() || old->second != stamp) {
                upsert_file((base / path).string(), path, old == previous.end() ? "CREATED" : "MODIFIED");
                ++report.events_processed;
                if (max_events != 0 && report.events_processed >= max_events) return 0;
            }
        }
        for (const auto& [path, stamp] : previous) {
            (void)stamp;
            if (!current.contains(path)) {
                remove_file(path);
                ++report.events_processed;
                if (max_events != 0 && report.events_processed >= max_events) return 0;
            }
        }
        previous = std::move(current);
    }
    report.timed_out = true;
    return 0;
#endif
}

} // namespace kryptos::engine
