// qualification.cpp — deterministic score extraction + DCP profile derivation
// (DK0-M1; ADR-0021). No clocks, no randomness: identical inputs produce
// identical outputs (canon determinism requirement).
#include "dc/qualification.hpp"
#include "dc/json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace dc {

namespace {

constexpr uint32_t kMinSamples = 3; // variance-bearing runs only

bool FiniteScore(double v) { return std::isfinite(v) && v >= 0.0; }

// Aggregate one benchmark into a domain score. Returns nullopt when the run
// is NotRun/Unavailable/Aborted or lacks enough valid samples.
std::optional<double> ScoreOf(const QualificationSet& q, const std::string& bench_id) {
    for (const auto& b : q.benchmarks) {
        if (b.benchmark_id != bench_id) continue;
        if (b.state != BenchState::Complete) return std::nullopt;
        // Benchmark-owned final score first: benchmarks finalize their own
        // domain-unit score (GB/s for copy/bandwidth, MB/s for storage,
        // pixels/ms for raster). Recomputing from raw samples would lose the
        // unit conversion (defect found on DevKit-0: memory/storage records
        // carry direct scores with 0 samples and were reported unmeasured).
        if (FiniteScore(b.score) && b.score > 0.0) return b.score;
        // Fallback: recompute per-iteration score from raw samples
        // (iterations / seconds) for records that only carry samples.
        if (b.samples.size() < kMinSamples) return std::nullopt;
        double sum = 0.0;
        for (const auto& s : b.samples) {
            if (!FiniteScore(s.gpu_time_ms) || !FiniteScore(s.cpu_time_ms)) return std::nullopt;
        }
        for (const auto& s : b.samples) sum += s.gpu_time_ms;
        double mean = sum / static_cast<double>(b.samples.size());
        if (!(mean > 0.0)) return std::nullopt;
        double per_iter = static_cast<double>(b.iterations) / (mean / 1000.0);
        if (!FiniteScore(per_iter)) return std::nullopt;
        return per_iter;
    }
    return std::nullopt;
}

double MeanOfId(const QualificationSet& q, const std::string& bench_id) {
    auto v = ScoreOf(q, bench_id);
    return v ? *v : 0.0;
}

bool Measured(const QualificationSet& q, const std::string& bench_id) {
    return ScoreOf(q, bench_id).has_value();
}

// Map a requirement path to its stable reason code (must stay in sync with
// dc/qualification.hpp reason namespace).
const char* ReasonCodeFor(const std::string& path) {
    if (path == "cpu.game_thread_score") return reason::kCpuGameThreadBelow;
    if (path == "cpu.sustained_score") return reason::kCpuSustainedBelow;
    if (path == "gpu.raster_score") return reason::kGpuRasterBelow;
    if (path == "gpu.compute_score") return reason::kGpuComputeBelow;
    if (path == "gpu.rt_score") return reason::kGpuRtScoreBelow;
    if (path == "gpu.sustained_score") return reason::kGpuSustainedBelow;
    if (path == "memory.bandwidth_gbps") return reason::kMemoryBelow;
    if (path == "memory.ram_bytes") return reason::kRamBelow;
    if (path == "memory.vram_bytes") return reason::kVramBelow;
    if (path == "storage.read_seq_mbps") return reason::kStorageBelow;
    if (path == "storage.latency_us_p99") return reason::kStorageLatency;
    if (path == "display.min_refresh_hz") return reason::kDisplayRefreshLow;
    if (path == "display.hdr_active") return reason::kHdrMissing;
    if (path == "display.vrr_proven") return reason::kVrrUnproven;
    return reason::kNotMeasured;
}

// Sample variance (population stddev) of per-iteration scores.
double VarianceOfId(const QualificationSet& q, const std::string& bench_id) {
    for (const auto& b : q.benchmarks) {
        if (b.benchmark_id == bench_id && b.state == BenchState::Complete &&
            b.samples.size() >= kMinSamples) {
            std::vector<double> per_iter;
            per_iter.reserve(b.samples.size());
            for (const auto& s : b.samples) {
                per_iter.push_back(static_cast<double>(b.iterations) /
                                   (s.gpu_time_ms / 1000.0));
            }
            double mean = 0.0;
            for (double v : per_iter) mean += v;
            mean /= static_cast<double>(per_iter.size());
            double acc = 0.0;
            for (double v : per_iter) acc += (v - mean) * (v - mean);
            return std::sqrt(acc / static_cast<double>(per_iter.size()));
        }
    }
    return 0.0;
}

} // namespace

