// bench_common.hpp — benchmark infrastructure (hosts/windows, DK0-M1).
// Timing via QPC; deterministic PRNG (splitmix64); sample-statistics helpers.
// Rules implemented here per docs/benchmarks.md (warmups, variance, aborts).
#pragma once

#include "dc/qualification.hpp"
#include "win_util.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace dcwin {

using namespace dc; // bench layer operates on dc:: qualification types

// Deterministic PRNG (splitmix64) — benchmark inputs must not depend on
// std::random_device or time (canon determinism requirement).
class BenchRng {
public:
    explicit BenchRng(uint64_t seed) : state_(seed + 0x9E3779B97F4A7C15ULL) {}
    uint64_t Next() {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    double NextUnit() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }

private:
    uint64_t state_;
};

// QPC wall clock in milliseconds.
inline double QpcMs() {
    LARGE_INTEGER f{}, now{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

// Escape the value's address so the optimizer cannot float the pure workload
// that produced it across the next opaque call (found on DevKit-0: MSVC moved
// the single-thread workload past the second QPC call => dt measured 0).
// Call INSIDE the timed region, immediately after the workload.
template <typename T>
inline void DoNotOptimize(const T& value) {
    const volatile T* sink = &value;
    (void)sink;
    _ReadWriteBarrier();
}

// Cooperative cancellation: benchmarks poll between samples. Ctrl+C or
// deadline expiry flips the flag; runs then record Aborted (never scores).
struct BenchCancel {
    bool requested = false;
    std::string reason;
    double deadline_ms = 0.0; // QpcMs() deadline; 0 = none
    bool Expired(double now_ms) const { return deadline_ms > 0.0 && now_ms >= deadline_ms; }
};

// Per-mode sustained-window configuration (docs/benchmarks.md rule 5).
struct SustainedConfig {
    double window_ms = 8000.0; // quick default
    uint32_t samples = 2;
    static SustainedConfig For(const std::string& mode) {
        if (mode == "certification") return {45000.0, 6};
        if (mode == "standard") return {20000.0, 4};
        return {8000.0, 3}; // >= 3 samples: variance-bearing per FinalizeRecord
    }
};

// Finalize a BenchmarkRecord: compute score (iterations / mean sample time),
// population-stddev variance, instability abort (docs/benchmarks.md rule 3).
// Returns false when the run was aborted (record keeps state/reason).
inline bool FinalizeRecord(BenchmarkRecord& rec, const std::vector<BenchSample>& samples,
                           double iterations_per_sample) {
    rec.samples = samples;
    if (samples.size() < 3) {
        rec.state = BenchState::Aborted;
        rec.abort_reason = "insufficient-samples";
        return false;
    }
    for (const auto& s : samples) {
        if (!std::isfinite(s.gpu_time_ms) || !std::isfinite(s.cpu_time_ms) || s.gpu_time_ms <= 0.0) {
            rec.state = BenchState::Aborted;
            rec.abort_reason = "invalid-sample";
            return false;
        }
    }
    std::vector<double> per_iter;
    per_iter.reserve(samples.size());
    for (const auto& s : samples) {
        per_iter.push_back(iterations_per_sample / (s.gpu_time_ms / 1000.0));
    }
    double mean_score = 0.0;
    for (double v : per_iter) mean_score += v;
    mean_score /= static_cast<double>(per_iter.size());
    double acc = 0.0;
    for (double v : per_iter) acc += (v - mean_score) * (v - mean_score);
    double var = std::sqrt(acc / static_cast<double>(per_iter.size()));
    if (mean_score > 0.0 && var / mean_score > 0.25) {
        rec.state = BenchState::Aborted;
        rec.abort_reason = "unstable-samples";
        return false;
    }
    rec.score = mean_score;
    rec.variance = var;
    rec.state = BenchState::Complete;
    return true;
}

// Stamp identity fields shared by every record (ADR-0018 evidence identity).
struct BenchIdentity {
    std::string adapter_id;
    std::string driver_version;
    std::string runtime_version = "0.1.0";
};

inline void StampRecord(BenchmarkRecord& rec, const BenchIdentity& id,
                        const char* benchmark_id, uint32_t iterations,
                        uint32_t warmups, const char* methodology_id) {
    rec.benchmark_id = benchmark_id;
    rec.benchmark_version = 1;
    rec.adapter_id = id.adapter_id;
    rec.driver_version = id.driver_version;
    rec.iterations = iterations;
    rec.warmup_iterations = warmups;
    rec.methodology_id = methodology_id;
    rec.runtime_version = id.runtime_version;
    rec.timestamp_utc = UtcTimestamp();
}

} // namespace dcwin
