// win_util.hpp — Windows host probe utilities (hosts/windows internal).
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS

#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <ctime>

#include <windows.h>

namespace dcwin {

// COM compatibility: MSVC SDK headers are C++-style (virtual methods);
// MinGW WIDL headers are C-style (lpVtbl). Canon §40.1 designates MSVC on
// Windows, but this keeps the host layer compilable by both toolchains.
#if defined(_MSC_VER)
    #define DC_COM(obj, method, ...) (obj)->method(__VA_ARGS__)
    #define DC_IID(type) __uuidof(type)
#else
    #define DC_COM(obj, method, ...) (obj)->lpVtbl->method((obj), ##__VA_ARGS__)
    #define DC_IID(type) IID_##type
#endif

// UTF-16 -> UTF-8
inline std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

inline std::string WideUtf8(const wchar_t* w) {
    if (!w) return {};
    return Utf8(std::wstring(w));
}

// Read a registry string value (REG_SZ/EXPAND_SZ). Returns false if absent.
inline bool RegReadString(HKEY root, const wchar_t* subkey, const wchar_t* value, std::string& out) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    LSTATUS st = RegQueryValueExW(h, value, nullptr, &type, nullptr, &size);
    if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) { RegCloseKey(h); return false; }
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, L'\0');
    st = RegQueryValueExW(h, value, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf.data()), &size);
    RegCloseKey(h);
    if (st != ERROR_SUCCESS) return false;
    out = Utf8(std::wstring(buf.data()));
    return true;
}

// Read a registry DWORD value.
inline bool RegReadDword(HKEY root, const wchar_t* subkey, const wchar_t* value, DWORD& out) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = sizeof(DWORD);
    LSTATUS st = RegQueryValueExW(h, value, nullptr, &type, reinterpret_cast<LPBYTE>(&out), &size);
    RegCloseKey(h);
    return st == ERROR_SUCCESS && type == REG_DWORD;
}

// UTC timestamp in ISO-8601.
inline std::string UtcTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

// Stable local host id: MachineGuid, else computer name.
inline std::string HostId() {
    std::string guid;
    if (RegReadString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid", guid)) {
        return guid;
    }
    wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD len = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(name, &len)) return Utf8(std::wstring(name, len));
    return "unknown-host";
}

// Map a Windows DriveType + bus heuristics to a DC storage class (canon §14.1).
// Honest classification: unknown stays DC_STORAGE_UNKNOWN; measured latency
// classes arrive with the microbenchmark phase.
inline std::string ClassifyStorage(UINT drive_type, const std::string& bus_type) {
    std::string bus;
    for (char c : bus_type) bus += static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    if (drive_type == DRIVE_FIXED) {
        if (bus.find("nvme") != std::string::npos) return "DC_STORAGE_NVME_BASE";
        if (bus.find("sd") != std::string::npos || bus.find("mmc") != std::string::npos)
            return "DC_STORAGE_SATA_SSD";
        if (bus.find("sata") != std::string::npos || bus.find("ata") != std::string::npos)
            return "DC_STORAGE_SATA_SSD";
        if (bus.find("usb") != std::string::npos) return "DC_STORAGE_LEGACY";
        return "DC_STORAGE_UNKNOWN";
    }
    if (drive_type == DRIVE_REMOVABLE) return "DC_STORAGE_LEGACY";
    return "DC_STORAGE_UNKNOWN";
}

} // namespace dcwin
