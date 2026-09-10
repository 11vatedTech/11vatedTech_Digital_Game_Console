// main.cpp — dc-hostprof: Host Doctor (DK0-M1; DC-CANON-001 §37.1, §50).
// Discovery pass (default): dc.host-capability/2 discovery-only record.
// Qualification pass (--qualify MODE): measured benchmarks + profile
// derivation + evidence files (ADR-0018/0021). Truth rules per C5/C10:
// unmeasured never claimed; aborted never failed; profile claims only from
// deterministic derivation.
#define _CRT_SECURE_NO_WARNINGS
#include "dc/capability.hpp"
#include "dc/i_host_platform.hpp"
#include "dc/qualification.hpp"
#include "dc/json.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace dcwin {
bool RunQualification(const dc::HostCapabilityRecord& discovery,
                      const std::string& mode,
                      dc::QualificationSet& out);
}

namespace {

void PrintUsage() {
    std::printf(
        "dc-hostprof — 11vated Digital Console Host Doctor (DK0-M1)\n"
        "\n"
        "Usage:\n"
        "  dc-hostprof [options]\n"
        "\n"
        "Options:\n"
        "  --json            Print dc.host-capability/2 JSON only\n"
        "  --report          Print human-readable report only (default)\n"
        "  --out FILE        Additionally write JSON to FILE\n"
        "  --schema-version N  Emit 1 (legacy compat shape) or 2 (default)\n"
        "  --qualify MODE    Run measured qualification: quick|standard|certification\n"
        "                    (writes evidence/qualification/*.json + derives DCP profiles)\n"
        "  --profiles DIR    Profile registry directory (default: formats/profiles)\n"
        "  --help            Show this help\n");
}

void PrintReport(const dc::HostCapabilityRecord& r) {
    std::printf("=================================================================\n");
    std::printf(" 11vated Digital Console — Host Qualification Report (DK0-M1)\n");
    std::printf("=================================================================\n");
    std::printf(" schema        : %s\n", r.schema_version.c_str());
    std::printf(" host_id       : %s\n", r.host_id.c_str());
    std::printf(" generated_utc : %s\n", r.generated_utc.c_str());
    std::printf(" build         : runtime %s, config %s, git %s\n",
                r.build.runtime_version.c_str(), r.build.build_config.c_str(),
                r.build.git_hash.c_str());
    std::printf("\n-- Host OS --------------------------------------------------\n");
    std::printf(" family/version: %s %s (build %u)\n", r.os.family.c_str(),
                r.os.version.c_str(), r.os.build);
    std::printf(" edition       : %s\n", r.os.edition.c_str());
    std::printf("\n-- CPU ------------------------------------------------------\n");
    std::printf(" model         : %s\n", r.cpu.model_name.c_str());
    std::printf(" topology      : %u physical / %u logical cores\n",
                r.cpu.physical_cores, r.cpu.logical_cores);
    std::printf(" base clock    : %.0f Hz\n", r.cpu.base_clock_hz);
    std::printf(" scores        : game=%.0f worker=%.0f sustained=%.0f\n",
                r.cpu.game_thread_score, r.cpu.worker_score, r.cpu.sustained_score);
    std::printf("\n-- GPU ------------------------------------------------------\n");
    std::printf(" model         : %s (%s)\n", r.gpu.model_name.c_str(), r.gpu.vendor.c_str());
    std::printf(" driver        : %s\n", r.gpu.driver_version.c_str());
    std::printf(" vram          : %.1f GB\n", static_cast<double>(r.gpu.vram_bytes) / (1024.0 * 1024.0 * 1024.0));
    for (const auto& api : r.gpu.api) std::printf(" api           : %s\n", api.c_str());
    if (r.gpu.d3d12.available) {
        std::printf(" d3d12         : FL %s, RT tier %.1f, mesh_shader=%s, sampler_feedback=%s\n",
                    r.gpu.d3d12.feature_level.c_str(), r.gpu.d3d12.raytracing_tier,
                    r.gpu.d3d12.mesh_shader ? "yes" : "no",
                    r.gpu.d3d12.sampler_feedback ? "yes" : "no");
    }
    if (r.gpu.vulkan.available) {
        std::printf(" vulkan        : %s, %s, ray_query=%s\n",
                    r.gpu.vulkan.api_version.c_str(), r.gpu.vulkan.device_name.c_str(),
                    r.gpu.vulkan.ray_query ? "yes" : "no");
    }
    std::printf(" scores        : raster=%.0f compute=%.0f rt=%.0f matrix=%.0f\n",
                r.gpu.raster_score, r.gpu.compute_score, r.gpu.rt_score, r.gpu.matrix_score);
    std::printf("\n-- Memory ---------------------------------------------------\n");
    std::printf(" system        : %.1f GB (%.1f GB available)\n",
                static_cast<double>(r.memory.system_bytes) / (1024.0 * 1024.0 * 1024.0),
                static_cast<double>(r.memory.available_bytes) / (1024.0 * 1024.0 * 1024.0));
    std::printf(" bandwidth     : %.0f MB/s (0 = unmeasured)\n", r.memory.bandwidth_score);
    std::printf(" pressure-safe : %.1f GB\n",
                static_cast<double>(r.memory.pressure_safe_bytes) / (1024.0 * 1024.0 * 1024.0));
    std::printf("\n-- Storage --------------------------------------------------\n");
    for (const auto& s : r.storage) {
        std::printf(" %-12s : %-9s %-10s cap %.0f GB free %.0f GB\n",
                    s.id.c_str(), s.klass.c_str(), s.bus_type.c_str(),
                    static_cast<double>(s.capacity_bytes) / (1024.0 * 1024.0 * 1024.0),
                    static_cast<double>(s.free_bytes) / (1024.0 * 1024.0 * 1024.0));
        if (s.read_seq_mbps > 0.0) {
            std::printf("               : seq %.0f MB/s, rand %.1f kIOPS@qd%u, p50/p95/p99 %.0f/%.0f/%.0f us\n",
                        s.read_seq_mbps, s.read_random_score, s.queue_depth_tested,
                        s.latency_us_p50, s.latency_us_p95, s.latency_us_p99);
        }
    }
    std::printf("\n-- Display --------------------------------------------------\n");
    for (const auto& d : r.display) {
        std::printf(" %-10s : %s%s\n", d.id.c_str(), d.name.c_str(),
                    d.primary ? "  [primary]" : "");
        std::printf("   desktop     : %ux%u @ %.0f Hz\n", d.width, d.height, d.desktop_refresh_hz);
        std::printf("   modes       : %zu enumerated\n", d.modes.size());
        std::printf("   hdr         : supported=%s active=%s kind=%s bits=%u sdr_white=%.0f nits\n",
                    d.hdr.advanced_color_supported ? "yes" : "no",
                    d.hdr.advanced_color_active ? "yes" : "no",
                    d.hdr.advanced_color_kind.c_str(), d.hdr.bits_per_color,
                    d.hdr.sdr_white_level_nits);
        if (d.hdr.metadata_available) {
            std::printf("   hdr meta    : min %.2f / max %.0f / maxFF %.0f nits, wp %.3f,%.3f\n",
                        d.hdr.min_luminance_nits, d.hdr.max_luminance_nits,
                        d.hdr.max_full_frame_luminance_nits,
                        d.hdr.white_point[0], d.hdr.white_point[1]);
        }
        std::printf("   vrr         : capable=%s path=%s proven=%s\n",
                    d.vrr.supported ? "yes" : "no",
                    d.vrr.path_compatible ? "yes" : "no",
                    d.vrr.actively_proven ? "yes" : "no");
    }
    std::printf("\n-- DirectStorage --------------------------------------------\n");
    std::printf(" runtime       : %s %s\n",
                r.directstorage.runtime_available ? "available" : "unavailable",
                r.directstorage.version.c_str());
    std::printf(" gpu decomp    : %s, gdeflate=%s, zstd=%s\n",
                r.directstorage.gpu_decompression_supported ? "yes" : "unverified",
                r.directstorage.gdeflate_supported ? "yes" : "unverified",
                r.directstorage.zstd_status.c_str());
    std::printf("\n-- Audio / Input --------------------------------------------\n");
    for (const auto& a : r.audio) {
        std::printf(" %-8s    : %s%s (%u ch)\n", a.kind.c_str(), a.name.c_str(),
                    a.default_output ? "  [default]" : "", a.channels);
    }
    if (r.input.empty()) std::printf(" (no gamepads detected via XInput floor)\n");
    for (const auto& i : r.input) {
        std::printf(" %-12s: %s [%s/%s]%s\n", i.id.c_str(), i.name.c_str(),
                    i.backend.c_str(), i.connection.c_str(),
                    i.haptics ? " haptics" : "");
    }
    std::printf("\n-- Thermal / Trust -----------------------------------------\n");
    std::printf(" sustained     : %s\n", r.thermal.sustained_profile_valid ? "valid" : "not yet qualified");
    std::printf(" secure boot   : %s, tpm %s %s\n",
                r.trust.secure_boot ? "on" : "off/unknown",
                r.trust.tpm ? "present" : "absent/unknown", r.trust.tpm_version.c_str());
    std::printf("\n-- Qualification -------------------------------------------\n");
    std::printf(" measured      : %s (mode: %s)\n",
                r.qualification.measured ? "yes" : "no (discovery-only pass)",
                r.qualification.mode.c_str());
    std::printf("\n-- Host Capabilities (DCP, ADR-0022) ------------------------\n");
    if (r.profiles_claimed.empty() && r.profiles_rejected.empty()) {
        std::printf(" (none derived — run --qualify for measured derivation)\n");
    }
    for (const auto& p : r.profiles_claimed) {
        std::printf(" PASS       : %s (v%u)\n", p.id.c_str(), p.version);
    }
    for (const auto& p : r.profiles_rejected) {
        std::printf(" FAIL       : %s\n", p.id.c_str());
        for (const auto& rc : p.reasons) std::printf("             - %s\n", rc.c_str());
    }
    std::printf("\n-- Session Capabilities (DCX, current display/presentation) --\n");
    for (const auto& p : r.session_profiles_claimed) {
        std::printf(" PASS       : %s (v%u)\n", p.id.c_str(), p.version);
    }
    for (const auto& p : r.session_profiles_rejected) {
        std::printf(" FAIL       : %s\n", p.id.c_str());
        for (const auto& rc : p.reasons) std::printf("             - %s\n", rc.c_str());
    }
    std::printf("=================================================================\n");
    std::printf(" Discovery pass complete. Unmeasured fields are 0/false and are\n");
    std::printf(" never claimed (constitution C5/C10). Run --qualify for measured\n");
    std::printf(" qualification and DCP profile derivation.\n");
}

bool WriteFileText(const std::string& path, const std::string& text) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    return true;
}

