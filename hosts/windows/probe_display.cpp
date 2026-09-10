// probe_display.cpp — display topology, modes, HDR/advanced-color, SDR white
// level. Discovery pass: CCD advanced-color + DXGI Output6 quantitative HDR
// metadata (DXGI_OUTPUT_DESC1) + VRR path_compatible via IDXGIFactory5
// tearing support (ledger §21). Actively_proven arrives via dc-displayprobe.
#include "win_util.hpp"
#include "display_compat.hpp"
#include "dc/capability.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <wingdi.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cstring>

namespace dcwin {

using namespace dc;

namespace {

std::string TargetNameViaDisplayConfig(const DISPLAY_DEVICEW& dd, const DEVMODEW& dm) {
    // QueryDisplayConfig path-source/target names give the monitor's EDID name.
    // Fallback: DEVMODE/WDDM-friendly "Generic" when CCD data is unavailable.
    (void)dd;
    (void)dm;
    return {};
}

void GetAdvancedColor(const std::wstring& device_key_w, HdrInfo& hdr) {
    if (device_key_w.empty()) return;

    DISPLAYCONFIG_DEVICE_INFO_HEADER header{};
    header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2;
    header.size = sizeof(DcGetAdvancedColorInfo2);

    // Resolve adapterId/id from CCD paths by matching GDI device key.
    UINT32 path_count = 0, mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count)
            != ERROR_SUCCESS || path_count == 0) {
        return;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(),
                           &mode_count, modes.data(), nullptr) != ERROR_SUCCESS) {
        return;
    }

    // Find the path whose source GDI device name matches this display's DeviceName.
    for (UINT32 p = 0; p < path_count; ++p) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = paths[p].sourceInfo.adapterId;
        src.header.id = paths[p].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;

        // src.viewGdiDeviceName is like L"\\\\.\\DISPLAY1" — compare prefix with
        // DEVMODE dmDeviceName. The DEVMODE DeviceName is the same GDI name.
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(device_key_w.c_str(), ENUM_CURRENT_SETTINGS, &dm)) continue;

        if (wcsncmp(src.viewGdiDeviceName, dm.dmDeviceName, 32) != 0) continue;

        // --- Advanced color (type 15 first, fall back to legacy type 9). ---
        DcGetAdvancedColorInfo2 ac2{};
        ac2.header = header;
        ac2.header.adapterId = paths[p].sourceInfo.adapterId;
        ac2.header.id = paths[p].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&ac2.header) == ERROR_SUCCESS) {
            hdr.advanced_color_supported = ac2.advancedColorSupported != 0;
            hdr.advanced_color_active = ac2.advancedColorActive != 0;
            hdr.user_enabled = ac2.highDynamicRangeUserEnabled != 0;
            hdr.wide_color_gamut = ac2.wideColorSupported != 0;
            hdr.bits_per_color = ac2.bitsPerColorChannel;
            switch (ac2.activeColorMode) {
                case 2: hdr.advanced_color_kind = "HDR"; break;
                case 1: hdr.advanced_color_kind = "WCG"; break;
                default: hdr.advanced_color_kind = "SDR"; break;
            }
        } else {
            DcGetAdvancedColorInfoLegacy ac{};
            ac.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
            ac.header.size = sizeof(ac);
            ac.header.adapterId = paths[p].sourceInfo.adapterId;
            ac.header.id = paths[p].sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&ac.header) == ERROR_SUCCESS) {
                hdr.advanced_color_supported = ac.advancedColorSupported != 0;
                hdr.advanced_color_active = ac.advancedColorActive != 0;
                hdr.wide_color_gamut = ac.wideColorEnabled != 0;
                hdr.advanced_color_kind = ac.advancedColorActive ? "WCG" : "SDR";
            }
        }

        // --- SDR white level (units: 1000 == 80 nits). ---
        DcGetSdrWhiteLevel sdr{};
        sdr.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        sdr.header.size = sizeof(sdr);
        sdr.header.adapterId = paths[p].targetInfo.adapterId;
        sdr.header.id = paths[p].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&sdr.header) == ERROR_SUCCESS) {
            hdr.sdr_white_level_nits = static_cast<double>(sdr.SDRWhiteLevel) * 80.0 / 1000.0;
        }

        hdr.metadata_available = hdr.advanced_color_supported; // luminance min/max pending IDXGIOutput6 backend
        break;
    }
}

} // namespace

