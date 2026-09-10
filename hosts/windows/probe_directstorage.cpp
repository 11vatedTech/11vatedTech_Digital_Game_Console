// probe_directstorage.cpp — DirectStorage capability discovery (DK0-M1E).
// Probes the OS-shipped runtime (storage.ral.dll) without requiring the DS
// SDK at build time. Stable path is DS 1.3 + GDeflate; 1.4/Zstd is preview
// (research ledger §18) and never a platform requirement.
#include "win_util.hpp"
#include "dc/capability.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <winver.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace dcwin {

using namespace dc;

namespace {

struct ModuleInfo {
    bool present = false;
    std::string version; // "1.3.0.0" style
};

ModuleInfo ProbeModule(const wchar_t* dll) {
    ModuleInfo info;
    HMODULE mod = LoadLibraryW(dll);
    if (!mod) return info;
    info.present = true;
    FreeLibrary(mod); // refcount balanced; presence proven

    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(mod, path, MAX_PATH) == 0) return info;
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (size == 0) return info;
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return info;
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &len) || !ffi) return info;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                  HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                  HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    info.version = buf;
    return info;
}

} // namespace

void ProbeDirectStorage(DirectStorageInfo& out) {
    // DStorageGetFactory is the stable DS 1.x entry point.
    HMODULE ral = LoadLibraryW(L"storage.ral.dll");
    if (!ral) {
        out.runtime_available = false;
        out.version = "";
        out.gpu_decompression_supported = false;
        out.gdeflate_supported = false;
        out.zstd_status = "unavailable";
        return;
    }
    void* pfn = reinterpret_cast<void*>(GetProcAddress(ral, "DStorageGetFactory"));
    out.runtime_available = pfn != nullptr;
    FreeLibrary(ral);

    ModuleInfo mi = ProbeModule(L"storage.ral.dll");
    out.version = mi.version;

    // GPU decompression / codec support requires the DS SDK interface surface
    // (IDStorageFactory2/3 QueryCompressionSupport). Without the SDK headers
    // linked, this probe cannot verify them: honest false + status, never a
    // claimed capability (C10). Wired to real QueryCompressionSupport when the
    // DS SDK dependency lands.
    out.gpu_decompression_supported = false;
    out.gdeflate_supported = false;
    // Zstd ships in the 1.4 preview channel only; stable OS runtime = no.
    out.zstd_status = out.runtime_available ? "unavailable" : "unavailable";
}

} // namespace dcwin
