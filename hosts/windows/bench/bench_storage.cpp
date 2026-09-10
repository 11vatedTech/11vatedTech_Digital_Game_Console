// bench_storage.cpp — storage qualification (DK0-M1D; docs/benchmarks.md).
// Read-oriented (game workload). Unbuffered IO so Windows cache cannot
// invalidate results. 1 GiB temp benchmark file, RAII-deleted on all paths.
#include "bench_common.hpp"
#include "dc/qualification.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace dcwin {

namespace {

// RAII temp benchmark file: 1 GiB of deterministic pseudo-random content.
class TempBenchFile {
public:
    explicit TempBenchFile(const std::string& dir) {
        path_ = dir + "\\dc-bench-1g.tmp";
        HANDLE h = CreateFileA(path_.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { ok_ = false; return; }
        const DWORD chunk = 4u << 20; // 4 MiB write chunks
        std::vector<unsigned char> buf(chunk);
        BenchRng rng(0xDCBE4C11ULL); // "DC-BENCH" seed (valid hex)
        bool write_ok = true;
        for (uint64_t off = 0; off < kFileSize && write_ok; off += chunk) {
            for (size_t i = 0; i < chunk; ++i) buf[i] = static_cast<unsigned char>(rng.Next() >> 56);
            DWORD written = 0;
            if (!WriteFile(h, buf.data(), chunk, &written, nullptr) || written != chunk) write_ok = false;
        }
        CloseHandle(h);
        ok_ = write_ok;
    }
    ~TempBenchFile() {
        if (!path_.empty()) std::filesystem::remove(path_, ec_);
    }
    bool ok() const { return ok_; }
    const std::string& path() const { return path_; }
    static constexpr uint64_t kFileSize = 1ull << 30; // 1 GiB

private:
    std::string path_;
    bool ok_ = false;
    std::error_code ec_;
};

// Aligned buffer for unbuffered IO (sector alignment required).
struct AlignedBuf {
    explicit AlignedBuf(size_t bytes, size_t align = 4096)
        : p_(static_cast<unsigned char*>(_aligned_malloc(bytes, align))) {}
    ~AlignedBuf() { if (p_) _aligned_free(p_); }
    AlignedBuf(const AlignedBuf&) = delete;
    AlignedBuf& operator=(const AlignedBuf&) = delete;
    unsigned char* get() { return p_; }
private:
    unsigned char* p_ = nullptr;
};

double Percentile(std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
    if (idx >= v.size()) idx = v.size() - 1;
    return v[idx];
}

} // namespace

void BenchStorageSeq(const BenchIdentity& id, BenchCancel& cancel,
                     BenchmarkRecord& rec) {
    StampRecord(rec, id, "storage.seq", 1, 0, "dc-bench/1:storage-seq");

    // Locate a writable temp dir on the fixed drive with most free space.
    char temp_dir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp_dir) == 0) {
        rec.state = BenchState::Unavailable; rec.abort_reason = "no-temp-dir"; return;
    }
    TempBenchFile file(temp_dir);
    if (!file.ok()) { rec.state = BenchState::Aborted; rec.abort_reason = "temp-file-create-failed"; return; }

    HANDLE h = CreateFileA(file.path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
    if (h == INVALID_HANDLE_VALUE) { rec.state = BenchState::Aborted; rec.abort_reason = "open-failed"; return; }

    const DWORD chunk = 1u << 20; // 1 MiB reads
    AlignedBuf buf(chunk);
    if (!buf.get()) { CloseHandle(h); rec.state = BenchState::Aborted; rec.abort_reason = "alloc-failed"; return; }

    std::vector<BenchSample> samples;
    std::vector<double> latencies_us; // per-1MiB-op latency
    OVERLAPPED ov{};
    auto read_pass = [&](uint64_t bytes, std::vector<double>* lat) -> bool {
        uint64_t off = 0;
        while (off < bytes) {
            if (cancel.requested || cancel.Expired(QpcMs())) return false;
            ov.Offset = static_cast<DWORD>(off & 0xFFFFFFFFull);
            ov.OffsetHigh = static_cast<DWORD>(off >> 32);
            ov.hEvent = nullptr;
            double t0 = QpcMs();
            DWORD got = 0;
            if (!ReadFile(h, buf.get(), chunk, &got, &ov) || got != chunk) return false;
            double dt_us = (QpcMs() - t0) * 1000.0;
            if (lat) lat->push_back(dt_us);
            off += chunk;
        }
        return true;
    };

    // Warmup: one full buffered pre-read pass to stabilize read-ahead, then
    // switch to unbuffered timing (docs/benchmarks.md rule 2).
    {
        HANDLE hw = CreateFileA(file.path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hw != INVALID_HANDLE_VALUE) {
            std::vector<char> wbuf(4u << 20);
            while (true) {
                DWORD got = 0;
                if (!ReadFile(hw, wbuf.data(), static_cast<DWORD>(wbuf.size()), &got, nullptr) || got == 0) break;
            }
            CloseHandle(hw);
        }
    }

    const uint64_t pass_bytes = 512u << 20; // 512 MiB per timed pass
    for (int s = 0; s < 3; ++s) {
        if (cancel.requested) { CloseHandle(h); rec.state = BenchState::Aborted; rec.abort_reason = "cancelled"; return; }
        LARGE_INTEGER start{}, end{};
        QueryPerformanceCounter(&start);
        std::vector<double> pass_lat;
        if (!read_pass(pass_bytes, &pass_lat)) {
            CloseHandle(h);
            rec.state = BenchState::Aborted; rec.abort_reason = "read-failed";
            return;
        }
        QueryPerformanceCounter(&end);
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        double sec = static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(freq.QuadPart);
        double mbps = (static_cast<double>(pass_bytes) / (1024.0 * 1024.0)) / sec;
        samples.push_back({mbps, sec * 1000.0});
        for (double us : pass_lat) latencies_us.push_back(us);
    }
    CloseHandle(h);

    // Score = MB/s mean of per-pass rates (use direct mean, not FinalizeRecord
    // unit math). Latency percentiles ride on the record (docs table).
    double sum = 0.0;
    for (const auto& s : samples) sum += s.gpu_time_ms;
    rec.score = sum / static_cast<double>(samples.size());
    rec.variance = Percentile(latencies_us, 0.95); // carried: p95 latency (us)
    // p50/p99 in elapsed_gpu/elapsed_cpu per qualification.cpp mapping.
    rec.elapsed_gpu_time_ms = Percentile(latencies_us, 0.50);
    rec.elapsed_cpu_time_ms = Percentile(latencies_us, 0.99);
    rec.score_ratio = 1.0; // queue depth 1 for seq
    rec.state = BenchState::Complete;
}