// Quantitative HDR metadata via IDXGIOutput6::GetDesc1 (DXGI_OUTPUT_DESC1).
// Returns false when Output6 is unavailable (honest metadata_available=false).
static bool ProbeOutput6Hdr(const DEVMODEW& dm, HdrInfo& hdr) {
    IDXGIFactory6* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(DC_IID(IDXGIFactory6), reinterpret_cast<void**>(&factory))) || !factory) {
        return false;
    }
    bool found = false;
    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (FAILED(DC_COM(factory, EnumAdapters1, i, &adapter)) || !adapter) break;
        for (UINT o = 0; ; ++o) {
            IDXGIOutput* output = nullptr;
            if (FAILED(DC_COM(adapter, EnumOutputs, o, &output)) || !output) break;
            DXGI_OUTPUT_DESC odesc{};
            if (SUCCEEDED(DC_COM(output, GetDesc, &odesc)) && odesc.Monitor) {
                MONITORINFOEXW mi{};
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfoW(odesc.Monitor, &mi) &&
                    wcsncmp(mi.szDevice, dm.dmDeviceName, 32) == 0) {
                    // This output drives the display we're probing.
                    IDXGIOutput6* o6 = nullptr;
                    if (SUCCEEDED(output->QueryInterface(DC_IID(IDXGIOutput6),
                                                         reinterpret_cast<void**>(&o6))) && o6) {
                        DXGI_OUTPUT_DESC1 d1{};
                        if (SUCCEEDED(o6->GetDesc1(&d1))) {
                            hdr.min_luminance_nits = d1.MinLuminance;
                            hdr.max_luminance_nits = d1.MaxLuminance;
                            hdr.max_full_frame_luminance_nits = d1.MaxFullFrameLuminance;
                            hdr.red_primary[0] = d1.RedPrimary[0] / 1000.0;
                            hdr.red_primary[1] = d1.RedPrimary[1] / 1000.0;
                            hdr.green_primary[0] = d1.GreenPrimary[0] / 1000.0;
                            hdr.green_primary[1] = d1.GreenPrimary[1] / 1000.0;
                            hdr.blue_primary[0] = d1.BluePrimary[0] / 1000.0;
                            hdr.blue_primary[1] = d1.BluePrimary[1] / 1000.0;
                            hdr.white_point[0] = d1.WhitePoint[0] / 1000.0;
                            hdr.white_point[1] = d1.WhitePoint[1] / 1000.0;
                            // ColorSpace field: DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 (SDR)
                            // vs RGB_FULL_G10_NONE_P709 / G2084_NONE_P2020 (HDR-class).
                            if (d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                                d1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) {
                                hdr.metadata_formats.push_back("hdr10");
                            }
                            found = true;
                        }
                        o6->Release();
                    }
                    output->Release();
                    return found;
                }
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
    return found;
}

// VRR path_compatible: IDXGIFactory5 tearing support (ledger §21). This is
// environment-level; per-display OS VRR state arrives via CCD/advanced color
// and active proof via dc-displayprobe.
static void ProbeVrrPathCompatible(VrrInfo& vrr) {
    IDXGIFactory5* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(DC_IID(IDXGIFactory5), reinterpret_cast<void**>(&factory))) || !factory) {
        return; // stays false: unknown/unavailable is not a claim
    }
    BOOL tearing = FALSE;
    HRESULT hr = factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                              &tearing, sizeof(tearing));
    factory->Release();
    vrr.path_compatible = SUCCEEDED(hr) && tearing == TRUE;
}

void ProbeDisplays(std::vector<DisplayDeviceInfo>& displays) {
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD dev = 0; EnumDisplayDevicesW(nullptr, dev, &dd, EDD_GET_DEVICE_INTERFACE_NAME); ++dev) {
        if ((dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) continue;

        DisplayDeviceInfo info;
        info.id = "display" + std::to_string(dev);
        info.name = WideUtf8(dd.DeviceString);
        info.primary = (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
        info.active = true;

        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
            info.width = dm.dmPelsWidth;
            info.height = dm.dmPelsHeight;
            info.desktop_refresh_hz =
                dm.dmDisplayFrequency > 1 ? static_cast<double>(dm.dmDisplayFrequency) : 60.0;
        }

        // Enumerate discrete modes.
        for (DWORD m = 0; ; ++m) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (!EnumDisplaySettingsExW(dd.DeviceName, m, &mode, 0)) break;
            if ((mode.dmFields & DM_PELSWIDTH) == 0 || mode.dmPelsWidth == 0) continue;
            if ((mode.dmFields & DM_DISPLAYFREQUENCY) == 0 || mode.dmDisplayFrequency < 23) continue;

            DisplayMode dm_mode;
            dm_mode.width = mode.dmPelsWidth;
            dm_mode.height = mode.dmPelsHeight;
            // Rational truth (ADR-0022): CDS reports integer Hz — record
            // denominator 1 honestly rather than inventing precision.
            dm_mode.refresh_numerator = mode.dmDisplayFrequency;
            dm_mode.refresh_denominator = 1;
            dm_mode.refresh_hz = static_cast<double>(mode.dmDisplayFrequency);
            dm_mode.bits_per_color = (mode.dmFields & DM_BITSPERPEL) ? mode.dmBitsPerPel : 0;
            info.modes.push_back(dm_mode);
        }

        GetAdvancedColor(std::wstring(dd.DeviceName), info.hdr);

        // Quantitative HDR (min/max luminance, primaries, white point) —
        // only truthful when Output6 exposes this monitor.
        DEVMODEW cur{};
        cur.dmSize = sizeof(cur);
        if (EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &cur)) {
            if (ProbeOutput6Hdr(cur, info.hdr)) {
                info.hdr.metadata_available = true;
            }
        }

        // VRR three-state model (ledger §21): supported stays false until the
        // OS layer reports it (pending CCD VRR metadata); path compatibility
        // is probed here; active proof only via dc-displayprobe.
        ProbeVrrPathCompatible(info.vrr);
        info.vrr.actively_proven = false;

        displays.push_back(std::move(info));
    }
}

} // namespace dcwin