bool ExtractScores(const QualificationSet& q,
                   CpuScores& cpu, GpuScores& gpu,
                   MemoryScores& memory, StorageScores& storage) {
    cpu = CpuScores{};
    gpu = GpuScores{};
    memory = MemoryScores{};
    storage = StorageScores{};

    cpu.measured = Measured(q, "cpu.single-thread") && Measured(q, "cpu.game-thread");
    if (cpu.measured) {
        cpu.single_thread = MeanOfId(q, "cpu.single-thread");
        cpu.game_thread = MeanOfId(q, "cpu.game-thread");
        cpu.worker = MeanOfId(q, "cpu.worker");
        cpu.sustained = MeanOfId(q, "cpu.game-thread.sustained");
        // Throughput ratio over one thread (upper bound = logical cores).
        if (cpu.single_thread > 0.0) {
            cpu.parallel_efficiency = cpu.worker / cpu.single_thread;
        }
        // sustained_ratio: sustained game-thread over burst game-thread.
        if (cpu.game_thread > 0.0 && cpu.sustained > 0.0) {
            cpu.sustained_ratio = cpu.sustained / cpu.game_thread;
        }
    }

    bool raster = Measured(q, "gpu.raster");
    bool compute = Measured(q, "gpu.compute");
    gpu.measured = raster && compute;
    if (gpu.measured) {
        gpu.raster = MeanOfId(q, "gpu.raster");
        gpu.compute = MeanOfId(q, "gpu.compute");
        if (Measured(q, "gpu.rt_inline")) gpu.rt = MeanOfId(q, "gpu.rt_inline");
        if (Measured(q, "gpu.rt_pipeline")) gpu.rt_pipeline = MeanOfId(q, "gpu.rt_pipeline");
        if (Measured(q, "gpu.copy")) gpu.copy_gbps = MeanOfId(q, "gpu.copy"); // MB/ms == GB/s
        if (Measured(q, "gpu.raster.sustained")) {
            gpu.sustained = MeanOfId(q, "gpu.raster.sustained");
            if (gpu.raster > 0.0) gpu.sustained_ratio = gpu.sustained / gpu.raster;
        }
    }

    memory.measured = Measured(q, "memory.bandwidth");
    if (memory.measured) {
        memory.bandwidth_gbps = MeanOfId(q, "memory.bandwidth"); // MB/ms == GB/s
        // pressure_safe_bytes: measured during the memory pressure test;
        // carried on the record when present, else derived conservatively.
        for (const auto& b : q.benchmarks) {
            if (b.benchmark_id == "memory.pressure" && b.state == BenchState::Complete) {
                // benchmark-specific: score field carries safe bytes
                memory.pressure_safe_bytes = static_cast<uint64_t>(b.score);
            }
        }
        if (memory.pressure_safe_bytes == 0) {
            memory.pressure_safe_bytes = 0; // honest: not derived without the test
        }
    }

    storage.measured = Measured(q, "storage.seq");
    if (storage.measured) {
        storage.read_seq_mbps = MeanOfId(q, "storage.seq");
        storage.read_random_score = MeanOfId(q, "storage.random");
        storage.sustained_streaming_score = MeanOfId(q, "storage.sustained");
        // latency percentiles carried directly on the seq record (us).
        for (const auto& b : q.benchmarks) {
            if (b.benchmark_id == "storage.seq" && b.state == BenchState::Complete) {
                if (b.score_ratio > 0.0 && b.score_ratio != 1.0) {
                    storage.queue_depth_tested = static_cast<uint32_t>(b.score_ratio);
                }
                if (b.samples.size() >= 1) {
                    // p50/p95/p99 ride in methodology-specific sample fields:
                    // p50 in first sample cpu_time, p95/p99 carried on the
                    // record via elapsed_gpu/elapsed_cpu when present.
                    storage.latency_us_p50 = b.samples.front().cpu_time_ms;
                }
                if (b.elapsed_gpu_time_ms > 0.0) storage.latency_us_p95 = b.elapsed_gpu_time_ms;
                if (b.elapsed_cpu_time_ms > 0.0) storage.latency_us_p99 = b.elapsed_cpu_time_ms;
            }
        }
    }
    return cpu.measured || gpu.measured || memory.measured || storage.measured;
}

