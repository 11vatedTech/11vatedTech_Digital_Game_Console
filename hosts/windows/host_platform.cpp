// host_platform.cpp — Windows Host Edition implementation of IHostPlatform
// (DC-CANON-001 §6.1, §10; ADR-0002/0007). Discovery-only capability truth.
#include "dc/i_host_platform.hpp"

#include "win_util.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <objbase.h>

namespace dcwin {

using namespace dc;

// probe_system.cpp
void ProbeOs(OsInfo& os, uint32_t& build);
void ProbeMemory(MemoryInfo& mem);
void ProbeThermal(ThermalInfo& thermal);
void ProbeTrust(TrustInfo& trust);
// probe_cpugpu.cpp
void ProbeCpu(CpuInfo& cpu);
void ProbeGpu(GpuInfo& gpu);
// probe_display.cpp
void ProbeDisplays(std::vector<DisplayDeviceInfo>& displays);
// probe_storage.cpp
void ProbeStorage(std::vector<StorageDeviceInfo>& storage);
// probe_devices.cpp
void ProbeAudioEndpoints(std::vector<AudioEndpoint>& out);
void ProbeInputDevices(std::vector<InputDevice>& out);
// probe_directstorage.cpp
void ProbeDirectStorage(DirectStorageInfo& out);
// probe_vulkan.cpp
void ProbeVulkan(VulkanInfo& out);
// probe_gameinput.cpp
size_t ProbeGameInputDevices(std::vector<InputDevice>& out);
} // namespace dcwin

namespace {

class WindowsHostPlatform final : public dc::IHostPlatform {
public:
    const char* HostFamily() const override { return "windows"; }

    dc::Result QueryCapabilities(dc::HostCapabilityRecord& out) override {
        // COM for MMDevice (audio) — apartment-threaded, per-call init is cheap
        // and keeps this entry point self-contained.
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool com_owner = SUCCEEDED(com);

        out = dc::HostCapabilityRecord{};
        out.host_id = dcwin::HostId();
        out.generated_utc = dcwin::UtcTimestamp();

        uint32_t build = 0;
        dcwin::ProbeOs(out.os, build);
        dcwin::ProbeCpu(out.cpu);
        dcwin::ProbeGpu(out.gpu);
        dcwin::ProbeMemory(out.memory);
        dcwin::ProbeStorage(out.storage);
        dcwin::ProbeDisplays(out.display);
        dcwin::ProbeAudioEndpoints(out.audio);
        dcwin::ProbeInputDevices(out.input);
        dcwin::ProbeThermal(out.thermal);
        dcwin::ProbeTrust(out.trust);
        dcwin::ProbeDirectStorage(out.directstorage);
        dcwin::ProbeVulkan(out.gpu.vulkan);
        // Vulkan presence extends the API list truthfully (C10).
        if (out.gpu.vulkan.available) out.gpu.api.push_back("vulkan");

        out.build.git_hash =
#ifdef DC_BUILD_GIT_HASH
            DC_BUILD_GIT_HASH;
#else
            "unknown";
#endif
        out.build.build_config =
#ifdef NDEBUG
            "Release";
#else
            "Debug";
#endif
        out.build.runtime_version = "0.1.0";

        // profiles: empty until sustained qualification microbenchmarks run
        // (ADR-0007). Honest absence beats invented tiers (C5/C10).

        if (com_owner) CoUninitialize();
        return dc::Result::Success();
    }
};

} // namespace

namespace dc {
IHostPlatform* CreateHostPlatform() {
    return new (std::nothrow) WindowsHostPlatform();
}
} // namespace dc
