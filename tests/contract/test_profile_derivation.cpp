// test_profile_derivation.cpp — profile engine contract tests: boundary
// values, just-below/exactly-at/just-above thresholds, missing measurements,
// invalid inputs, determinism (byte-identical output for identical input).
#include "dc/qualification.hpp"
#include "dc/json.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace dc;

namespace {

ProfileDef MakeDef(const std::string& id, std::vector<ProfileRequirement> reqs) {
    ProfileDef d;
    d.id = id;
    d.version = 1;
    d.title = "test";
    d.requirements = std::move(reqs);
    return d;
}

HostDerivationInput FullyMeasuredInput() {
    HostDerivationInput in;
    in.cpu.measured = true;
    in.cpu.game_thread = 100000.0;
    in.cpu.sustained = 85000.0;
    in.cpu.single_thread = 40000.0;
    in.cpu.worker = 300000.0;
    in.gpu.measured = true;
    in.gpu.raster = 10000000000.0;
    in.gpu.compute = 20000000000.0;
    in.gpu.rt = 200000000.0;
    in.gpu.sustained = 7000000000.0;
    in.gpu.copy_gbps = 40.0;
    in.memory.measured = true;
    in.memory.bandwidth_gbps = 300.0;
    in.memory.pressure_safe_bytes = 12ull << 30;
    in.storage.measured = true;
    in.storage.read_seq_mbps = 3500.0;
    in.storage.latency_us_p99 = 800.0;
    in.vram_bytes = 12ull << 30;
    in.ram_bytes = 32ull << 30;
    in.max_display_refresh_hz = 60.0;
    in.hdr_active = false;
    in.vrr_actively_proven = false;
    return in;
}

void TestExactThresholdPasses() {
    auto def = MakeDef("DCP-2026-TEST", {{"cpu.game_thread_score", 100000.0}});
    auto out = DeriveProfiles({def}, FullyMeasuredInput());
    assert(out.size() == 1);
    assert(out[0].supported);
    assert(out[0].reasons.empty());
}

void TestJustBelowFails() {
    HostDerivationInput in = FullyMeasuredInput();
    in.cpu.game_thread = 99999.999;
    auto def = MakeDef("DCP-2026-TEST", {{"cpu.game_thread_score", 100000.0}});
    auto out = DeriveProfiles({def}, in);
    assert(!out[0].supported);
    assert(out[0].reasons.size() == 1);
    assert(out[0].reasons[0] == reason::kCpuGameThreadBelow);
}

void TestJustAbovePasses() {
    HostDerivationInput in = FullyMeasuredInput();
    in.cpu.game_thread = 100000.001;
    auto def = MakeDef("DCP-2026-TEST", {{"cpu.game_thread_score", 100000.0}});
    auto out = DeriveProfiles({def}, in);
    assert(out[0].supported);
}

void TestMissingMeasurementFailsWithNotMeasured() {
    HostDerivationInput in = FullyMeasuredInput();
    in.gpu.measured = false; // RT score unreachable
    auto def = MakeDef("DCP-2026-TEST", {{"gpu.rt_score", 1000.0}});
    auto out = DeriveProfiles({def}, in);
    assert(!out[0].supported);
    assert(out[0].reasons.size() == 1);
    assert(out[0].reasons[0] == reason::kNotMeasured);
}

void TestUnknownPathFails() {
    auto def = MakeDef("DCP-2026-TEST", {{"made.up.path", 1.0}});
    auto out = DeriveProfiles({def}, FullyMeasuredInput());
    assert(!out[0].supported);
    assert(out[0].reasons[0].rfind("UNKNOWN_REQUIREMENT_PATH:", 0) == 0);
}

void TestRtZeroMeansNotMeasured() {
    HostDerivationInput in = FullyMeasuredInput();
    in.gpu.rt = 0.0; // host could not measure RT
    auto def = MakeDef("DCP-2026-TEST", {{"gpu.rt_score", 1.0}});
    auto out = DeriveProfiles({def}, in);
    assert(!out[0].supported);
    assert(out[0].reasons[0] == reason::kNotMeasured);
}

void TestDisplayFlagsAsRequirements() {
    auto def = MakeDef("DCP-2026-HDRTEST", {{"display.hdr_active", 1.0}});
    HostDerivationInput in = FullyMeasuredInput();
    in.hdr_active = true;
    assert(DeriveProfiles({def}, in)[0].supported);
    in.hdr_active = false;
    auto out = DeriveProfiles({def}, in);
    assert(!out[0].supported);
    assert(out[0].reasons[0] == reason::kHdrMissing);
}

void TestMultipleRequirementsCollectAllReasons() {
    HostDerivationInput in = FullyMeasuredInput();
    in.cpu.game_thread = 1.0;   // below
    in.gpu.rt = 1.0;            // below
    in.hdr_active = false;      // flag off
    auto def = MakeDef("DCP-2026-TEST", {
        {"cpu.game_thread_score", 100000.0},
        {"gpu.rt_score", 100000.0},
        {"display.hdr_active", 1.0},
    });
    auto out = DeriveProfiles({def}, in);
    assert(!out[0].supported);
    assert(out[0].reasons.size() == 3);
    assert(out[0].reasons[0] == reason::kCpuGameThreadBelow);
    assert(out[0].reasons[1] == reason::kGpuRtScoreBelow);
    assert(out[0].reasons[2] == reason::kHdrMissing);
}

void TestDeterminism() {
    auto def = MakeDef("DCP-2026-TEST", {{"cpu.game_thread_score", 50000.0}});
    std::vector<ProfileDef> reg{def};
    auto a = DeriveProfiles(reg, FullyMeasuredInput());
    auto b = DeriveProfiles(reg, FullyMeasuredInput());
    assert(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        assert(a[i].supported == b[i].supported);
        assert(a[i].reasons == b[i].reasons);
    }
}

void TestRegistryLoader() {
    // Loader reads the real DCP registry from formats/profiles.
    std::vector<ProfileDef> reg;
    std::string err;
    bool ok = LoadProfileRegistry("formats/profiles", reg, err);
    assert(ok);
    assert(reg.size() >= 8);
    bool saw_base = false;
    for (const auto& d : reg) {
        if (d.id == "DCP-2026-BASE") saw_base = true;
        assert(d.id.rfind("DCP-", 0) == 0);
        assert(!d.requirements.empty());
    }
    assert(saw_base);
    // Missing directory fails cleanly.
    std::vector<ProfileDef> reg2;
    assert(!LoadProfileRegistry("formats/does-not-exist", reg2, err));
    assert(!err.empty());
}

void TestExtractScoresRejectsBadSamples() {
    QualificationSet q;
    q.measured = true;
    BenchmarkRecord rec;
    rec.benchmark_id = "cpu.game-thread";
    rec.state = BenchState::Complete;
    rec.iterations = 100;
    rec.samples = {{1.0, 1.0}, {1.0, 1.0}}; // only 2 samples: rejected
    q.benchmarks.push_back(rec);
    CpuScores cpu; GpuScores gpu; MemoryScores mem; StorageScores stor;
    bool any = ExtractScores(q, cpu, gpu, mem, stor);
    assert(!any);
    assert(!cpu.measured);

    // NaN sample: rejected.
    q.benchmarks[0].samples = {{1.0, 1.0}, {1.0, 1.0}, {std::nan(""), 1.0}};
    bool any2 = ExtractScores(q, cpu, gpu, mem, stor);
    assert(!any2);
    assert(!cpu.measured);

    // 3 clean samples: measured.
    q.benchmarks[0].samples = {{1.0, 1.0}, {1.0, 1.0}, {1.0, 1.0}};
    bool any3 = ExtractScores(q, cpu, gpu, mem, stor);
    assert(any3);
    assert(cpu.measured);
    assert(cpu.game_thread > 0.0);
}

// ---- Session derivation (DCX, ADR-0022) -----------------------------------

void TestSessionModePredicateConjunctive() {
    // Independent maxima must NOT combine across modes: a 4K@30 mode plus a
    // 1080p@240 mode must NOT satisfy a 3840x2160@120 mode predicate.
    HostDerivationInput host = FullyMeasuredInput();
    host.display_modes = {{3840, 2160, 30000, 1000},   // 4K @ 30 (rational 30.000)
                          {1920, 1080, 240000, 1000}}; // 1080p @ 240
    SessionDerivationInput sin;
    sin.host_input = host;
    auto host_def = MakeDef("DCP-2026-RENDER-UHD120", {{"gpu.raster_score", 1000.0}});
    sin.host_results = DeriveProfiles({host_def}, host);
    auto dcx = MakeDef("DCX-UHD120", {});
    ProfileRequirement mode_req;
    mode_req.mode_min_width = 3840;
    mode_req.mode_min_height = 2160;
    mode_req.mode_min_refresh_hz = 120.0;
    ProfileRequirement hp_req;
    hp_req.host_profile = "DCP-2026-RENDER-UHD120";
    dcx.requirements = {hp_req, mode_req};
    dcx.kind = "session";
    auto out = DeriveSessionProfiles({dcx}, sin);
    assert(out.size() == 1);
    assert(!out[0].supported);
    bool has_mode_reason = false;
    for (const auto& rc : out[0].reasons)
        if (rc == reason::kDisplayModeUnavailable) has_mode_reason = true;
    assert(has_mode_reason);
}

void TestSessionModeExactRationalBoundaries() {
    HostDerivationInput host = FullyMeasuredInput();
    // 119.997 Hz (239994/2000... use 119997/1000): must FAIL a 120 Hz bar.
    host.display_modes = {{3840, 2160, 119997, 1000}};
    SessionDerivationInput sin;
    sin.host_input = host;
    auto dcx = MakeDef("DCX-UHD120", {});
    ProfileRequirement mode_req;
    mode_req.mode_min_width = 3840;
    mode_req.mode_min_height = 2160;
    mode_req.mode_min_refresh_hz = 120.0;
    dcx.requirements = {mode_req};
    dcx.kind = "session";
    auto out = DeriveSessionProfiles({dcx}, sin);
    assert(out.size() == 1 && !out[0].supported);

    // Exactly 120.000 (240000/2000): must PASS (inclusive).
    sin.host_input.display_modes = {{3840, 2160, 240000, 2000}};
    auto out2 = DeriveSessionProfiles({dcx}, sin);
    assert(out2.size() == 1 && out2[0].supported);

    // Just above: 240001/2000 = 120.0005 Hz: PASS.
    sin.host_input.display_modes = {{3840, 2160, 240001, 2000}};
    auto out3 = DeriveSessionProfiles({dcx}, sin);
    assert(out3.size() == 1 && out3[0].supported);
}

void TestSessionHostProfileReference() {
    HostDerivationInput host = FullyMeasuredInput();
    host.display_modes = {{3840, 2160, 240000, 1000}};
    SessionDerivationInput sin;
    sin.host_input = host;
    // Host profile SATISFIED: DCX passes.
    auto ok_def = MakeDef("DCP-2026-RENDER-UHD120", {{"gpu.raster_score", 1000.0}});
    sin.host_results = DeriveProfiles({ok_def}, host);
    auto dcx = MakeDef("DCX-UHD120", {});
    ProfileRequirement hp_req;
    hp_req.host_profile = "DCP-2026-RENDER-UHD120";
    dcx.requirements = {hp_req};
    dcx.kind = "session";
    auto out = DeriveSessionProfiles({dcx}, sin);
    assert(out.size() == 1 && out[0].supported);

    // Host profile UNSATISFIED (threshold above measurement): DCX fails with
    // HOST_PROFILE_UNSATISFIED, never a pass.
    auto bad_def = MakeDef("DCP-2026-RENDER-UHD120", {{"gpu.raster_score", 2e10}});
    sin.host_results = DeriveProfiles({bad_def}, host);
    auto out2 = DeriveSessionProfiles({dcx}, sin);
    assert(out2.size() == 1 && !out2[0].supported);
    bool has_hp_reason = false;
    for (const auto& rc : out2[0].reasons)
        if (rc == reason::kHostProfileUnsatisfied) has_hp_reason = true;
    assert(has_hp_reason);

    // Referenced DCP missing from the host results: also UNSATISFIED.
    sin.host_results = {};
    auto out3 = DeriveSessionProfiles({dcx}, sin);
    assert(out3.size() == 1 && !out3[0].supported);
}

void TestSessionHdrAndVrrStates() {
    HostDerivationInput host = FullyMeasuredInput();
    SessionDerivationInput sin;
    sin.host_input = host;
    auto host_def = MakeDef("DCP-2026-HDR", {{"gpu.raster_score", 1000.0}});
    sin.host_results = DeriveProfiles({host_def}, host);

    auto dcx_hdr = MakeDef("DCX-HDR", {});
    ProfileRequirement hdr_req;
    hdr_req.path = "display.hdr_active";
    hdr_req.value = 1.0;
    dcx_hdr.requirements = {hdr_req};
    dcx_hdr.kind = "session";
    auto out = DeriveSessionProfiles({dcx_hdr}, sin);
    assert(out.size() == 1 && !out[0].supported);
    assert(out[0].reasons[0] == reason::kHdrInactive);

    sin.host_input.hdr_active = true;
    auto out2 = DeriveSessionProfiles({dcx_hdr}, sin);
    assert(out2.size() == 1 && out2[0].supported);
}

void TestHostDerivationSkipsSessionProfiles() {
    auto def = MakeDef("DCX-UHD120", {});
    def.kind = "session";
    ProfileRequirement hp_req;
    hp_req.host_profile = "DCP-2026-RENDER-UHD120";
    def.requirements = {hp_req};
    auto out = DeriveProfiles({def}, FullyMeasuredInput());
    assert(out.empty()); // host derivation never emits session claims
}

} // namespace

int main() {
    TestExactThresholdPasses();
    TestJustBelowFails();
    TestJustAbovePasses();
    TestMissingMeasurementFailsWithNotMeasured();
    TestUnknownPathFails();
    TestRtZeroMeansNotMeasured();
    TestDisplayFlagsAsRequirements();
    TestMultipleRequirementsCollectAllReasons();
    TestDeterminism();
    TestRegistryLoader();
    TestExtractScoresRejectsBadSamples();
    TestSessionModePredicateConjunctive();
    TestSessionModeExactRationalBoundaries();
    TestSessionHostProfileReference();
    TestSessionHdrAndVrrStates();
    TestHostDerivationSkipsSessionProfiles();
    std::printf("profile derivation tests: all passed\n");
    return 0;
}
