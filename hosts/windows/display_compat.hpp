// display_compat.hpp — local ABI-compatible declarations for display info that
// MinGW-w64 headers gate behind newer NTDDI values (DISPLAYCONFIG advanced color).
// Layouts follow the official DISPLAYCONFIG definitions (Microsoft Learn, 2026).
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <wingdi.h>

#pragma warning(push)
#pragma warning(disable: 4201) // nameless struct/union mirrors the official ABI

namespace dcwin {

// Legacy GET_ADVANCED_COLOR_INFO payload (info type 9), ABI-stable since Win10 1703.
struct DcGetAdvancedColorInfoLegacy {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    union {
        struct {
            UINT32 advancedColorSupported : 1;
            UINT32 advancedColorActive : 1;
            UINT32 reserved1 : 1;
            UINT32 wideColorEnabled : 1;
            UINT32 reserved : 28;
        };
        UINT32 value;
    };
};

// GET_ADVANCED_COLOR_INFO_2 payload (info type 15, Windows 11 GA+).
struct DcGetAdvancedColorInfo2 {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    union {
        struct {
            UINT32 advancedColorSupported : 1;
            UINT32 advancedColorActive : 1;
            UINT32 reserved1 : 1;
            UINT32 advancedColorLimitedByPolicy : 1;
            UINT32 highDynamicRangeSupported : 1;
            UINT32 highDynamicRangeUserEnabled : 1;
            UINT32 wideColorSupported : 1;
            UINT32 wideColorUserEnabled : 1;
            UINT32 reserved : 24;
        };
        UINT32 value;
    };
    UINT32 colorEncoding;
    UINT32 bitsPerColorChannel;
    UINT32 activeColorMode; // 0=SDR 1=WCG 2=HDR (DISPLAYCONFIG_ADVANCED_COLOR_MODE)
};

// GET_SDR_WHITE_LEVEL payload (info type 11). SDRWhiteLevel units: 1000 == 80 nits.
struct DcGetSdrWhiteLevel {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    union {
        struct { UINT32 SDRWhiteLevel : 32; };
        UINT32 value;
    };
};

// DISPLAYCONFIG_DEVICE_INFO_GET_* info-type constants come from wingdi.h
// (official enum values; the *_2 struct itself is gated in MinGW headers).

} // namespace dcwin

#pragma warning(pop)