// ADR-0023 evidence policy: qualification runs are IMMUTABLE. Every run
// writes evidence/qualification/runs/<run-id>/ and never overwrites a prior
// run. Run-id = UTC timestamp + mode + short identity hash (timestamp-first
// so directory listings sort chronologically; the full identity lives in
// manifest.json). The index file is the only mutable artifact and is
// append-merged, never rewritten from memory.
std::string SanitizeRunId(std::string s) {
    for (char& c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) c = '_';
    }
    return s;
}

std::string NewRunId(const std::string& timestamp_utc, const std::string& mode) {
    // timestamp_utc is ISO-8601; make it filesystem-safe and sort-friendly.
    std::string ts = SanitizeRunId(timestamp_utc);
    for (char& c : ts) if (c == ':') c = '-';
    std::string id = ts + "_" + SanitizeRunId(mode);
    return id;
}

// Append-merge one run entry into the qualification index (JSON array).
void AppendRunIndex(const std::string& index_path, const std::string& entry_json) {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(index_path).parent_path(), ec);
    std::string existing;
    {
        FILE* f = std::fopen(index_path.c_str(), "rb");
        if (f) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) existing.append(buf, n);
            std::fclose(f);
        }
    }
    // Trim whitespace; tolerate missing/empty file (start a new array).
    size_t a = existing.find_first_not_of(" \t\r\n");
    std::string body = (a == std::string::npos) ? "" : existing;
    std::string out;
    if (!body.empty() && body.find('[') != std::string::npos && body.find(']') != std::string::npos) {
        // Insert entry before the final ']'. Naive but safe for our own files:
        // we always write well-formed arrays with a trailing newline.
        size_t close = body.rfind(']');
        std::string inner = body.substr(0, close);
        size_t last = inner.rfind('}');
        if (last != std::string::npos) {
            out = inner.substr(0, last + 1) + ",\n" + entry_json + "\n]\n";
        } else {
            out = inner + entry_json + "\n]\n";
        }
    } else {
        out = "[\n" + entry_json + "\n]\n";
    }
    WriteFileText(index_path, out);
}

