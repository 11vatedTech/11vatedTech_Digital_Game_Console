// qualify_runner.cpp — qualification orchestration (DK0-M1F).
// Runs the platform benchmark set for a mode (quick/standard/certification),
// filling a QualificationSet. Benchmarks run in a fixed order; each is
// individually abort-safe. Identity fields come from real discovery truth.
#include "bench_common.hpp"
#include "bench_gpu.hpp"
#include "dc/capability.hpp"
#include "dc/qualification.hpp"
#include "win_util.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <psapi.h>
#include <string>
#include <vector>

// Benchmarks from the other TUs:
namespace dcwin {
void BenchCpuSingleThread(const BenchIdentity& id, BenchCancel& cancel, BenchmarkRecord& rec);
void BenchCpuGameThread(const BenchIdentity& id, BenchCancel& cancel, BenchmarkRecord& rec);
void BenchCpuWorker(const BenchIdentity& id, BenchCancel& cancel, BenchmarkRecord& rec, uint32_t logical_cores);
void BenchCpuSustained(const BenchIdentity& id, BenchCancel& cancel, BenchmarkRecord& rec, const SustainedConfig& cfg);
void BenchMemoryBandwidth(const BenchIdentity& id, BenchCancel& cancel, BenchmarkRecord& rec);
void BenchMemoryPressure(const BenchIdentity& id, BenchCancel& cancel, BenchmarkRecord& rec);
void BenchStorageSeq(const BenchIdentity& id, BenchCancel& cancel, BenchmarkRecord& rec);
void BenchStorageRandom(const BenchIdentity& id, BenchCancel& cancel, BenchmarkRecord& rec, uint32_t queue_depth);
void BenchStorageSustained(const BenchIdentity& id, BenchCancel& cancel, BenchmarkRecord& rec, const SustainedConfig& cfg);
}

