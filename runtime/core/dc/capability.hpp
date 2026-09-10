// dc/capability.hpp — HostCapabilityRecord (DC-CANON-001 §10; ADR-0007, ADR-0021)
// POD model mirroring formats/schemas/host-capability.schema.json (dc.host-capability/2).
// Discovery scores of 0.0 mean "unmeasured (discovery-only pass)"; measurement
// state is explicit via qualification.measured (ADR-0021 semantic break).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dc {

struct OsInfo {
    std::string family;     // "windows" | "consoleos"
    std::string version;    // "11", "10", ...
    uint32_t build = 0;
    std::string edition;
};

struct CpuInfo {
    std::string arch = "x86_64";
    std::string model_name;
    uint32_t physical_cores = 0;
    uint32_t logical_cores = 0;
    double base_clock_hz = 0.0;
    double max_clock_hz = 0.0;
    double game_thread_score = 0.0; // unmeasured = 0
    double worker_score = 0.0;
    double sustained_score = 0.0;
};

struct D3D12Info {
    bool available = false;
    std::string feature_level;
    uint64_t dedicated_vram_bytes = 0;
    uint64_t shared_vram_bytes = 0;
    double raytracing_tier = 0.0; // 0 = none/unreported
    bool mesh_shader = false;
    bool sampler_feedback = false;
    bool directstorage_gpu_decompression = false; // measured later; false is honest
};

struct VulkanInfo {
    bool available = false;
    std::string status = "not_probed"; // ok | loader_not_found | entry_point_missing | instance_creation_failed | no_physical_devices | enum_failed
    std::string api_version;
    std::string device_name;
    bool ray_query = false;
    bool mesh_shader = false;
    uint32_t queue_families = 0;   // discovered via vkEnumeratePhysicalDevices
    uint32_t memory_heaps = 0;
};

// DirectStorage capability discovery (DK0-M1E; ledger §18).
struct DirectStorageInfo {
    bool runtime_available = false; // storage.ral.dll + DStorageGetFactory
    std::string version;            // "1.3.0.0" style from version resource
    bool gpu_decompression_supported = false; // verified only via SDK surface
    bool gdeflate_supported = false;
    std::string zstd_status = "unavailable"; // stable | preview | unavailable
};

struct GpuInfo {
    std::string vendor = "unknown"; // nvidia|amd|intel|other|unknown
    std::string model_name;
    std::string driver_version;
    std::vector<std::string> api; // "d3d12", "vulkan"
    D3D12Info d3d12;
    VulkanInfo vulkan;
    uint64_t vram_bytes = 0;
    double raster_score = 0.0;
    double compute_score = 0.0;
    double rt_score = 0.0;
    double matrix_score = 0.0;
};

struct MemoryInfo {
    uint64_t system_bytes = 0;
    uint64_t available_bytes = 0;
    double bandwidth_score = 0.0;
    uint64_t pressure_safe_bytes = 0;
};

struct StorageDeviceInfo {
    std::string id;
    std::string klass; // JSON "class": DC_STORAGE_LEGACY | ... | DC_STORAGE_UNKNOWN
    std::string media_type;
    std::string bus_type;
    uint64_t capacity_bytes = 0;
    uint64_t free_bytes = 0;
    double read_seq_mbps = 0.0;
    double read_random_score = 0.0;
    double latency_us_p50 = 0.0;
    double latency_us_p95 = 0.0;
    double latency_us_p99 = 0.0;
    uint32_t queue_depth_tested = 0;
    double sustained_streaming_score = 0.0;
    bool gpu_decompression = false;
};

struct DisplayMode {
    uint32_t width = 0, height = 0;
    double refresh_hz = 0.0;      // derived: numerator / denominator
    uint32_t refresh_numerator = 0;   // rational truth (ADR-0022): CDS
    uint32_t refresh_denominator = 0; // reports integer Hz => denominator 1
    uint32_t bits_per_color = 0;
};

struct HdrInfo {
    bool advanced_color_supported = false;
    bool advanced_color_active = false;
    bool user_enabled = false;      // HDR explicitly enabled by the user
    bool wide_color_gamut = false;
    bool metadata_available = false; // true when luminance/primaries were obtained
    std::string advanced_color_kind = "SDR"; // SDR | WCG | HDR
    uint32_t bits_per_color = 8;
    double min_luminance_nits = 0.0;
    double max_luminance_nits = 0.0;
    double max_full_frame_luminance_nits = 0.0;
    double sdr_white_level_nits = 0.0;
    std::vector<std::string> metadata_formats; // "hdr10", "hdr10plus"
    double red_primary[2] = {0.0, 0.0};   // x, y in [0,1]
    double green_primary[2] = {0.0, 0.0};
    double blue_primary[2] = {0.0, 0.0};
    double white_point[2] = {0.0, 0.0};
};