void BenchStorageRandom(const BenchIdentity& id, BenchCancel& cancel,
                        BenchmarkRecord& rec, uint32_t queue_depth) {
    StampRecord(rec, id, "storage.random", 1, 0, "dc-bench/1:storage-random");

    char temp_dir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp_dir) == 0) {
        rec.state = BenchState::Unavailable; rec.abort_reason = "no-temp-dir"; return;
    }
    TempBenchFile file(temp_dir);
    if (!file.ok()) { rec.state = BenchState::Aborted; rec.abort_reason = "temp-file-create-failed"; return; }

    HANDLE h = CreateFileA(file.path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
    if (h == INVALID_HANDLE_VALUE) { rec.state = BenchState::Aborted; rec.abort_reason = "open-failed"; return; }

    const DWORD kio = 4096;
    AlignedBuf buf(static_cast<size_t>(kio) * queue_depth);
    if (!buf.get()) { CloseHandle(h); rec.state = BenchState::Aborted; rec.abort_reason = "alloc-failed"; return; }

    // Fixed duration: 4 seconds (cache-cold behavior does not improve with
    // more samples; docs rule 5 discovery benches are duration-independent).
    const double duration_ms = 4000.0;
    std::vector<OVERLAPPED> ovs(queue_depth);
    BenchRng rng(0xD3ADBEEFULL);
    const uint64_t max_off = TempBenchFile::kFileSize - kio;

    std::vector<double> latencies_us;
    uint64_t ops = 0;
    double t_start = QpcMs();
    // Simple in-order completion loop: submit QD outstanding, await one, record.
    // (Overlapped IO via ReadFile + OVERLAPPED without event => GetOverlappedResult.)
    struct Pend { OVERLAPPED* ov; double t0; };
    std::vector<Pend> pending;
    pending.reserve(queue_depth);
    // Offsets MUST be 4096-aligned for FILE_FLAG_NO_BUFFERING (sector rule).
    auto aligned_offset = [&]() -> uint64_t {
        return (rng.Next() % (max_off / kio)) * kio;
    };
    uint64_t next_off = aligned_offset();
    for (uint32_t q = 0; q < queue_depth; ++q) {
        OVERLAPPED& ov = ovs[q];
        ZeroMemory(&ov, sizeof(ov));
        ov.Offset = static_cast<DWORD>(next_off & 0xFFFFFFFFull);
        ov.OffsetHigh = static_cast<DWORD>(next_off >> 32);
        next_off = aligned_offset();
        DWORD got = 0;
        double t0 = QpcMs();
        if (!ReadFile(h, buf.get() + q * kio, kio, &got, &ov)) {
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) { CloseHandle(h); rec.state = BenchState::Aborted; rec.abort_reason = "read-failed"; return; }
        }
        pending.push_back({&ov, t0});
    }
    while (QpcMs() - t_start < duration_ms) {
        if (cancel.requested) break;
        if (pending.empty()) break;
        Pend p = pending.front();
        DWORD got = 0;
        if (!GetOverlappedResult(h, p.ov, &got, TRUE)) {
            CloseHandle(h);
            rec.state = BenchState::Aborted; rec.abort_reason = "read-failed";
            return;
        }
        double done_t = QpcMs();
        latencies_us.push_back((done_t - p.t0) * 1000.0);
        ++ops;
        pending.erase(pending.begin());
        // resubmit
        OVERLAPPED& ov = *p.ov;
        ZeroMemory(&ov, sizeof(ov));
        ov.Offset = static_cast<DWORD>(next_off & 0xFFFFFFFFull);
        ov.OffsetHigh = static_cast<DWORD>(next_off >> 32);
        next_off = aligned_offset();
        DWORD got2 = 0;
        double t0 = QpcMs();
        if (!ReadFile(h, buf.get() + (ops % queue_depth) * kio, kio, &got2, &ov)) {
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) { CloseHandle(h); rec.state = BenchState::Aborted; rec.abort_reason = "read-failed"; return; }
        }
        pending.push_back({&ov, t0});
    }
    // Drain outstanding.
    for (auto& p : pending) {
        DWORD got = 0;
        GetOverlappedResult(h, p.ov, &got, TRUE);
        latencies_us.push_back((QpcMs() - p.t0) * 1000.0);
        ++ops;
    }
    CloseHandle(h);
    CancelIo(h);

    double elapsed_s = (QpcMs() - t_start) / 1000.0;
    if (ops < 10) { rec.state = BenchState::Aborted; rec.abort_reason = "insufficient-ops"; return; }
    rec.score = static_cast<double>(ops) / elapsed_s / 1000.0; // kIOPS
    rec.variance = Percentile(latencies_us, 0.95);
    rec.elapsed_gpu_time_ms = Percentile(latencies_us, 0.50);
    rec.elapsed_cpu_time_ms = Percentile(latencies_us, 0.99);
    rec.score_ratio = static_cast<double>(queue_depth);
    rec.state = BenchState::Complete;
}

