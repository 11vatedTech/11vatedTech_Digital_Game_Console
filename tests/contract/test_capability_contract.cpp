// test_capability_contract.cpp — contract tests for dc.host-capability/2
// (Gate P0 build health; canon §42). Real JSON parsing (not substring-only);
// substring assertions retained as belt-and-suspenders.
#include "dc/capability.hpp"
#include "dc/json.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using dc::HostCapabilityRecord;
using dc::json::Value;

namespace {

void TestSchemaVersion() {
    HostCapabilityRecord r;
    assert(r.schema_version == "dc.host-capability/2");
}

HostCapabilityRecord PopulatedRecord() {
    HostCapabilityRecord r;
    r.host_id = "test-host";
    r.generated_utc = "2026-09-06T00:00:00Z";
    r.os.family = "windows";
    r.os.version = "11";
    r.os.build = 26200;
    r.os.edition = "test";
    r.cpu.logical_cores = 24;
    r.cpu.physical_cores = 24;
    r.gpu.vendor = "nvidia";
    r.gpu.model_name = "Test GPU";
    r.gpu.api.push_back("d3d12");
    r.memory.system_bytes = 32ull << 30;

    dc::StorageDeviceInfo s;
    s.id = "disk0";
    s.klass = "DC_STORAGE_UNKNOWN";
    r.storage.push_back(s);
    return r;
}

void TestV2RoundTripParses() {
    HostCapabilityRecord r = PopulatedRecord();
    const std::string json = dc::ToJson(r, true);

    // Must parse with the strict parser (not just substring-match).
    std::string err;
    auto v = dc::json::Parse(json, err);
    assert(v != nullptr);
    assert(v->find("schema")->as_string() == "dc.host-capability/2");
    assert(v->find("host_id")->as_string() == "test-host");
    // v2-only sections exist.
    assert(v->find("build") != nullptr);
    assert(v->find("directstorage") != nullptr);
    assert(v->find("qualification") != nullptr);
    assert(v->find("profiles_claimed") != nullptr);
    assert(v->find("profiles_rejected") != nullptr);
    // v2 qualification defaults: discovery-only.
    assert(v->find("qualification")->find("measured")->as_bool() == false);
    assert(v->find("qualification")->find("mode")->as_string() == "none");
    // Display VRR has all three states.
    const auto& display = v->find("display")->as_array();
    assert(!display.empty());
    const Value* vrr = display[0].find("vrr");
    assert(vrr != nullptr);
    assert(vrr->find("path_compatible") != nullptr);
    assert(vrr->find("actively_proven") != nullptr);
}

void TestV2KeyFacts() {
    HostCapabilityRecord r = PopulatedRecord();
    r.gpu.raster_score = 12345678.0;
    r.profiles_claimed.push_back({"DCP-2026-BASE", 1});
    dc::ProfileRejection rej;
    rej.id = "DCP-2026-RT2";
    rej.reasons.push_back("GPU_RT_SCORE_BELOW_REQUIREMENT");
    r.profiles_rejected.push_back(rej);
    r.directstorage.runtime_available = true;
    r.directstorage.zstd_status = "preview";

    const std::string json = dc::ToJson(r, false);
    // Substring spot checks (in addition to structural checks elsewhere).
    assert(json.find("\"raster_score\": 12345678") != std::string::npos);
    assert(json.find("\"zstd_status\": \"preview\"") != std::string::npos);
    std::string err;
    auto v = dc::json::Parse(json, err);
    assert(v != nullptr);
    assert(v->find("profiles_claimed")->as_array()[0].find("id")->as_string() == "DCP-2026-BASE");
    assert(v->find("profiles_rejected")->as_array()[0].find("reasons")->as_array()[0].as_string()
           == "GPU_RT_SCORE_BELOW_REQUIREMENT");
}

void TestUnmeasuredDefaultsHonest() {
    HostCapabilityRecord r;
    // C10: an unmeasured field must be 0/false, never a claimed value.
    assert(r.cpu.game_thread_score == 0.0);
    assert(r.gpu.raster_score == 0.0);
    assert(r.gpu.d3d12.directstorage_gpu_decompression == false);
    assert(r.thermal.sustained_profile_valid == false);
    assert(r.profiles_claimed.empty());
    assert(r.qualification.measured == false);
    assert(r.qualification.mode == "none");
    assert(r.directstorage.zstd_status == "unavailable");
}

void TestV1CompatMapping() {
    HostCapabilityRecord r = PopulatedRecord();
    r.profiles_claimed.push_back({"DCP-2026-BASE", 1});
    const std::string v1 = dc::ToJsonV1(r, true);

    std::string err;
    auto v = dc::json::Parse(v1, err);
    assert(v != nullptr);
    assert(v->find("schema")->as_string() == "dc.host-capability/1");
    // /1 shape: no build/directstorage/qualification/profiles_claimed sections.
    assert(v->find("build") == nullptr);
    assert(v->find("directstorage") == nullptr);
    assert(v->find("qualification") == nullptr);
    assert(v->find("profiles_claimed") == nullptr);
    // profiles is a bare id array in /1.
    const auto& profiles = v->find("profiles")->as_array();
    assert(profiles.size() == 1);
    assert(profiles[0].as_string() == "DCP-2026-BASE");
    // No VRR three-state fields in /1.
    const auto& display = v->find("display")->as_array();
    assert(!display.empty());
    assert(display[0].find("vrr")->find("path_compatible") == nullptr);
    // Storage has no /2-only fields.
    const auto& storage = v->find("storage")->as_array();
    assert(!storage.empty());
    assert(storage[0].find("queue_depth_tested") == nullptr);
}

void TestJsonEscaping() {
    HostCapabilityRecord r;
    r.host_id = "host\"with\\special\nchars";
    const std::string json = dc::ToJson(r, false);
    assert(json.find("\\\"with\\\\special\\nchars") != std::string::npos);
    std::string err;
    auto v = dc::json::Parse(json, err);
    assert(v != nullptr);
    assert(v->find("host_id")->as_string() == "host\"with\\special\nchars");
}

void TestDisplayAndAudioEntries() {
    HostCapabilityRecord r;
    dc::DisplayDeviceInfo d;
    d.id = "display0";
    d.name = "Test TV";
    d.primary = true;
    d.width = 3840;
    d.height = 2160;
    d.desktop_refresh_hz = 120;
    d.hdr.advanced_color_supported = true;
    d.hdr.advanced_color_active = true;
    d.hdr.advanced_color_kind = "HDR";
    d.hdr.metadata_available = true;
    d.hdr.max_luminance_nits = 1000.0;
    d.hdr.red_primary[0] = 0.708;
    d.hdr.red_primary[1] = 0.292;
    d.hdr.metadata_formats.push_back("hdr10");
    d.vrr.supported = true;
    d.vrr.path_compatible = true;
    d.vrr.actively_proven = true;
    d.modes.push_back({3840, 2160, 120.0, 10});
    r.display.push_back(d);

    dc::AudioEndpoint a;
    a.id = "audio0";
    a.name = "TV (HDMI)";
    a.kind = "hdmi";
    a.default_output = true;
    a.channels = 2;
    a.sample_rates_hz.push_back(48000);
    r.audio.push_back(a);

    const std::string json = dc::ToJson(r, true);
    std::string err;
    auto v = dc::json::Parse(json, err);
    assert(v != nullptr);
    const auto& display = v->find("display")->as_array();
    const Value& d0 = display[0];
    assert(d0.find("hdr")->find("advanced_color_kind")->as_string() == "HDR");
    assert(d0.find("hdr")->find("max_luminance_nits")->as_number() == 1000.0);
    assert(d0.find("hdr")->find("red_primary")->as_array()[0].as_number() == 0.708);
    assert(d0.find("hdr")->find("metadata_formats")->as_array()[0].as_string() == "hdr10");
    assert(d0.find("vrr")->find("actively_proven")->as_bool() == true);
    const auto& audio = v->find("audio")->as_array();
    assert(audio[0].find("default_output")->as_bool() == true);
}

void TestDerivationWritesReasonCodes() {
    // End-to-end shape check: rejected profiles serialize their reason codes.
    HostCapabilityRecord r = PopulatedRecord();
    dc::ProfileRejection rej;
    rej.id = "DCP-2026-4K120";
    rej.reasons.push_back("DISPLAY_REFRESH_TOO_LOW");
    rej.reasons.push_back("GPU_SUSTAINED_SCORE_BELOW_REQUIREMENT");
    r.profiles_rejected.push_back(rej);
    const std::string json = dc::ToJson(r, false);
    assert(json.find("DISPLAY_REFRESH_TOO_LOW") != std::string::npos);
    std::string err;
    auto v = dc::json::Parse(json, err);
    assert(v != nullptr);
    const auto& rej_arr = v->find("profiles_rejected")->as_array();
    assert(rej_arr[0].find("reasons")->as_array().size() == 2);
}

} // namespace

int main() {
    TestSchemaVersion();
    TestV2RoundTripParses();
    TestV2KeyFacts();
    TestUnmeasuredDefaultsHonest();
    TestV1CompatMapping();
    TestJsonEscaping();
    TestDisplayAndAudioEntries();
    TestDerivationWritesReasonCodes();
    std::printf("capability contract tests: all passed\n");
    return 0;
}