// VRR three-state model (ledger §21; C10 — never collapse to one boolean):
//   supported       = OS reports VRR-capable environment
//   path_compatible = flip-model + tearing presentation path constructible
//   actively_proven = dc-displayprobe measured the path working
struct VrrInfo {
    bool supported = false;
    bool path_compatible = false;
    bool actively_proven = false;
    double min_refresh_hz = 0.0;
    double max_refresh_hz = 0.0;
};

struct DisplayDeviceInfo {
    std::string id;
    std::string name;
    bool primary = false;
    bool active = false;
    uint32_t width = 0, height = 0;
    double desktop_refresh_hz = 0.0;
    std::vector<DisplayMode> modes;
    HdrInfo hdr;
    VrrInfo vrr;
};

struct AudioEndpoint {
    std::string id;
    std::string name;
    std::string kind = "other"; // hdmi|analog|usb|bluetooth|virtual|other
    bool default_output = false;
    uint32_t channels = 2;
    std::vector<uint32_t> sample_rates_hz;
    bool spatial_capable = false;
    bool microphone = false;
};

struct InputDevice {
    std::string id;
    std::string kind = "gamepad"; // gamepad|wheel|flightstick|arcade_stick|touch|other
    std::string name;
    std::string backend = "rawinput"; // gameinput|sdl3|rawinput|other
    std::string connection = "unknown";
    int battery_percent = -1; // -1 = unknown/wired
    bool haptics = false;
    bool gyro = false;
    bool touch_surface = false;
    bool adaptive_triggers = false;
};

struct ThermalInfo {
    bool sustained_profile_valid = false; // false until sustained qualification ran
    std::string power_scheme;
    std::string note;
};

// Build identity for evidence provenance (ADR-0018).
struct BuildInfo {
    std::string git_hash;      // may be "unknown" outside a git checkout
    std::string build_config;  // "Release" | "Debug"
    std::string runtime_version; // platform runtime semver
};

// Qualification payload embedded in /2 records (ADR-0021). Raw benchmark
// records also ship as separate evidence files; this summary is authoritative
// for measurement state.
struct QualificationInfo {
    bool measured = false;      // false = discovery-only pass
    std::string mode = "none";  // none | quick | standard | certification
    bool sustained_valid = false;
    std::string timestamp_utc;
};

struct ProfileClaim {
    std::string id;      // "DCP-2026-BASE"
    uint32_t version = 1;
};

struct ProfileRejection {
    std::string id;
    std::vector<std::string> reasons; // stable reason codes (qualification.hpp)
};

struct TrustInfo {
    bool secure_boot = false;
    bool tpm = false;
    std::string tpm_version;
    bool measured_boot_available = false;
};

struct HostCapabilityRecord {
    std::string schema_version = "dc.host-capability/2";
    std::string host_id;
    std::string generated_utc;
    BuildInfo build;
    OsInfo os;
    CpuInfo cpu;
    GpuInfo gpu;
    MemoryInfo memory;
    std::vector<StorageDeviceInfo> storage;
    std::vector<DisplayDeviceInfo> display;
    std::vector<AudioEndpoint> audio;
    std::vector<InputDevice> input;
    ThermalInfo thermal;
    TrustInfo trust;
    DirectStorageInfo directstorage;
    QualificationInfo qualification;
    std::vector<ProfileClaim> profiles_claimed;     // DCP host profiles only (ADR-0022)
    std::vector<ProfileRejection> profiles_rejected;
    std::vector<ProfileClaim> session_profiles_claimed;     // DCX session (ADR-0022)
    std::vector<ProfileRejection> session_profiles_rejected;
};

// Serialize to JSON (schema dc.host-capability/2). Deterministic field order.
std::string ToJson(const HostCapabilityRecord& record, bool pretty = true);

// Serialize in legacy dc.host-capability/1 shape (ADR-0021 compat mapping):
// new sections dropped, unmeasured scores collapse to 0, profiles[] as ids.
std::string ToJsonV1(const HostCapabilityRecord& record, bool pretty = true);

} // namespace dc