void BenchStorageSustained(const BenchIdentity& id, BenchCancel& cancel,
                           BenchmarkRecord& rec, const SustainedConfig& cfg) {
    StampRecord(rec, id, "storage.sustained", 1, 0, "dc-bench/1:storage-sustained");
    char temp_dir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp_dir) == 0) {
        rec.state = BenchState::Unavailable; rec.abort_reason = "no-temp-dir"; return;
    }
    TempBenchFile file(temp_dir);
    if (!file.ok()) { rec.state = BenchState::Aborted; rec.abort_reason = "temp-file-create-failed"; return; }
    HANDLE h = CreateFileA(file.path().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
    if (h == INVALID_HANDLE_VALUE) { rec.state = BenchState::Aborted; rec.abort_reason = "open-failed"; return; }
    const DWORD chunk = 1u << 20;
    AlignedBuf buf(chunk);
    if (!buf.get()) { CloseHandle(h); rec.state = BenchState::Aborted; rec.abort_reason = "alloc-failed"; return; }

    // Sustained = repeated full-file sequential passes for the window;
    // score = mean MB/s across cfg.samples window samples.
    std::vector<double> rates;
    OVERLAPPED ov{};
    for (uint32_t s = 0; s < cfg.samples; ++s) {
        double win_start = QpcMs();
        uint64_t bytes = 0;
        LARGE_INTEGER size{};
        GetFileSizeEx(h, &size);
        LARGE_INTEGER zero{}; SetFilePointerEx(h, zero, nullptr, FILE_BEGIN);
        while (bytes < static_cast<uint64_t>(size.QuadPart)) {
            if (cancel.requested || QpcMs() - win_start > cfg.window_ms) break;
            ov.Offset = static_cast<DWORD>(bytes & 0xFFFFFFFFull);
            ov.OffsetHigh = static_cast<DWORD>(bytes >> 32);
            ov.hEvent = nullptr;
            DWORD got = 0;
            if (!ReadFile(h, buf.get(), chunk, &got, &ov) || got == 0) break;
            bytes += got;
        }
        double sec = (QpcMs() - win_start) / 1000.0;
        if (sec > 0.0 && bytes > 0) {
            rates.push_back((static_cast<double>(bytes) / (1024.0 * 1024.0)) / sec);
        }
        if (cancel.requested || cancel.Expired(QpcMs())) break;
    }
    CloseHandle(h);
    if (rates.empty()) { rec.state = BenchState::Aborted; rec.abort_reason = "no-data"; return; }
    double mean = 0.0;
    for (double r : rates) mean += r;
    mean /= static_cast<double>(rates.size());
    rec.score = mean;
    rec.state = BenchState::Complete;
}

} // namespace dcwin
