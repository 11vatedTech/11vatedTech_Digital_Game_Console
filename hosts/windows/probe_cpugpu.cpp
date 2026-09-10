// probe_cpugpu.cpp — CPU topology + GPU discovery (DXGI factory, D3D12 feature query).
// All feature probing is capability truth; performance scores stay 0 until the
// microbenchmark phase (C5, C10).
#include "win_util.hpp"
#include "dc/capability.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define INITGUID

#include <windows.h>
#include <initguid.h>
#include <dxgi.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace dcwin {

using namespace dc;

namespace {

std::string VendorFromId(uint32_t vendor_id) {
    switch (vendor_id & 0xFFFF) {
        case 0x10DE: return "nvidia";
        case 0x1002: case 0x1022: return "amd";
        case 0x8086: return "intel";
        default: return "other";
    }
}

std::string FeatureLevelName(D3D_FEATURE_LEVEL fl) {
    // D3D_FEATURE_LEVEL encoding: major in bits 12-15, minor in bits 8-11
    // (e.g. 0xc200 = 12_2, 0xb100 = 11_1).
    unsigned major = (static_cast<unsigned>(fl) >> 12) & 0xF;
    unsigned minor = (static_cast<unsigned>(fl) >> 8) & 0xF;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u_%u", major, minor);
    return buf;
}

void ProbeDxgiAdapters(GpuInfo& gpu) {
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(DC_IID(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        if (gpu.model_name.empty()) gpu.model_name = "dxgi-unavailable";
        return;
    }

    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (FAILED(DC_COM(factory, EnumAdapters1, i, &adapter)) || !adapter) break;

        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(DC_COM(adapter, GetDesc1, &desc))) {
            // Software adapters report LUID 0 / no dedicated VRAM; prefer hardware.
            bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            if (!software && desc.DedicatedVideoMemory >= gpu.vram_bytes) {
                gpu.vendor = VendorFromId(desc.VendorId);
                gpu.model_name = WideUtf8(desc.Description);
                gpu.vram_bytes = desc.DedicatedVideoMemory;
                gpu.d3d12.dedicated_vram_bytes = desc.DedicatedVideoMemory;
                gpu.d3d12.shared_vram_bytes = desc.SharedSystemMemory;
            }
        }
        DC_COM(adapter, Release);
    }
    DC_COM(factory, Release);
}

// Re-enumerate and return the hardware adapter with the most VRAM (the same
// adapter ProbeDxgiAdapters selected). Caller releases. Hybrid-host fix:
// feature probing must run on the selected GPU, not the OS default adapter.
static IDXGIAdapter1* AcquireSelectedAdapter() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(DC_IID(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || !factory) {
        return nullptr;
    }
    IDXGIAdapter1* best = nullptr;
    UINT64 best_vram = 0;
    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (FAILED(DC_COM(factory, EnumAdapters1, i, &adapter)) || !adapter) break;
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(DC_COM(adapter, GetDesc1, &desc)) &&
            (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            desc.DedicatedVideoMemory > best_vram) {
            best_vram = desc.DedicatedVideoMemory;
            if (best) DC_COM(best, Release);
            best = adapter; // ownership transferred
            continue;
        }
        DC_COM(adapter, Release);
    }
    DC_COM(factory, Release);
    return best;
}