// ---------------------------------------------------------------------------
// Profile registry loading
// ---------------------------------------------------------------------------

namespace {

bool ParseRequirement(const json::Value& v, ProfileRequirement& out, std::string& err) {
    if (!v.is_object()) { err = "requirement must be an object"; return false; }

    // host_profile reference form (ADR-0022):
    if (const auto* hp = v.find("host_profile"); hp && hp->is_string()) {
        if (hp->as_string().empty()) { err = "host_profile empty"; return false; }
        out.host_profile = hp->as_string();
        return true;
    }
    // mode predicate form (ADR-0022):
    if (const auto* mode = v.find("mode"); mode && mode->is_object()) {
        const auto* w = mode->find("min_width");
        const auto* h = mode->find("min_height");
        const auto* rf = mode->find("min_refresh_hz");
        if (!w || !w->is_number() || !h || !h->is_number() ||
            !rf || !rf->is_number() || !std::isfinite(rf->as_number())) {
            err = "mode requires min_width/min_height/min_refresh_hz"; return false;
        }
        out.mode_min_width = static_cast<uint32_t>(w->as_number());
        out.mode_min_height = static_cast<uint32_t>(h->as_number());
        out.mode_min_refresh_hz = rf->as_number();
        if (out.mode_min_width == 0 || out.mode_min_height == 0 || out.mode_min_refresh_hz <= 0.0) {
            err = "mode constraints must be positive"; return false;
        }
        return true;
    }
    // path/value form (v1):
    const auto* path = v.find("path");
    const auto* val = v.find("value");
    if (!path || !path->is_string() || path->as_string().empty()) {
        err = "requirement.path missing/empty"; return false;
    }
    if (!val || !val->is_number() || !std::isfinite(val->as_number())) {
        err = "requirement.value missing/not finite"; return false;
    }
    out.path = path->as_string();
    out.value = val->as_number();
    return true;
}

} // namespace

