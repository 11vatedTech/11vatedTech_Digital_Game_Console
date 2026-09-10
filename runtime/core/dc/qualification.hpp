// dc/qualification.hpp — measured qualification model (DK0-M1; ADR-0021).
// runtime/core, host-agnostic (canon §39.1): hosts fill RawMeasurement
// records; the qualification engine consumes them deterministically.
// Truth rules (C5/C7/C10):
//   - measured=false means "not measured"; the paired score stays null.
//   - aborted runs are recorded as aborted, never as failed capability.
//   - every score carries its raw measurements and methodology id.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dc {

// Measurement state machine: a benchmark either completes with samples,
// or is explicitly aborted (recorded as aborted, never as failed capability),
// or reports unavailable (capability absent on this host).
enum class BenchState {
    NotRun = 0,
    Complete,
    Aborted,
    Unavailable,
};

struct BenchSample {
    double gpu_time_ms = 0.0; // GPU timestamp delta when available
    double cpu_time_ms = 0.0; // wall-clock delta
};

// One benchmark's evidence: identity + raw samples + derived result.
struct BenchmarkRecord {
    std::string benchmark_id;     // "cpu.game-thread", "gpu.raster", ...
    uint32_t benchmark_version = 1;
    std::string adapter_id;       // DXGI LUID or "cpu"
    std::string driver_version;
    uint32_t iterations = 0;
    uint32_t warmup_iterations = 0;
    BenchState state = BenchState::NotRun;
    std::string abort_reason;     // populated when state == Aborted
    std::vector<BenchSample> samples; // post-warmup samples (>=3 for variance)
    double elapsed_gpu_time_ms = 0.0; // aggregate across iterations
    double elapsed_cpu_time_ms = 0.0;
    double score = 0.0;           // domain units (ops/ms, MB/s, ...)
    double variance = 0.0;        // sample stddev of per-iteration scores
    double score_ratio = 1.0;     // sustained/burst where applicable (1.0 = unmeasured)
    std::string methodology_id;   // "dc-bench/1:<domain>" — see docs/benchmarks.md
    std::string runtime_version;  // platform runtime semver
    std::string timestamp_utc;
};

// Full qualification payload emitted by one hostprof --qualify pass.
struct QualificationSet {
    std::string mode = "quick"; // quick | standard | certification
    bool measured = false;      // false = discovery-only pass (ADR-0021)
    bool sustained_valid = false;
    std::string timestamp_utc;
    std::vector<BenchmarkRecord> benchmarks;
};

// Domain scores extracted from a QualificationSet by the deterministic engine.
struct CpuScores {
    bool measured = false;
    double single_thread = 0.0;
    double game_thread = 0.0;
    double worker = 0.0;
    double parallel_efficiency = 0.0; // worker_score / (single_thread * cores), 0..1
    double sustained = 0.0;
    double sustained_ratio = 0.0;
};

struct GpuScores {
    bool measured = false;
    double raster = 0.0;      // mpixels/s (fill+vertex composite)
    double compute = 0.0;     // gflops equivalent (fp32 workload)
    double rt = 0.0;          // inline ray-query domain, mray/s; unmeasured => !measured
    double rt_pipeline = 0.0; // full DXR pipeline domain (DispatchRays), mray/s
    double copy_gbps = 0.0;   // device-internal copy bandwidth
    double sustained = 0.0;   // sustained composite
    double sustained_ratio = 0.0;
};

struct MemoryScores {
    bool measured = false;
    double bandwidth_gbps = 0.0;
    uint64_t pressure_safe_bytes = 0;
};

struct StorageScores {
    bool measured = false;
    double read_seq_mbps = 0.0;
    double read_random_score = 0.0; // kIOPS at queue_depth_tested
    double latency_us_p50 = 0.0;
    double latency_us_p95 = 0.0;
    double latency_us_p99 = 0.0;
    uint32_t queue_depth_tested = 0;
    double sustained_streaming_score = 0.0; // MB/s over sustained window
};

// Deterministic extraction (no clocks, no randomness): same
// QualificationSet => same scores. Rejects NaN/Inf samples; rejects runs
// with < 3 samples for score derivation (state stays Complete only for
// variance-bearing runs; otherwise Aborted with reason).
bool ExtractScores(const QualificationSet& q,
                   CpuScores& cpu, GpuScores& gpu,
                   MemoryScores& memory, StorageScores& storage);

// -------- Profile derivation (DCP; machine-readable constraints) ----------

