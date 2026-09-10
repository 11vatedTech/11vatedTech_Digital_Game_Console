// probe_gameinput.cpp — GameInput device discovery (DK0-M1H; directive §13/§16).
// GameInput is the primary Windows input backend; XInput remains the legacy
// fallback floor in probe_devices.cpp. The GameInput runtime is dynamically
// loaded (GameInputCreate via GetProcAddress) so a machine without the
// redistributable degrades honestly to the XInput floor (C10). All GameInput
// types stay inside this file; consumers see dc::InputDevice only.
//
// API surface: Microsoft.GameInput NuGet 3.5.270 (GAMEINPUT_API_VERSION 3),
// namespace GameInput::v3. Runtime verified on DevKit-0: GameInputRedist.dll
// 4.3.0.218 exports GameInputCreate directly.
#include "dc/capability.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "GameInput.h"

using namespace GameInput::v3;

namespace dcwin {

using namespace dc;

namespace {

std::string DeviceIdFromInfo(const GameInputDeviceInfo* info) {
    // APP_LOCAL_DEVICE_ID is a 32-byte binary id (windef.h); render as hex.
    char buf[65];
    static const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        buf[i * 2] = hex[info->deviceId.value[i] >> 4];
        buf[i * 2 + 1] = hex[info->deviceId.value[i] & 0xF];
    }
    buf[64] = '\0';
    return std::string("gameinput-") + buf;
}

void CALLBACK OnDeviceCallback(GameInputCallbackToken /*callbackToken*/, void* context,
                               IGameInputDevice* device, uint64_t /*timestamp*/,
                               GameInputDeviceStatus currentStatus,
                               GameInputDeviceStatus /*previousStatus*/) {
    if (context && device && currentStatus == GameInputDeviceConnected) {
        static_cast<std::vector<IGameInputDevice*>*>(context)->push_back(device);
        device->AddRef(); // callback does not retain for us
    }
}

const char* KindFromInputKind(GameInputKind kind) {
    if (kind & GameInputKindGamepad) return "gamepad";
    if (kind & GameInputKindRacingWheel) return "wheel";
    if (kind & GameInputKindFlightStick) return "flightstick";
    if (kind & GameInputKindArcadeStick) return "arcade_stick";
    if (kind & GameInputKindSensors) return "sensors";
    return "other";
}

} // namespace

// Returns the number of devices discovered through GameInput (0 if the
// runtime is unavailable — callers keep the XInput floor in that case).
size_t ProbeGameInputDevices(std::vector<InputDevice>& out) {
    // Direct link through GameInput.lib (NuGet Microsoft.GameInput 3.5.270).
    // Do NOT LoadLibrary the runtime here: dynamically loading a mismatched
    // GameInput.dll/Redist pairing crashed inside RegisterDeviceCallback on
    // DevKit-0 (recorded in RESEARCH_LEDGER §26). The static import pins the
    // correct runtime pairing and resolves GameInputCreate from the redist.
    IGameInput* gi = nullptr;
    if (FAILED(GameInputCreate(&gi)) || !gi) return 0;

    size_t discovered = 0;

    // Console input kinds: gamepad-class devices only. Keyboard/mouse are
    // deliberately excluded — they are not console input devices (C3).
    constexpr GameInputKind kConsoleKinds = static_cast<GameInputKind>(
        GameInputKindGamepad | GameInputKindRacingWheel |
        GameInputKindFlightStick | GameInputKindArcadeStick);

    // Authoritative enumeration: blocking device callback (works with zero
    // readings, unlike reading-history walking).
    std::vector<IGameInputDevice*> seen;

    GameInputCallbackToken token = 0;
    HRESULT hr = gi->RegisterDeviceCallback(
        nullptr, GameInputKindGamepad, GameInputDeviceConnected,
        GameInputBlockingEnumeration, &seen, OnDeviceCallback, &token);
    if (FAILED(hr)) { gi->Release(); return 0; }
    gi->UnregisterCallback(token);

    for (IGameInputDevice* dev : seen) {
        const GameInputDeviceInfo* info = nullptr;
        if (FAILED(dev->GetDeviceInfo(&info)) || !info) { dev->Release(); continue; }
        InputDevice d;
        d.id = DeviceIdFromInfo(info);
        d.kind = KindFromInputKind(info->supportedInput);
        d.name = info->displayName ? info->displayName : "GameInput Device";
        d.backend = "gameinput";
        d.connection = "unknown";
        d.haptics = info->supportedRumbleMotors != GameInputRumbleNone
                        || info->forceFeedbackMotorCount > 0;
        // Battery: not exposed by the v3 device interface; record unknown
        // rather than inventing a level (C10).
        d.battery_percent = -1;
        // Gyro/touch/adaptive triggers are only claimed when the device
        // actually reports a sensors surface (v3 exposes sensorsInfo).
        d.gyro = (info->supportedInput & GameInputKindSensors) != 0 &&
                 info->sensorsInfo != nullptr;
        d.touch_surface = false;
        out.push_back(std::move(d));
        ++discovered;
        dev->Release();
    }

    gi->Release();
    return discovered;
}

} // namespace dcwin