namespace dcwin {

namespace {

constexpr uint32_t kRandomQueueDepth = 8; // documented queue depth (docs table)

// Per-mode deadlines (docs/benchmarks.md rule 6). Two layers:
//  - per-benchmark budget: a single benchmark may not exceed this (quick=30s
//    fits the 8s sustained window + discovery benches with margin; standard
//    60s fits 20s windows; certification 180s fits 45s windows x6 samples).
//  - global ceiling: last-resort safety stop for the whole run (2x the sum of
//    realistic per-bench worst cases; aborts beyond this are recorded as
//    aborted, never as failed capability).
double ModeDeadlineMs(const std::string& mode) {
    if (mode == "certification") return 900000.0;
    if (mode == "standard") return 360000.0;
    return 180000.0;
}

double PerBenchDeadlineMs(const std::string& mode) {
    if (mode == "certification") return 180000.0;
    if (mode == "standard") return 60000.0;
    return 30000.0;
}

BenchIdentity IdentityFromDiscovery(const dc::HostCapabilityRecord& disc) {
    BenchIdentity id;
    id.adapter_id = "LUID-host"; // refined when GPU env initializes
    id.driver_version = disc.gpu.driver_version;
    id.runtime_version = disc.build.runtime_version.empty() ? "0.1.0" : disc.build.runtime_version;
    return id;
}

} // namespace

bool RunQualification(const dc::HostCapabilityRecord& discovery,
                      const std::string& mode,
                      dc::QualificationSet& out) {
    out = dc::QualificationSet{};
    out.mode = mode;
    out.measured = true;
    out.timestamp_utc = UtcTimestamp();

    BenchIdentity id = IdentityFromDiscovery(discovery);
    BenchCancel cancel;
    const double global_deadline = QpcMs() + ModeDeadlineMs(mode);
    const double per_bench_deadline = PerBenchDeadlineMs(mode);
    SustainedConfig cfg = SustainedConfig::For(mode);

    // Each benchmark writes into a FRESH record (a previous aborted/unavailable
    // run must not leak identity fields into the next record) and gets its own
    // deadline — otherwise slow CPU/storage benches starve the GPU chain
    // (DevKit-0 defect: quick-mode global 60s expired before the first GPU
    // sample, recording every GPU bench as 'cancelled').
    dc::BenchmarkRecord rec;
    auto emit = [&](auto&& fn) {
        rec = dc::BenchmarkRecord{};
        if (QpcMs() >= global_deadline) {
            cancel.requested = true;
            cancel.reason = "global-deadline";
        } else {
            cancel.requested = false;
            cancel.reason.clear();
            cancel.deadline_ms = QpcMs() + per_bench_deadline;
        }
        fn(rec);
        out.benchmarks.push_back(rec);
    };

    // ---- CPU (game-thread capability first: it gates every profile) ----
    emit([&](dc::BenchmarkRecord& r) { BenchCpuSingleThread(id, cancel, r); });
    emit([&](dc::BenchmarkRecord& r) { BenchCpuGameThread(id, cancel, r); });
    emit([&](dc::BenchmarkRecord& r) { BenchCpuWorker(id, cancel, r, discovery.cpu.logical_cores); });
    emit([&](dc::BenchmarkRecord& r) { BenchCpuSustained(id, cancel, r, cfg); });

    // ---- Memory ----
    emit([&](dc::BenchmarkRecord& r) { BenchMemoryBandwidth(id, cancel, r); });
    emit([&](dc::BenchmarkRecord& r) { BenchMemoryPressure(id, cancel, r); });

    // ---- Storage ----
    emit([&](dc::BenchmarkRecord& r) { BenchStorageSeq(id, cancel, r); });
    emit([&](dc::BenchmarkRecord& r) { BenchStorageRandom(id, cancel, r, kRandomQueueDepth); });
    emit([&](dc::BenchmarkRecord& r) { BenchStorageSustained(id, cancel, r, cfg); });

    // ---- GPU (skip silently-unavailable domains honestly) ----
    // Fresh device per benchmark: a fault in one bench must neither poison
    // its successors nor mask which bench actually failed (DevKit-0: the
    // device removal surfaced in a successor's entry while the predecessor
    // reported complete). Each bench starts from clean driver state.
    GpuBenchEnv gpu_env;
    auto gpu_bench = [&](const char* bench_id, auto&& fn) {
        gpu_env = GpuBenchEnv{};
        bool healthy = false;
        // One retry: a fresh device can be born into a transiently wedged
        // per-process adapter context (DevKit-0 — clears by the next device
        // creation). Recreate once before recording an honest Unavailable.
        for (int attempt = 0; attempt < 2 && !healthy; ++attempt) {
            if (!InitGpuBenchEnv(gpu_env)) break;
            healthy = GpuEnvHealthy(gpu_env);
            if (!healthy) ShutdownGpuBenchEnv(gpu_env);
        }
        if (!healthy) {
            rec = dc::BenchmarkRecord{};
            rec.benchmark_id = bench_id;
            rec.state = dc::BenchState::Unavailable;
            rec.abort_reason = gpu_env.error.empty() ? "device-unhealthy-at-init" : gpu_env.error;
            rec.timestamp_utc = UtcTimestamp();
            out.benchmarks.push_back(rec);
            return;
        }
        gpu_env.driver_version = discovery.gpu.driver_version;
        fn();
        ShutdownGpuBenchEnv(gpu_env);
    };

    // ORDER (DevKit-0 evidence): compute and copy run BEFORE raster. The
    // driver asynchronously faults while processing raster's submissions and
    // wedges the per-process adapter context for whichever bench FOLLOWS
    // raster (GetDeviceRemovedReason=DXGI_ERROR_INVALID_CALL at its entry,
    // recovering by the next device creation). Running raster-shaped benches
    // last means every bench still measures on a healthy context. Revisit if
    // the driver behaves differently after an update.
    // ORDER (DevKit-0 evidence): raster-shaped benches run LAST. The driver
    // asynchronously faults while processing raster's submissions and wedges
    // the per-process adapter context for whichever bench FOLLOWS raster
    // (GetDeviceRemovedReason=DXGI_ERROR_INVALID_CALL, recovering by the next
    // device creation). gpu.rt is compute-shaped (ray-query dispatch) and also
    // wedged when it directly followed raster (2026-09-07 evidence), so it
    // runs before raster too. Revisit if the driver behaves differently after
    // an update.
    gpu_bench("gpu.compute", [&] {
        emit([&](dc::BenchmarkRecord& r) { BenchGpuCompute(gpu_env, cancel, r); });
    });
    gpu_bench("gpu.copy", [&] {
        emit([&](dc::BenchmarkRecord& r) { BenchGpuCopy(gpu_env, cancel, r); });
    });
    gpu_bench("gpu.rt_inline", [&] {
        emit([&](dc::BenchmarkRecord& r) { BenchGpuRaytracing(gpu_env, cancel, r, discovery.gpu.d3d12.raytracing_tier); });
    });
    if (std::getenv("DC_SKIP_RT_PIPELINE") == nullptr) {
    gpu_bench("gpu.rt_pipeline", [&] {
        emit([&](dc::BenchmarkRecord& r) { BenchGpuRaytracingPipeline(gpu_env, cancel, r, discovery.gpu.d3d12.raytracing_tier); });
    });
    }
    gpu_bench("gpu.raster", [&] {
        emit([&](dc::BenchmarkRecord& r) { BenchGpuRaster(gpu_env, cancel, r); });
    });
    gpu_bench("gpu.raster.sustained", [&] {
        emit([&](dc::BenchmarkRecord& r) { BenchGpuSustained(gpu_env, cancel, r, cfg); });
    });

    // sustained_valid: at least the CPU and GPU sustained runs completed.
    bool cpu_sust = false, gpu_sust = false;
    for (const auto& b : out.benchmarks) {
        if (b.state == dc::BenchState::Complete) {
            if (b.benchmark_id == "cpu.game-thread.sustained") cpu_sust = true;
            if (b.benchmark_id == "gpu.raster.sustained") gpu_sust = true;
        }
    }
    out.sustained_valid = cpu_sust && gpu_sust;
    return true;
}

} // namespace dcwin
