// probe_devices.cpp — audio endpoints (MMDevice C API) + input devices (XInput).
// GameInput/SDL3 backends land in the input-subsystem phase; XInput gives an
// honest DevKit-0 enumeration floor.
#include "win_util.hpp"
#include "dc/capability.hpp"
#include "dc/input_reconcile.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define INITGUID

#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <xinput.h>
#include <cstdio>
#include <cstring>

namespace dcwin {

using namespace dc;

size_t ProbeGameInputDevices(std::vector<InputDevice>& out);
size_t ProbeSdl3Gamepads(std::vector<InputDevice>& out);

namespace {

// Official CLSID for MMDeviceEnumerator (mmdeviceapi).
// Defined locally: SDK macro paths differ between toolchains; the GUID is fixed.
const CLSID kMMDeviceEnumeratorClsid =
    {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};

std::string EndpointKindFromName(const std::string& name) {
    std::string lower;
    lower.reserve(name.size());
    for (char c : name) lower += static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    if (lower.find("hdmi") != std::string::npos ||
        lower.find("nvidia ") != std::string::npos ||
        lower.find("high definition audio") != std::string::npos) return "hdmi";
    if (lower.find("bluetooth") != std::string::npos) return "bluetooth";
    if (lower.find("usb") != std::string::npos) return "usb";
    return "analog";
}

struct PropVariantRAII {
    PROPVARIANT v{};
    PropVariantRAII() { PropVariantInit(&v); }
    ~PropVariantRAII() { PropVariantClear(&v); }
    PropVariantRAII(const PropVariantRAII&) = delete;
    PropVariantRAII& operator=(const PropVariantRAII&) = delete;
};

void ProbeAudio(std::vector<AudioEndpoint>& out) {
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(kMMDeviceEnumeratorClsid, nullptr, CLSCTX_ALL,
                                  DC_IID(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) return;

    IMMDeviceCollection* collection = nullptr;
    if (SUCCEEDED(DC_COM(enumerator, EnumAudioEndpoints,
                         eRender, DEVICE_STATE_ACTIVE, &collection)) && collection) {
        UINT count = 0;
        DC_COM(collection, GetCount, &count);

        for (UINT i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            if (FAILED(DC_COM(collection, Item, i, &device)) || !device) continue;

            AudioEndpoint ep;
            LPWSTR id_w = nullptr;
            if (SUCCEEDED(DC_COM(device, GetId, &id_w)) && id_w) {
                ep.id = WideUtf8(id_w);
                CoTaskMemFree(id_w);
            }

            IPropertyStore* store = nullptr;
            if (SUCCEEDED(DC_COM(device, OpenPropertyStore, STGM_READ, &store)) && store) {
                PropVariantRAII pv;
                if (SUCCEEDED(DC_COM(store, GetValue, PKEY_Device_FriendlyName, &pv.v)) &&
                    pv.v.vt == VT_LPWSTR && pv.v.pwszVal) {
                    ep.name = WideUtf8(pv.v.pwszVal);
                }
                DC_COM(store, Release);
            }

            IAudioClient* client = nullptr;
            if (SUCCEEDED(DC_COM(device, Activate, DC_IID(IAudioClient), CLSCTX_ALL,
                                 nullptr, reinterpret_cast<void**>(&client))) && client) {
                WAVEFORMATEX* mix = nullptr;
                if (SUCCEEDED(DC_COM(client, GetMixFormat, &mix)) && mix) {
                    ep.channels = mix->nChannels;
                    ep.sample_rates_hz.push_back(mix->nSamplesPerSec);
                    CoTaskMemFree(mix);
                }
                DC_COM(client, Release);
            }

            ep.kind = EndpointKindFromName(ep.name);
            ep.spatial_capable = false; // reported honestly; spatial backend later
            ep.microphone = false;
            out.push_back(std::move(ep));
            DC_COM(device, Release);
        }
        DC_COM(collection, Release);
    }

    // Mark the default output.
    IMMDevice* def = nullptr;
    if (SUCCEEDED(DC_COM(enumerator, GetDefaultAudioEndpoint, eRender, eConsole, &def)) && def) {
        LPWSTR id_w = nullptr;
        if (SUCCEEDED(DC_COM(def, GetId, &id_w)) && id_w) {
            std::string def_id = WideUtf8(id_w);
            for (auto& ep : out) if (ep.id == def_id) ep.default_output = true;
            CoTaskMemFree(id_w);
        }
        DC_COM(def, Release);
    }
    DC_COM(enumerator, Release);
}

void ProbeInput(std::vector<InputDevice>& out) {
    // Primary: GameInput (DK0-M1H). XInput below is the legacy fallback floor
    // for machines without the GameInput runtime (C10 honest degradation).
    // §K dedup: enumerate every backend, then reconcile through the core
    // policy — one physical controller must never surface as two players.
    // The policy resolves cross-backend duplicates (GameInput primary → SDL3
    // portable → XInput legacy) deterministically; hosts only apply the
    // reconciled result. Same observable behavior as the previous early-
    // return waterfall on single-backend hosts, but correct when backends
    // genuinely split device families.
    std::vector<InputDevice> gi, sdl, xi;
    ProbeGameInputDevices(gi);
    ProbeSdl3Gamepads(sdl);

    // XInput floor: up to 4 XInput gamepads (legacy backend).
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE state{};
        if (XInputGetState(i, &state) != ERROR_SUCCESS) continue;
        InputDevice dev;
        dev.id = "xinput-" + std::to_string(i);
        dev.kind = "gamepad";
        dev.name = "XInput Gamepad " + std::to_string(i + 1);
        dev.backend = "rawinput";
        dev.connection = "unknown";
        dev.haptics = true; // XInput supports rumble

        XINPUT_BATTERY_INFORMATION bat{};
        if (XInputGetBatteryInformation(i, BATTERY_DEVTYPE_GAMEPAD, &bat) == ERROR_SUCCESS) {
            if (bat.BatteryType == BATTERY_TYPE_WIRED) dev.connection = "wired";
            else if (bat.BatteryType == BATTERY_TYPE_ALKALINE ||
                     bat.BatteryType == BATTERY_TYPE_NIMH) {
                dev.connection = "wireless";
                dev.battery_percent = bat.BatteryLevel == BATTERY_LEVEL_FULL ? 100
                                    : bat.BatteryLevel == BATTERY_LEVEL_MEDIUM ? 60
                                    : bat.BatteryLevel == BATTERY_LEVEL_LOW ? 25 : 8;
            }
        }
        xi.push_back(std::move(dev));
    }

    // Reconcile (pure core policy): primary → secondary → legacy with
    // conservative duplicate suppression. Reconciled devices are mapped back
    // to their full discovery records by (backend, id).
    std::vector<dc::ReconcileDevice> gp, sp, xp;
    for (const auto& d : gi) gp.push_back({d.backend, d.id, ""});
    for (const auto& d : sdl) sp.push_back({d.backend, d.id, ""});
    for (const auto& d : xi) xp.push_back({d.backend, d.id, ""});
    auto reconciled = dc::ReconcileControllers(gp, sp, xp);
    for (const auto& r : reconciled) {
        const std::vector<InputDevice>* src = nullptr;
        if (r.backend == "gameinput") src = &gi;
        else if (r.backend == "sdl3") src = &sdl;
        else src = &xi;
        for (const auto& d : *src) {
            if (d.id == r.id) {
                out.push_back(d);
                break;
            }
        }
    }
}

} // namespace

void ProbeAudioEndpoints(std::vector<AudioEndpoint>& out) {
    // COM must be initialized before use; host_platform.cpp initializes COM as
    // apartment-threaded before probing.
    ProbeAudio(out);
}

void ProbeInputDevices(std::vector<InputDevice>& out) { ProbeInput(out); }

} // namespace dcwin