void ProbeD3D12Features(GpuInfo& gpu) {
    HMODULE d3d12mod = LoadLibraryW(L"d3d12.dll");
    if (!d3d12mod) return;

    using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto create = reinterpret_cast<D3D12CreateDeviceFn>(
        reinterpret_cast<void*>(GetProcAddress(d3d12mod, "D3D12CreateDevice")));
    if (!create) { FreeLibrary(d3d12mod); return; }

    // Hybrid-host correctness: probe the selected GPU, never the default adapter.
    IDXGIAdapter1* selected = AcquireSelectedAdapter();
    IUnknown* target = selected;

    ID3D12Device* device = nullptr;
    HRESULT hr = create(target, D3D_FEATURE_LEVEL_11_0, DC_IID(ID3D12Device),
                        reinterpret_cast<void**>(&device));
    if (FAILED(hr) || !device) {
        if (selected) DC_COM(selected, Release);
        FreeLibrary(d3d12mod);
        return;
    }

    gpu.d3d12.available = true;

    // Highest supported feature level.
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
    };
    for (D3D_FEATURE_LEVEL fl : levels) {
        ID3D12Device* probe = nullptr;
        if (SUCCEEDED(create(target, fl, DC_IID(ID3D12Device),
                             reinterpret_cast<void**>(&probe))) && probe) {
            gpu.d3d12.feature_level = FeatureLevelName(fl);
            DC_COM(probe, Release);
            break;
        }
    }
    if (gpu.d3d12.feature_level.empty()) {
        gpu.d3d12.feature_level = FeatureLevelName(D3D_FEATURE_LEVEL_11_0);
    }

    // Options5: ray tracing tier (tier value / 10 => e.g. 1.0, 1.1, 1.2).
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
    if (SUCCEEDED(DC_COM(device, CheckFeatureSupport,
            D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5)))) {
        gpu.d3d12.raytracing_tier = static_cast<double>(opts5.RaytracingTier) / 10.0;
    }

    // Options7: mesh shaders + sampler feedback.
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7{};
    if (SUCCEEDED(DC_COM(device, CheckFeatureSupport,
            D3D12_FEATURE_D3D12_OPTIONS7, &opts7, sizeof(opts7)))) {
        gpu.d3d12.mesh_shader = opts7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
        gpu.d3d12.sampler_feedback =
            opts7.SamplerFeedbackTier != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
    }

    DC_COM(device, Release);
    if (selected) DC_COM(selected, Release);
    FreeLibrary(d3d12mod);
}

void ProbeCpuTopology(CpuInfo& cpu) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    cpu.logical_cores = si.dwNumberOfProcessors;

    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && len > 0) {
        std::vector<BYTE> buf(len);
        if (GetLogicalProcessorInformationEx(RelationProcessorCore,
                                             reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()),
                                             &len)) {
            uint32_t cores = 0;
            BYTE* p = buf.data();
            BYTE* end = buf.data() + len;
            while (p + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= end) {
                auto info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(p);
                if (info->Size == 0) break;
                if (info->Relationship == RelationProcessorCore) ++cores;
                p += info->Size;
            }
            cpu.physical_cores = cores;
        }
    }

    std::string name;
    DWORD mhz_value = 0;
    RegReadString(HKEY_LOCAL_MACHINE,
                  L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                  L"ProcessorNameString", name);
    cpu.model_name = name;
    // "~MHz" is REG_DWORD in the registry.
    if (!RegReadDword(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      L"~MHz", mhz_value)) {
        mhz_value = 0;
    }
    cpu.base_clock_hz = static_cast<double>(mhz_value) * 1e6;
}

void ProbeGpuDriver(GpuInfo& gpu) {
    // Match the display-class driver to the selected GPU by DriverDesc so
    // hybrid hosts report the discrete GPU's driver, not the iGPU's.
    HKEY base = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Class\\"
                      L"{4d36e968-e325-11ce-bfc1-08002be10318}",
                      0, KEY_READ, &base) != ERROR_SUCCESS) {
        return;
    }
    std::string want = gpu.model_name;
    wchar_t buf[16] = {};
    for (unsigned i = 0; i < 64; ++i) {
        std::swprintf(buf, 16, L"%04u", i);
        HKEY h = nullptr;
        if (RegOpenKeyExW(base, buf, 0, KEY_READ, &h) != ERROR_SUCCESS) continue;
        std::string desc;
        if (RegReadString(h, nullptr, L"DriverDesc", desc) && !desc.empty()) {
            bool match = want.empty() || desc.find(want) != std::string::npos;
            std::string ver;
            if (match && RegReadString(h, nullptr, L"DriverVersion", ver)) {
                gpu.driver_version = ver;
                RegCloseKey(h);
                break;
            }
        }
        RegCloseKey(h);
    }
    RegCloseKey(base);
}

} // namespace

void ProbeCpu(CpuInfo& cpu) { ProbeCpuTopology(cpu); }

void ProbeGpu(GpuInfo& gpu) {
    ProbeDxgiAdapters(gpu);
    ProbeD3D12Features(gpu);
    ProbeGpuDriver(gpu);
    if (gpu.d3d12.available) {
        bool has = false;
        for (const auto& a : gpu.api) if (a == "d3d12") has = true;
        if (!has) gpu.api.push_back("d3d12");
    }
}

} // namespace dcwin