void ApplyScores(dc::HostCapabilityRecord& record, const dc::CpuScores& cpu,
                 const dc::GpuScores& gpu, const dc::MemoryScores& mem,
                 const dc::StorageScores& stor) {
    if (cpu.measured) {
        record.cpu.game_thread_score = cpu.game_thread;
        record.cpu.worker_score = cpu.worker;
        record.cpu.sustained_score = cpu.sustained;
    }
    if (gpu.measured) {
        record.gpu.raster_score = gpu.raster;
        record.gpu.compute_score = gpu.compute;
        record.gpu.rt_score = gpu.rt;
    }
    if (mem.measured) {
        record.memory.bandwidth_score = mem.bandwidth_gbps * 1000.0; // MB/ms basis
        record.memory.pressure_safe_bytes = mem.pressure_safe_bytes;
    }
    if (stor.measured && !record.storage.empty()) {
        auto& s = record.storage.front();
        s.read_seq_mbps = stor.read_seq_mbps;
        s.read_random_score = stor.read_random_score;
        s.latency_us_p50 = stor.latency_us_p50;
        s.latency_us_p95 = stor.latency_us_p95;
        s.latency_us_p99 = stor.latency_us_p99;
        s.queue_depth_tested = stor.queue_depth_tested;
        s.sustained_streaming_score = stor.sustained_streaming_score;
    }
}

