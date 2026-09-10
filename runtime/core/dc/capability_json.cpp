// capability_json.cpp — deterministic JSON serialization of HostCapabilityRecord
// (v2 per ADR-0021; ToJsonV1 provides the documented /1 compat mapping).
#include "dc/capability.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace dc {

namespace {

void EscapeInto(const std::string& in, std::string& out) {
    for (unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
}

std::string Q(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    EscapeInto(s, out);
    out += '"';
    return out;
}

std::string Num(double v) {
    if (std::isfinite(v)) {
        if (v == static_cast<long long>(v) && std::fabs(v) < 1e15) {
            return std::to_string(static_cast<long long>(v));
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f", v);
        std::string s(buf);
        if (s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.pop_back();
        }
        return s;
    }
    return "0";
}

class Writer {
public:
    explicit Writer(bool pretty) : pretty_(pretty) {}

    void Key(const std::string& k) {
        Indent();
        body_ += Q(k) + ": ";
    }
    void Str(const std::string& v) { body_ += Q(v) + ","; }
    void Raw(const std::string& v) { body_ += v + ","; }
    void Bool(bool v) { body_ += v ? "true," : "false,"; }

    void ObjOpen() { body_ += "{"; depth_++; }
    void ObjClose() {
        depth_--;
        TrimTrailingComma();
        Indent();
        body_ += "},";
    }
    void ArrOpen() { body_ += "["; depth_++; }
    void ArrClose() {
        depth_--;
        TrimTrailingComma();
        Indent();
        body_ += "],";
    }

    void BeginKey(const std::string& k) { Key(k); }
    void ObjOpenAfterKey() { ObjOpen(); }
    void ArrOpenAfterKey() { ArrOpen(); }

    std::string Finish() {
        TrimTrailingComma();
        body_ += "\n}\n"; // close the root object
        return body_;
    }

private:
    void Indent() {
        if (pretty_) {
            body_ += '\n';
            body_.append(static_cast<size_t>(depth_) * 2, ' ');
        }
    }
    void TrimTrailingComma() {
        size_t end = body_.size();
        while (end > 0 && (body_[end - 1] == ' ' || body_[end - 1] == '\n')) --end;
        if (end > 0 && body_[end - 1] == ',') body_.resize(end - 1);
    }

    bool pretty_ = false;
    int depth_ = 0;
    std::string body_ = "{";
};

void WritePairArr(Writer& w, const double (&xy)[2]) {
    w.ArrOpenAfterKey();
    w.Raw(Num(xy[0]));
    w.Raw(Num(xy[1]));
    w.ArrClose();
}

void WriteStorage(Writer& w, const StorageDeviceInfo& s) {
    w.ObjOpen();
    w.Key("id"); w.Str(s.id);
    w.Key("class"); w.Str(s.klass);
    w.Key("media_type"); w.Str(s.media_type);
    w.Key("bus_type"); w.Str(s.bus_type);
    w.Key("capacity_bytes"); w.Raw(std::to_string(s.capacity_bytes));
    w.Key("free_bytes"); w.Raw(std::to_string(s.free_bytes));
    w.Key("read_seq_mbps"); w.Raw(Num(s.read_seq_mbps));
    w.Key("read_random_score"); w.Raw(Num(s.read_random_score));
    w.Key("latency_us_p50"); w.Raw(Num(s.latency_us_p50));
    w.Key("latency_us_p95"); w.Raw(Num(s.latency_us_p95));
    w.Key("latency_us_p99"); w.Raw(Num(s.latency_us_p99));
    w.Key("queue_depth_tested"); w.Raw(std::to_string(s.queue_depth_tested));
    w.Key("sustained_streaming_score"); w.Raw(Num(s.sustained_streaming_score));
    w.Key("gpu_decompression"); w.Bool(s.gpu_decompression);
    w.ObjClose();
}

void WriteDisplay(Writer& w, const DisplayDeviceInfo& d, bool v1_shape) {
    w.ObjOpen();
    w.Key("id"); w.Str(d.id);
    w.Key("name"); w.Str(d.name);
    w.Key("primary"); w.Bool(d.primary);
    w.Key("active"); w.Bool(d.active);
    w.Key("resolution"); w.Raw("{\"width\": " + std::to_string(d.width) +
                               ", \"height\": " + std::to_string(d.height) + "}");
    w.Key("desktop_refresh_hz"); w.Raw(Num(d.desktop_refresh_hz));

    w.BeginKey("modes"); w.ArrOpenAfterKey();
    for (const auto& m : d.modes) {
        w.ObjOpen();
        w.Key("width"); w.Raw(std::to_string(m.width));
        w.Key("height"); w.Raw(std::to_string(m.height));
        w.Key("refresh_numerator"); w.Raw(std::to_string(m.refresh_numerator));
        w.Key("refresh_denominator"); w.Raw(std::to_string(m.refresh_denominator));
        w.Key("refresh_hz"); w.Raw(Num(m.refresh_hz));
        w.Key("bits_per_color"); w.Raw(std::to_string(m.bits_per_color));
        w.ObjClose();
    }
    w.ArrClose();

    w.Key("hdr"); w.ObjOpen();
    w.Key("advanced_color_supported"); w.Bool(d.hdr.advanced_color_supported);
    w.Key("advanced_color_active"); w.Bool(d.hdr.advanced_color_active);
    w.Key("user_enabled"); w.Bool(d.hdr.user_enabled);
    w.Key("wide_color_gamut"); w.Bool(d.hdr.wide_color_gamut);
    w.Key("metadata_available"); w.Bool(d.hdr.metadata_available);
    w.Key("advanced_color_kind"); w.Str(d.hdr.advanced_color_kind);
    w.Key("bits_per_color"); w.Raw(std::to_string(d.hdr.bits_per_color));
    w.Key("min_luminance_nits"); w.Raw(Num(d.hdr.min_luminance_nits));
    w.Key("max_luminance_nits"); w.Raw(Num(d.hdr.max_luminance_nits));
    w.Key("max_full_frame_luminance_nits"); w.Raw(Num(d.hdr.max_full_frame_luminance_nits));
    w.Key("sdr_white_level_nits"); w.Raw(Num(d.hdr.sdr_white_level_nits));
    if (!v1_shape) {
        w.BeginKey("metadata_formats"); w.ArrOpenAfterKey();
        for (const auto& f : d.hdr.metadata_formats) w.Str(f);
        w.ArrClose();
        w.Key("red_primary"); WritePairArr(w, d.hdr.red_primary);
        w.Key("green_primary"); WritePairArr(w, d.hdr.green_primary);
        w.Key("blue_primary"); WritePairArr(w, d.hdr.blue_primary);
        w.Key("white_point"); WritePairArr(w, d.hdr.white_point);
    }
    w.ObjClose();

    w.Key("vrr"); w.ObjOpen();
    w.Key("supported"); w.Bool(d.vrr.supported);
    if (!v1_shape) {
        w.Key("path_compatible"); w.Bool(d.vrr.path_compatible);
        w.Key("actively_proven"); w.Bool(d.vrr.actively_proven);
    }
    w.Key("min_refresh_hz"); w.Raw(Num(d.vrr.min_refresh_hz));
    w.Key("max_refresh_hz"); w.Raw(Num(d.vrr.max_refresh_hz));
    w.ObjClose();

    w.ObjClose();
}

void WriteAudio(Writer& w, const AudioEndpoint& a) {
    w.ObjOpen();
    w.Key("id"); w.Str(a.id);
    w.Key("name"); w.Str(a.name);
    w.Key("kind"); w.Str(a.kind);
    w.Key("default_output"); w.Bool(a.default_output);
    w.Key("channels"); w.Raw(std::to_string(a.channels));
    w.BeginKey("sample_rates_hz"); w.ArrOpenAfterKey();
    for (uint32_t sr : a.sample_rates_hz) w.Raw(std::to_string(sr));
    w.ArrClose();
    w.Key("spatial_capable"); w.Bool(a.spatial_capable);
    w.Key("microphone"); w.Bool(a.microphone);
    w.ObjClose();
}

void WriteInput(Writer& w, const InputDevice& i) {
    w.ObjOpen();
    w.Key("id"); w.Str(i.id);
    w.Key("kind"); w.Str(i.kind);
    w.Key("name"); w.Str(i.name);
    w.Key("backend"); w.Str(i.backend);
    w.Key("connection"); w.Str(i.connection);
    w.Key("battery_percent");
    if (i.battery_percent < 0) w.Raw("null");
    else w.Raw(std::to_string(i.battery_percent));
    w.Key("haptics"); w.Bool(i.haptics);
    w.Key("gyro"); w.Bool(i.gyro);
    w.Key("touch_surface"); w.Bool(i.touch_surface);
    w.Key("adaptive_triggers"); w.Bool(i.adaptive_triggers);
    w.ObjClose();
}

// Sections shared between v1 and v2 shapes (minus /2-only fields).
void WriteCommonHead(Writer& w, const HostCapabilityRecord& r, bool v1_shape) {
    w.Key("schema"); w.Str(v1_shape ? "dc.host-capability/1" : r.schema_version);
    w.Key("host_id"); w.Str(r.host_id);
    w.Key("generated_utc"); w.Str(r.generated_utc);
    if (!v1_shape) {
        w.Key("build"); w.ObjOpen();
        w.Key("git_hash"); w.Str(r.build.git_hash);
        w.Key("build_config"); w.Str(r.build.build_config);
        w.Key("runtime_version"); w.Str(r.build.runtime_version);
        w.ObjClose();
    }

    w.Key("os"); w.ObjOpen();
    w.Key("family"); w.Str(r.os.family);
    w.Key("version"); w.Str(r.os.version);
    w.Key("build"); w.Raw(std::to_string(r.os.build));
    w.Key("edition"); w.Str(r.os.edition);
    w.ObjClose();

    w.Key("cpu"); w.ObjOpen();
    w.Key("arch"); w.Str(r.cpu.arch);
    w.Key("model_name"); w.Str(r.cpu.model_name);
    w.Key("physical_cores"); w.Raw(std::to_string(r.cpu.physical_cores));
    w.Key("logical_cores"); w.Raw(std::to_string(r.cpu.logical_cores));
    w.Key("base_clock_hz"); w.Raw(Num(r.cpu.base_clock_hz));
    w.Key("max_clock_hz"); w.Raw(Num(r.cpu.max_clock_hz));
    w.Key("game_thread_score"); w.Raw(Num(r.cpu.game_thread_score));
    w.Key("worker_score"); w.Raw(Num(r.cpu.worker_score));
    w.Key("sustained_score"); w.Raw(Num(r.cpu.sustained_score));
    w.ObjClose();

    w.Key("gpu"); w.ObjOpen();
    w.Key("vendor"); w.Str(r.gpu.vendor);
    w.Key("model_name"); w.Str(r.gpu.model_name);
    w.Key("driver_version"); w.Str(r.gpu.driver_version);
    w.Key("api"); w.ArrOpenAfterKey();
    for (const auto& a : r.gpu.api) w.Str(a);
    w.ArrClose();
    w.Key("d3d12"); w.ObjOpen();
    w.Key("available"); w.Bool(r.gpu.d3d12.available);
    w.Key("feature_level"); w.Str(r.gpu.d3d12.feature_level);
    w.Key("dedicated_vram_bytes"); w.Raw(std::to_string(r.gpu.d3d12.dedicated_vram_bytes));
    w.Key("shared_vram_bytes"); w.Raw(std::to_string(r.gpu.d3d12.shared_vram_bytes));
    w.Key("raytracing_tier"); w.Raw(Num(r.gpu.d3d12.raytracing_tier));
    w.Key("mesh_shader"); w.Bool(r.gpu.d3d12.mesh_shader);
    w.Key("sampler_feedback"); w.Bool(r.gpu.d3d12.sampler_feedback);
    w.Key("directstorage_gpu_decompression"); w.Bool(r.gpu.d3d12.directstorage_gpu_decompression);
    w.ObjClose();
    w.Key("vulkan"); w.ObjOpen();
    w.Key("available"); w.Bool(r.gpu.vulkan.available);
    w.Key("status"); w.Str(r.gpu.vulkan.status);
    w.Key("api_version"); w.Str(r.gpu.vulkan.api_version);
    w.Key("device_name"); w.Str(r.gpu.vulkan.device_name);
    w.Key("ray_query"); w.Bool(r.gpu.vulkan.ray_query);
    w.Key("mesh_shader"); w.Bool(r.gpu.vulkan.mesh_shader);
    if (!v1_shape) {
        w.Key("queue_families"); w.Raw(std::to_string(r.gpu.vulkan.queue_families));
        w.Key("memory_heaps"); w.Raw(std::to_string(r.gpu.vulkan.memory_heaps));
    }
    w.ObjClose();
    w.Key("vram_bytes"); w.Raw(std::to_string(r.gpu.vram_bytes));
    w.Key("raster_score"); w.Raw(Num(r.gpu.raster_score));
    w.Key("compute_score"); w.Raw(Num(r.gpu.compute_score));
    w.Key("rt_score"); w.Raw(Num(r.gpu.rt_score));
    w.Key("matrix_score"); w.Raw(Num(r.gpu.matrix_score));
    w.ObjClose();

    w.Key("memory"); w.ObjOpen();
    w.Key("system_bytes"); w.Raw(std::to_string(r.memory.system_bytes));
    w.Key("available_bytes"); w.Raw(std::to_string(r.memory.available_bytes));
    w.Key("bandwidth_score"); w.Raw(Num(r.memory.bandwidth_score));
    w.Key("pressure_safe_bytes"); w.Raw(std::to_string(r.memory.pressure_safe_bytes));
    w.ObjClose();
}

} // namespace

std::string ToJson(const HostCapabilityRecord& r, bool pretty) {
    Writer w(pretty);

    WriteCommonHead(w, r, /*v1_shape=*/false);

    w.Key("storage"); w.ArrOpenAfterKey();
    for (const auto& s : r.storage) WriteStorage(w, s);
    w.ArrClose();

    w.Key("display"); w.ArrOpenAfterKey();
    for (const auto& d : r.display) WriteDisplay(w, d, false);
    w.ArrClose();

    w.Key("audio"); w.ArrOpenAfterKey();
    for (const auto& a : r.audio) WriteAudio(w, a);
    w.ArrClose();

    w.Key("input"); w.ArrOpenAfterKey();
    for (const auto& i : r.input) WriteInput(w, i);
    w.ArrClose();

    w.Key("thermal"); w.ObjOpen();
    w.Key("sustained_profile_valid"); w.Bool(r.thermal.sustained_profile_valid);
    w.Key("power_scheme"); w.Str(r.thermal.power_scheme);
    w.Key("note"); w.Str(r.thermal.note);
    w.ObjClose();

    w.Key("trust"); w.ObjOpen();
    w.Key("secure_boot"); w.Bool(r.trust.secure_boot);
    w.Key("tpm"); w.Bool(r.trust.tpm);
    w.Key("tpm_version"); w.Str(r.trust.tpm_version);
    w.Key("measured_boot_available"); w.Bool(r.trust.measured_boot_available);
    w.ObjClose();

    w.Key("directstorage"); w.ObjOpen();
    w.Key("runtime_available"); w.Bool(r.directstorage.runtime_available);
    w.Key("version"); w.Str(r.directstorage.version);
    w.Key("gpu_decompression_supported"); w.Bool(r.directstorage.gpu_decompression_supported);
    w.Key("gdeflate_supported"); w.Bool(r.directstorage.gdeflate_supported);
    w.Key("zstd_status"); w.Str(r.directstorage.zstd_status);
    w.ObjClose();

    w.Key("qualification"); w.ObjOpen();
    w.Key("measured"); w.Bool(r.qualification.measured);
    w.Key("mode"); w.Str(r.qualification.mode);
    w.Key("sustained_valid"); w.Bool(r.qualification.sustained_valid);
    w.Key("timestamp_utc"); w.Str(r.qualification.timestamp_utc);
    w.ObjClose();

    w.Key("profiles_claimed"); w.ArrOpenAfterKey();
    for (const auto& p : r.profiles_claimed) {
        w.ObjOpen();
        w.Key("id"); w.Str(p.id);
        w.Key("version"); w.Raw(std::to_string(p.version));
        w.ObjClose();
    }
    w.ArrClose();

    w.Key("profiles_rejected"); w.ArrOpenAfterKey();
    for (const auto& p : r.profiles_rejected) {
        w.ObjOpen();
        w.Key("id"); w.Str(p.id);
        w.BeginKey("reasons"); w.ArrOpenAfterKey();
        for (const auto& rc : p.reasons) w.Str(rc);
        w.ArrClose();
        w.ObjClose();
    }
    w.ArrClose();

    // Session experience (DCX) claims — ADR-0022.
    w.Key("session_profiles_claimed"); w.ArrOpenAfterKey();
    for (const auto& p : r.session_profiles_claimed) {
        w.ObjOpen();
        w.Key("id"); w.Str(p.id);
        w.Key("version"); w.Raw(std::to_string(p.version));
        w.ObjClose();
    }
    w.ArrClose();

    w.Key("session_profiles_rejected"); w.ArrOpenAfterKey();
    for (const auto& p : r.session_profiles_rejected) {
        w.ObjOpen();
        w.Key("id"); w.Str(p.id);
        w.BeginKey("reasons"); w.ArrOpenAfterKey();
        for (const auto& rc : p.reasons) w.Str(rc);
        w.ArrClose();
        w.ObjClose();
    }
    w.ArrClose();

    return w.Finish();
}

std::string ToJsonV1(const HostCapabilityRecord& r, bool pretty) {
    Writer w(pretty);

    WriteCommonHead(w, r, /*v1_shape=*/true);

    w.Key("storage"); w.ArrOpenAfterKey();
    for (const auto& s : r.storage) WriteStorage(w, s);
    w.ArrClose();

    w.Key("display"); w.ArrOpenAfterKey();
    for (const auto& d : r.display) WriteDisplay(w, d, true);
    w.ArrClose();

    w.Key("audio"); w.ArrOpenAfterKey();
    for (const auto& a : r.audio) WriteAudio(w, a);
    w.ArrClose();

    w.Key("input"); w.ArrOpenAfterKey();
    for (const auto& i : r.input) WriteInput(w, i);
    w.ArrClose();

    w.Key("thermal"); w.ObjOpen();
    w.Key("sustained_profile_valid"); w.Bool(r.thermal.sustained_profile_valid);
    w.Key("power_scheme"); w.Str(r.thermal.power_scheme);
    w.Key("note"); w.Str(r.thermal.note);
    w.ObjClose();

    w.Key("trust"); w.ObjOpen();
    w.Key("secure_boot"); w.Bool(r.trust.secure_boot);
    w.Key("tpm"); w.Bool(r.trust.tpm);
    w.Key("tpm_version"); w.Str(r.trust.tpm_version);
    w.Key("measured_boot_available"); w.Bool(r.trust.measured_boot_available);
    w.ObjClose();

    // /1 profiles: bare id strings.
    w.Key("profiles"); w.ArrOpenAfterKey();
    for (const auto& p : r.profiles_claimed) w.Str(p.id);
    w.ArrClose();

    return w.Finish();
}

} // namespace dc