bool LoadProfileRegistry(const std::string& directory,
                         std::vector<ProfileDef>& out, std::string& error) {
    out.clear();
    error.clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        error = "profile registry not a directory: " + directory;
        return false;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    if (ec) { error = "registry iteration failed"; return false; }
    std::sort(files.begin(), files.end()); // deterministic order

    for (const auto& f : files) {
        std::string ferr;
        auto root = json::ParseFile(f.string(), ferr);
        if (!root) { error = f.string() + ": " + ferr; return false; }
        if (!root->is_object()) { error = f.string() + ": root must be object"; return false; }

        const auto* schema = root->find("schema");
        if (!schema || !schema->is_string() || schema->as_string() != "dc.profile/1") {
            error = f.string() + ": schema must be dc.profile/1";
            return false;
        }
        ProfileDef def;
        const auto* id = root->find("id");
        if (!id || !id->is_string() ||
            (id->as_string().rfind("DCP-", 0) != 0 && id->as_string().rfind("DCX-", 0) != 0)) {
            error = f.string() + ": id must start with DCP- or DCX-";
            return false;
        }
        def.id = id->as_string();
        def.kind = def.id.rfind("DCX-", 0) == 0 ? "session" : "host";
        if (const auto* ver = root->find("version"); ver && ver->is_number()) {
            def.version = static_cast<uint32_t>(ver->as_number());
        }
        if (const auto* title = root->find("title"); title && title->is_string()) {
            def.title = title->as_string();
        }
        if (const auto* sup = root->find("supersedes"); sup && sup->is_string()) {
            def.supersedes = sup->as_string();
        }
        if (const auto* reqs = root->find("requires"); reqs && reqs->is_array()) {
            for (const auto& r : reqs->as_array()) {
                ProfileRequirement req;
                if (!ParseRequirement(r, req, error)) {
                    error = f.string() + ": " + error;
                    return false;
                }
                def.requirements.push_back(std::move(req));
            }
        }
        out.push_back(std::move(def));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Derivation
// ---------------------------------------------------------------------------

namespace {

// Resolved value source: either a derived score or a discovery truth.
struct Resolver {
    const HostDerivationInput* in = nullptr;

    // Returns: 1 = measured value available, 0 = path unknown,
    // -1 = domain known but not measured (must fail, not pass).
    int Resolve(const std::string& path, double& out) const {
        const HostDerivationInput& i = *in;
        if (path == "cpu.game_thread_score") {
            if (!i.cpu.measured) return -1;
            out = i.cpu.game_thread; return 1;
        }
        if (path == "cpu.sustained_score") {
            if (!i.cpu.measured || !(i.cpu.sustained > 0.0)) return -1; // aborted sustained = unmeasured, never "below" (C10)
            out = i.cpu.sustained; return 1;
        }
        if (path == "cpu.single_thread_score") {
            if (!i.cpu.measured) return -1;
            out = i.cpu.single_thread; return 1;
        }
        if (path == "cpu.worker_score") {
            if (!i.cpu.measured) return -1;
            out = i.cpu.worker; return 1;
        }
        if (path == "gpu.raster_score") {
            if (!i.gpu.measured) return -1;
            out = i.gpu.raster; return 1;
        }
        if (path == "gpu.compute_score") {
            if (!i.gpu.measured) return -1;
            out = i.gpu.compute; return 1;
        }
        if (path == "gpu.rt_score") {
            if (!i.gpu.measured) return -1;
            // RT score: full pipeline domain preferred (certification-relevant
            // path); inline ray-query is the fallback domain. Unmeasured/unavailable
            // must fail RT profiles honestly.
            if (i.gpu.rt_pipeline > 0.0) { out = i.gpu.rt_pipeline; return 1; }
            if (i.gpu.rt > 0.0) { out = i.gpu.rt; return 1; }
            return -1;
        }
        if (path == "gpu.sustained_score") {
            if (!i.gpu.measured || !(i.gpu.sustained > 0.0)) return -1; // aborted sustained = unmeasured, never "below" (C10)
            out = i.gpu.sustained; return 1;
        }
        if (path == "gpu.copy_gbps") {
            if (!i.gpu.measured) return -1;
            out = i.gpu.copy_gbps; return 1;
        }
        if (path == "gpu.matrix_score") {
            if (!i.gpu.measured) return -1;
            out = 0.0; return -1; // never measured yet (truth rule, DK0-M1G)
        }
        if (path == "memory.bandwidth_gbps") {
            if (!i.memory.measured) return -1;
            out = i.memory.bandwidth_gbps; return 1;
        }
        if (path == "storage.read_seq_mbps") {
            if (!i.storage.measured) return -1;
            out = i.storage.read_seq_mbps; return 1;
        }
        if (path == "storage.latency_us_p99") {
            if (!i.storage.measured) return -1;
            out = i.storage.latency_us_p99; return 1;
        }
        if (path == "memory.vram_bytes") { out = static_cast<double>(i.vram_bytes); return 1; }
        if (path == "memory.ram_bytes") { out = static_cast<double>(i.ram_bytes); return 1; }
        if (path == "display.min_refresh_hz") { out = i.max_display_refresh_hz; return 1; }
        if (path == "display.hdr_active") { out = i.hdr_active ? 1.0 : 0.0; return 1; }
        if (path == "display.vrr_proven") { out = i.vrr_actively_proven ? 1.0 : 0.0; return 1; }
        return 0;
    }
};

} // namespace

std::vector<DerivedProfile> DeriveProfiles(const std::vector<ProfileDef>& registry,
                                           const HostDerivationInput& in) {
    Resolver r{&in};
    std::vector<DerivedProfile> results;
    results.reserve(registry.size());

    for (const auto& def : registry) {
        if (def.kind != "host") continue; // session profiles derive below (ADR-0022)
        DerivedProfile d;
        d.def = def;
        d.supported = true;
        for (const auto& req : def.requirements) {
            double value = 0.0;
            int st = r.Resolve(req.path, value);
            if (st == 0) {
                d.supported = false;
                d.reasons.push_back(std::string("UNKNOWN_REQUIREMENT_PATH:") + req.path);
                continue;
            }
            if (st == -1) {
                d.supported = false;
                d.reasons.push_back(reason::kNotMeasured);
                continue;
            }
            // ">=" inclusive comparison; thresholds are finite.
            if (!(value >= req.value)) {
                d.supported = false;
                d.reasons.push_back(ReasonCodeFor(req.path));
            }
        }
        // Dedupe reasons deterministically, preserve first-seen order.
        std::vector<std::string> unique;
        for (const auto& rc : d.reasons) {
            if (std::find(unique.begin(), unique.end(), rc) == unique.end()) unique.push_back(rc);
        }
        d.reasons = std::move(unique);
        results.push_back(std::move(d));
    }
    return results;
}

std::vector<DerivedProfile> DeriveSessionProfiles(const std::vector<ProfileDef>& registry,
                                                  const SessionDerivationInput& in) {
    const HostDerivationInput& host = in.host_input;
    Resolver host_resolver{&host};
    std::vector<DerivedProfile> results;

    for (const auto& def : registry) {
        if (def.kind != "session") continue;
        DerivedProfile d;
        d.def = def;
        d.supported = true;
        for (const auto& req : def.requirements) {
            if (!req.host_profile.empty()) {
                // The named DCP must be present AND supported in the host
                // derivation results (ADR-0022).
                const DerivedProfile* hp = nullptr;
                for (const auto& hr : in.host_results) {
                    if (hr.def.id == req.host_profile) { hp = &hr; break; }
                }
                if (!hp || !hp->supported) {
                    d.supported = false;
                    d.reasons.push_back(reason::kHostProfileUnsatisfied);
                }
                continue;
            }
            if (req.mode_min_width != 0) {
                // Exact rational compare: num/den >= R is evaluated as
                // num*1000 >= round(R*1000)*den — profile refresh values are
                // authored at millihertz precision, so no drift is possible.
                const uint64_t rhs_millihz = static_cast<uint64_t>(
                    std::llround(req.mode_min_refresh_hz * 1000.0));
                bool found = false;
                for (const auto& m : host.display_modes) {
                    if (m.width < req.mode_min_width || m.height < req.mode_min_height) continue;
                    if (m.refresh_denominator == 0) continue;
                    const uint64_t lhs_millihz =
                        static_cast<uint64_t>(m.refresh_numerator) * 1000ull;
                    if (lhs_millihz >= rhs_millihz * static_cast<uint64_t>(m.refresh_denominator)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    d.supported = false;
                    d.reasons.push_back(reason::kDisplayModeUnavailable);
                }
                continue;
            }
            // path/value form through the host resolver (session context):
            double value = 0.0;
            int st = host_resolver.Resolve(req.path, value);
            if (st == -1 || st == 0) {
                d.supported = false;
                d.reasons.push_back(req.path == "display.hdr_active" ? reason::kHdrInactive
                                    : req.path == "display.vrr_proven" ? reason::kVrrUnproven
                                    : reason::kNotMeasured);
                continue;
            }
            if (!(value >= req.value)) {
                d.supported = false;
                d.reasons.push_back(req.path == "display.hdr_active" ? reason::kHdrInactive
                                    : req.path == "display.vrr_proven" ? reason::kVrrUnproven
                                    : ReasonCodeFor(req.path));
            }
        }
        std::vector<std::string> unique;
        for (const auto& rc : d.reasons) {
            if (std::find(unique.begin(), unique.end(), rc) == unique.end()) unique.push_back(rc);
        }
        d.reasons = std::move(unique);
        results.push_back(std::move(d));
    }
    return results;
}

} // namespace dc