int RunQualify(const std::string& mode, const std::string& profiles_dir,
               dc::HostCapabilityRecord& record) {
    dc::QualificationSet q;
    std::fprintf(stderr, "[qualify] running benchmarks (%s)...\n", mode.c_str());
    std::fflush(stderr);
    if (!dcwin::RunQualification(record, mode, q)) {
        std::fprintf(stderr, "fatal: qualification runner failed\n");
        return 1;
    }
    std::fprintf(stderr, "[qualify] runner complete; writing evidence...\n");
    std::fflush(stderr);

    // Deterministic extraction.
    dc::CpuScores cpu; dc::GpuScores gpu; dc::MemoryScores mem; dc::StorageScores stor;
    dc::ExtractScores(q, cpu, gpu, mem, stor);
    ApplyScores(record, cpu, gpu, mem, stor);

    record.qualification.measured = true;
    record.qualification.mode = mode;
    record.qualification.sustained_valid = q.sustained_valid;
    record.qualification.timestamp_utc = q.timestamp_utc;
    record.thermal.sustained_profile_valid = q.sustained_valid;

    // ---- Active presentation-path proof (DK0-M1I integration) ----
    // VRR active proof requires actual Present(0, ALLOW_TEARING) evidence,
    // which only dc-displayprobe can produce. Run it as a child process with
    // its own clean D3D12/DXGI context, parse its dc.display-probe/1 JSON with
    // the real parser (no substring checks), and keep the raw output as
    // immutable run evidence. The proof integrates into BOTH host and session
    // derivation inputs (ADR-0022: display.vrr_proven).
    std::string pending_probe_evidence;
    {
        std::fprintf(stderr, "[qualify] active presentation proof (dc-displayprobe)...\n");
        std::fflush(stderr);
        // Launch the sibling dc-displayprobe.exe (same directory as this exe).
        char exe_path[MAX_PATH] = {};
        std::string probe_cmd = "dc-displayprobe --json";
        if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) > 0) {
            std::string dir(exe_path);
            size_t slash = dir.find_last_of("/\\");
            if (slash != std::string::npos) {
                dir.resize(slash + 1);
                probe_cmd = "\"" + dir + "dc-displayprobe.exe\" --json";
            }
        }
        std::string probe_json;
        bool probe_ran = false;
        FILE* pipe = _popen(probe_cmd.c_str(), "rb");
        if (pipe) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, pipe)) > 0) probe_json.append(buf, n);
            int prc = _pclose(pipe);
            probe_ran = (prc == 0) && !probe_json.empty();
        }
        std::string probe_note;
        if (probe_ran) {
            std::string perr;
            auto root = dc::json::Parse(probe_json, perr);
            if (root && root->is_object()) {
                bool proven = root->find("vrr_actively_proven") &&
                              root->find("vrr_actively_proven")->as_bool(false);
                if (proven) {
                    for (auto& d : record.display) d.vrr.actively_proven = true;
                }
                probe_note = "displayprobe: vrr_actively_proven=" +
                             std::string(proven ? "true" : "false") +
                             "; proven_adapter=" +
                             (root->find("proven_adapter")
                                  ? root->find("proven_adapter")->as_string()
                                  : std::string());
            } else {
                probe_note = "displayprobe output unparseable: " + perr;
            }
        } else {
            probe_note = "dc-displayprobe not runnable (skipped; proof absent)";
        }
        std::fprintf(stderr, "[qualify] %s\n", probe_note.c_str());
        // Raw probe output preserved inside the run dir as immutable evidence.
        WriteFileText("evidence/qualification/displayprobe-latest.json", probe_json);
        pending_probe_evidence = probe_json; // stored into run dir below
    }

    // Profile derivation (deterministic; per-profile rejection reasons).
    std::vector<dc::ProfileDef> registry;
    std::string err;
    if (dc::LoadProfileRegistry(profiles_dir, registry, err)) {
        dc::HostDerivationInput in;
        in.cpu = cpu; in.gpu = gpu; in.memory = mem; in.storage = stor;
        in.vram_bytes = record.gpu.vram_bytes;
        in.ram_bytes = record.memory.system_bytes;
        for (const auto& d : record.display) {
            if (d.desktop_refresh_hz > in.max_display_refresh_hz) {
                in.max_display_refresh_hz = d.desktop_refresh_hz;
            }
            if (d.hdr.advanced_color_active) in.hdr_active = true;
            if (d.vrr.actively_proven) in.vrr_actively_proven = true;
        }
        auto derived = dc::DeriveProfiles(registry, in);
        for (auto& d : derived) {
            if (d.supported) {
                record.profiles_claimed.push_back({d.def.id, d.def.version});
            } else {
                record.profiles_rejected.push_back({d.def.id, d.reasons});
            }
        }

        // Session-experience derivation (DCX, ADR-0022): host results +
        // display mode/presentation truth from the discovery record.
        dc::SessionDerivationInput sin;
        sin.host_results = derived;
        sin.host_input = in;
        for (const auto& disp : record.display) {
            for (const auto& m : disp.modes) {
                sin.host_input.display_modes.push_back({m.width, m.height,
                                                         m.refresh_numerator,
                                                         m.refresh_denominator});
            }
            if (disp.hdr.advanced_color_active) sin.host_input.hdr_active = true;
            if (disp.vrr.actively_proven) sin.host_input.vrr_actively_proven = true;
        }
        auto sderived = dc::DeriveSessionProfiles(registry, sin);
        for (auto& d : sderived) {
            if (d.supported) {
                record.session_profiles_claimed.push_back({d.def.id, d.def.version});
            } else {
                record.session_profiles_rejected.push_back({d.def.id, d.reasons});
            }
        }
    } else {
        std::fprintf(stderr, "warning: profile registry not loaded: %s\n", err.c_str());
    }

    // Evidence: immutable run directory (ADR-0023) + convenience "latest"
    // pointers. Never overwrites a prior run.
    const std::string run_id = NewRunId(q.timestamp_utc, mode);
    const std::string run_dir = "evidence/qualification/runs/" + run_id;
    const std::string host_json = dc::ToJson(record, true);
    const std::string bench_json = dc::QualificationToJson(q, true);

    // Session profile JSON: reuse the host serializer's arrays via the record.
    std::string session_json = "{";
    session_json += "\n  \"schema\": \"dc.session-profiles/1\",";
    session_json += "\n  \"run_id\": \"" + run_id + "\",";
    session_json += "\n  \"claimed\": [";
    for (size_t i = 0; i < record.session_profiles_claimed.size(); ++i) {
        const auto& p = record.session_profiles_claimed[i];
        session_json += (i ? ",\n" : "\n") + std::string("    {\"id\": \"") + p.id + "\", \"version\": " + std::to_string(p.version) + "}";
    }
    session_json += "\n  ],\n  \"rejected\": [";
    for (size_t i = 0; i < record.session_profiles_rejected.size(); ++i) {
        const auto& p = record.session_profiles_rejected[i];
        session_json += (i ? ",\n" : "\n") + std::string("    {\"id\": \"") + p.id + "\", \"reasons\": [";
        for (size_t j = 0; j < p.reasons.size(); ++j) {
            session_json += (j ? ", " : "") + std::string("\"") + p.reasons[j] + "\"";
        }
        session_json += "]}";
    }
    session_json += "\n  ]\n}\n";

    // manifest.json: run identity + provenance (directive §6).
    std::string manifest = "{\n";
    manifest += "  \"schema\": \"dc.qualification-run/1\",\n";
    manifest += "  \"run_id\": \"" + run_id + "\",\n";
    manifest += "  \"mode\": \"" + mode + "\",\n";
    manifest += "  \"started_at_utc\": \"" + q.timestamp_utc + "\",\n";
    manifest += "  \"runtime_version\": \"" + record.build.runtime_version + "\",\n";
    manifest += "  \"os\": \"" + record.os.family + " " + record.os.version + " build " + std::to_string(record.os.build) + "\",\n";
    if (!record.gpu.model_name.empty()) {
        manifest += "  \"gpu\": \"" + record.gpu.model_name + "\",\n";
        manifest += "  \"gpu_driver\": \"" + record.gpu.driver_version + "\",\n";
    }
    manifest += "  \"sustained_valid\": " + std::string(q.sustained_valid ? "true" : "false") + ",\n";
    manifest += "  \"benchmarks\": " + std::to_string(q.benchmarks.size()) + ",\n";
    manifest += "  \"host_profiles_claimed\": " + std::to_string(record.profiles_claimed.size()) + ",\n";
    manifest += "  \"session_profiles_claimed\": " + std::to_string(record.session_profiles_claimed.size()) + "\n";
    manifest += "}\n";

    bool ok = true;
    ok = WriteFileText(run_dir + "/manifest.json", manifest) && ok;
    ok = WriteFileText(run_dir + "/host-capability.json", host_json) && ok;
    ok = WriteFileText(run_dir + "/benchmarks.json", bench_json) && ok;
    ok = WriteFileText(run_dir + "/session-profiles.json", session_json) && ok;
    if (!pending_probe_evidence.empty()) {
        ok = WriteFileText(run_dir + "/displayprobe.json", pending_probe_evidence) && ok;
    }

    // Index append (mutable by design; merge-safe).
    std::string idx = "{\n  \"run_id\": \"" + run_id + "\",\n  \"mode\": \"" + mode + "\",\n  \"started_at_utc\": \"" + q.timestamp_utc + "\",\n  \"sustained_valid\": " + std::string(q.sustained_valid ? "true" : "false") + "\n}";
    AppendRunIndex("evidence/qualification/index.json", idx);

    // Convenience latest pointer (regenerated each run; not evidence).
    WriteFileText("evidence/qualification/benchmarks.json", bench_json);
    WriteFileText("evidence/qualification/summary.json", host_json);
    WriteFileText("evidence/host-capability.json", host_json);

    std::printf("qualification (%s): %zu benchmarks, %zu claimed, %zu rejected\n",
                mode.c_str(), q.benchmarks.size(),
                record.profiles_claimed.size(), record.profiles_rejected.size());
    std::printf("run %s %s\n", run_dir.c_str(), ok ? "written" : "WRITE FAILED");
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    bool want_json = false, want_report = false;
    std::string out_path, profiles_dir = "formats/profiles";
    std::string qualify_mode;
    int schema_version = 2;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) want_json = true;
        else if (std::strcmp(argv[i], "--report") == 0) want_report = true;
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (std::strcmp(argv[i], "--schema-version") == 0 && i + 1 < argc) {
            schema_version = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--qualify") == 0 && i + 1 < argc) {
            qualify_mode = argv[++i];
        } else if (std::strcmp(argv[i], "--profiles") == 0 && i + 1 < argc) {
            profiles_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            PrintUsage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            PrintUsage();
            return 2;
        }
    }
    if (qualify_mode != "quick" && qualify_mode != "standard" && qualify_mode != "certification") {
        if (!qualify_mode.empty()) {
            std::fprintf(stderr, "invalid --qualify mode: %s\n", qualify_mode.c_str());
            return 2;
        }
    }
    if (schema_version != 1 && schema_version != 2) {
        std::fprintf(stderr, "invalid --schema-version: %d (expected 1 or 2)\n", schema_version);
        return 2;
    }
    if (!want_json && !want_report && qualify_mode.empty()) want_report = true;

    dc::IHostPlatform* host = dc::CreateHostPlatform();
    if (!host) {
        std::fprintf(stderr, "fatal: could not construct host platform\n");
        return 1;
    }

    dc::HostCapabilityRecord record;
    dc::Result r = host->QueryCapabilities(record);
    delete host;
    if (!r) {
        std::fprintf(stderr, "fatal: capability query failed: %s\n", r.message.c_str());
        return 1;
    }

    if (!qualify_mode.empty()) {
        int qrc = RunQualify(qualify_mode, profiles_dir, record);
        if (qrc != 0) return qrc;
    }

    std::string json = (schema_version == 1) ? dc::ToJsonV1(record, true)
                                             : dc::ToJson(record, true);

    if (!out_path.empty()) {
        if (!WriteFileText(out_path, json)) {
            std::fprintf(stderr, "fatal: cannot write %s\n", out_path.c_str());
            return 1;
        }
        std::fprintf(stderr, "wrote %s (%zu bytes)\n", out_path.c_str(), json.size());
    }

    if (want_json) {
        std::fputs(json.c_str(), stdout);
    }
    if (want_report) {
        PrintReport(record);
    }
    return 0;
}