// Reason codes (stable, machine-readable; extend only additively).
namespace reason {
    inline constexpr const char* kCpuGameThreadBelow = "CPU_GAMETHREAD_BELOW_REQUIREMENT";
    inline constexpr const char* kCpuSustainedBelow = "CPU_SUSTAINED_SCORE_BELOW_REQUIREMENT";
    inline constexpr const char* kGpuRasterBelow = "GPU_RASTER_SCORE_BELOW_REQUIREMENT";
    inline constexpr const char* kGpuComputeBelow = "GPU_COMPUTE_SCORE_BELOW_REQUIREMENT";
    inline constexpr const char* kGpuRtTier = "GPU_RT_TIER_BELOW_REQUIREMENT";
    inline constexpr const char* kGpuRtScoreBelow = "GPU_RT_SCORE_BELOW_REQUIREMENT";
    inline constexpr const char* kGpuSustainedBelow = "GPU_SUSTAINED_SCORE_BELOW_REQUIREMENT";
    inline constexpr const char* kVramBelow = "VRAM_BELOW_REQUIREMENT";
    inline constexpr const char* kMemoryBelow = "MEMORY_BANDWIDTH_BELOW_REQUIREMENT";
    inline constexpr const char* kRamBelow = "RAM_BELOW_REQUIREMENT";
    inline constexpr const char* kStorageBelow = "STORAGE_SEQ_BELOW_REQUIREMENT";
    inline constexpr const char* kStorageLatency = "STORAGE_LATENCY_ABOVE_REQUIREMENT";
    inline constexpr const char* kDisplayRefreshLow = "DISPLAY_REFRESH_TOO_LOW";
    inline constexpr const char* kHdrMissing = "HDR_UNAVAILABLE";
    inline constexpr const char* kVrrUnproven = "VRR_NOT_ACTIVELY_PROVEN";
    inline constexpr const char* kNotMeasured = "CAPABILITY_NOT_MEASURED";
    // Session (DCX) reason codes — ADR-0022:
    inline constexpr const char* kDisplayModeUnavailable = "DISPLAY_MODE_UNAVAILABLE";
    inline constexpr const char* kHdrInactive = "HDR_INACTIVE";
    inline constexpr const char* kHostProfileUnsatisfied = "HOST_PROFILE_UNSATISFIED";
} // namespace reason

// One requirement from a DCP/DCX profile (loaded from formats/profiles/*.json).
// Three forms (ADR-0022), discriminated at parse time:
//   path/value  — ">=" constraint over a resolved host/session path (v1 form)
//   host_profile — the named DCP must be satisfied by the host derivation
//   mode        — an enumerated display mode must exist with width >=
//                 min_width AND height >= min_height AND refresh >=
//                 min_refresh_hz (conjunctive over ONE mode; rationals honored)
struct ProfileRequirement {
    // path/value form:
    std::string path;
    double value = 0.0;
    // host_profile form:
    std::string host_profile;   // non-empty => reference form
    // mode form:
    uint32_t mode_min_width = 0;    // non-zero => mode form
    uint32_t mode_min_height = 0;
    double mode_min_refresh_hz = 0.0; // compared as numerator/denominator >= value
};

struct ProfileDef {
    std::string id;        // "DCP-2026-BASE" / "DCX-UHD120"
    uint32_t version = 1;
    std::string title;
    std::string kind = "host";    // "host" (DCP) | "session" (DCX) — ADR-0022
    std::string supersedes;       // optional migration provenance
    std::vector<ProfileRequirement> requirements; // JSON key: "requires"
};

struct DerivedProfile {
    ProfileDef def;
    bool supported = false;
    std::vector<std::string> reasons; // reason codes when unsupported
};

// Profile derivation input: the derived scores + discovery truth needed by
// profile constraints (display refresh, HDR, VRR, VRAM, RAM...).
struct HostDerivationInput {
    CpuScores cpu;
    GpuScores gpu;
    MemoryScores memory;
    StorageScores storage;

    // discovery-side constraints (deterministic pass outputs):
    uint64_t vram_bytes = 0;
    uint64_t ram_bytes = 0;
    double max_display_refresh_hz = 0.0;
    bool hdr_active = false;
    bool vrr_actively_proven = false;

    // display modes for session `mode` requirements (ADR-0022). Rational
    // pairs are compared exactly: num/den >= required without double drift.
    struct ModeEntry {
        uint32_t width = 0, height = 0;
        uint32_t refresh_numerator = 0, refresh_denominator = 0;
    };
    std::vector<ModeEntry> display_modes;
};

// Load the profile registry from a directory of *.json profile files.
// Invalid/NaN constraints or unknown schema -> file rejected with error text.
bool LoadProfileRegistry(const std::string& directory,
                         std::vector<ProfileDef>& out, std::string& error);

// Deterministic derivation. missing measurements produce reason::kNotMeasured
// for every constraint that touches the missing domain (never a pass).
std::vector<DerivedProfile> DeriveProfiles(const std::vector<ProfileDef>& registry,
                                           const HostDerivationInput& in);

// Session-experience derivation (DCX, ADR-0022): consumes the host derivation
// RESULTS (DCP claims/rejections) + display/presentation truth. Deterministic.
struct SessionDerivationInput {
    std::vector<DerivedProfile> host_results; // DCP derivation output
    HostDerivationInput host_input;           // display modes / hdr / vrr truth
};
std::vector<DerivedProfile> DeriveSessionProfiles(const std::vector<ProfileDef>& registry,
                                                  const SessionDerivationInput& in);

// Serialize a QualificationSet to JSON (schema dc.qualification/1) for the
// evidence/qualification/*.json files (ADR-0018).
std::string QualificationToJson(const QualificationSet& q, bool pretty = true);

} // namespace dc
