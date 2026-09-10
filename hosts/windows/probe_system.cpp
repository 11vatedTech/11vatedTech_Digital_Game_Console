// probe_system.cpp — OS / memory / thermal / trust discovery (DevKit-0 M1).
// Discovery-only pass: measured scores remain 0 (unmeasured) — C10 truthful reporting.
#include "win_util.hpp"
#include "dc/capability.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <tbs.h>
#include <sysinfoapi.h>
#include <cstdio>

namespace dcwin {

using namespace dc;

namespace {

using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

bool QueryOsVersion(std::string& version, uint32_t& build) {
    HMODULE mod = GetModuleHandleW(L"ntdll.dll");
    if (!mod) return false;
    auto fn = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "RtlGetVersion")));
    if (!fn) return false;
    RTL_OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (fn(&vi) != 0) return false;
    version = std::to_string(vi.dwMajorVersion) + "." + std::to_string(vi.dwMinorVersion);
    build = vi.dwBuildNumber;
    return true;
}

std::string WindowsEdition() {
    // Registry edition name mirrors winver.
    std::string name;
    if (RegReadString(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      L"EditionID", name)) {
        return name;
    }
    return "unknown";
}

void QueryMemory(MemoryInfo& mem) {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        mem.system_bytes = ms.ullTotalPhys;
        mem.available_bytes = ms.ullAvailPhys;
        // pressure_safe_bytes stays 0 here: it is a MEASURED value from the
        // memory.pressure benchmark (C10 — never derived or estimated).
        mem.pressure_safe_bytes = 0;
    }
}

void QueryThermal(ThermalInfo& thermal) {
    // Power scheme friendly name via PowerGetActiveScheme/PowerReadFriendlyName
    // is declared in newer SDK headers; read the registry mirror instead (honest,
    // read-only, no extra deps). A true sustained-qualification probe lands in
    // the microbenchmark phase; until then sustained_profile_valid stays false (C10).
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes",
                      0, KEY_READ, &h) == ERROR_SUCCESS) {
        RegCloseKey(h);
    }
    wchar_t guid[64] = {};
    DWORD size = sizeof(guid);
    HKEY active = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Power",
                      0, KEY_READ, &active) == ERROR_SUCCESS) {
        if (RegQueryValueExW(active, L"ActivePowerScheme", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(guid), &size) == ERROR_SUCCESS) {
            thermal.power_scheme = "scheme:" + Utf8(std::wstring(guid));
        }
        RegCloseKey(active);
    }
    thermal.note = "discovery-only; sustained qualification pending (microbenchmarks)";
}

// Secure Boot state: read the UEFI authenticated variable. On non-admin
// sessions the call fails; we treat failure as unknown => false (C10 truthful).
void QuerySecureBoot(TrustInfo& trust) {
    BYTE value[8] = {};
    DWORD size = GetFirmwareEnvironmentVariableExW(
        L"SecureBoot", L"{8be4df61-93ca-11d2-aa0d-00e098032b8c}",
        value, sizeof(value), nullptr);
    if (size > 0) {
        trust.secure_boot = value[0] != 0;
    }
}

void QueryTpm(TrustInfo& trust) {
    // Tbsi_Context_Create succeeds only when a TPM 2.0 device is present.
    // tbs.h returns TBS_RESULT (UINT32), TBS_SUCCESS == 0.
    TBS_HCONTEXT ctx = nullptr;
    TBS_CONTEXT_PARAMS2 params{};
    params.version = TBS_CONTEXT_VERSION_TWO;
    params.includeTpm12 = 0;
    params.includeTpm20 = 1;
    TBS_RESULT tr = Tbsi_Context_Create(reinterpret_cast<PCTBS_CONTEXT_PARAMS>(&params), &ctx);
    if (tr == TBS_SUCCESS) {
        trust.tpm = true;
        trust.tpm_version = "2.0";
        Tbsip_Context_Close(ctx);
    }
    trust.measured_boot_available = trust.tpm; // conservative: TPM enables measured boot
}

} // namespace

void ProbeOs(OsInfo& os, uint32_t& build_out) {
    std::string version;
    uint32_t build = 0;
    if (!QueryOsVersion(version, build)) {
        version = "unknown";
    }
    os.family = "windows";
    os.version = version;
    os.build = build;
    os.edition = WindowsEdition();
    build_out = build;
}

void ProbeMemory(MemoryInfo& mem) { QueryMemory(mem); }
void ProbeThermal(ThermalInfo& thermal) { QueryThermal(thermal); }
void ProbeTrust(TrustInfo& trust) {
    QuerySecureBoot(trust);
    QueryTpm(trust);
}

} // namespace dcwin
