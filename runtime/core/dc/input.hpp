// dc/input.hpp — console semantic input model (DK0-M2 §15/§19).
// Backend-independent types: GameInput/SDL3/XInput details never cross this
// boundary (directive §14). Buttons represent positions/actions, not vendor
// labels; glyph resolution is a shell concern.
#pragma once

#include <cstdint>

namespace dc {

using ConsoleDeviceId = uint64_t;  // stable per-device id (backend-assigned)
using ConsolePlayerId = uint32_t;  // player slot (0 = unassigned)

// Semantic gamepad buttons (positions/actions, vendor-neutral).
enum class GamepadButton : uint32_t {
    None = 0,
    South,      // accept (bottom face)
    East,       // back/cancel (right face)
    West,       // context (left face)
    North,      // alternate (top face)
    DpadUp, DpadDown, DpadLeft, DpadRight,
    LeftShoulder, RightShoulder,
    LeftStickClick, RightStickClick,
    Start,      // menu/options
    Select,     // view/share-adjacent
};

struct GamepadState {
    float left_x = 0.f, left_y = 0.f;
    float right_x = 0.f, right_y = 0.f;
    float left_trigger = 0.f;
    float right_trigger = 0.f;
    uint32_t buttons = 0; // bitmask of (1u << GamepadButton)
};

// Platform-owned system actions. Native titles NEVER receive these; the
// router intercepts them before title delivery (§19/§34).
enum class ConsoleAction : uint32_t {
    None = 0,
    Guide = 1 << 0,   // system button: shell/Guide ownership
    Share = 1 << 1,   // capture/share system button
    Home = 1 << 2,    // long-press Guide equivalent on some pads
};

struct ConsoleDeviceCapabilities {
    bool haptics = false;
    bool gyro = false;
    bool touch_surface = false;
    bool adaptive_triggers = false;
};

struct ConsoleBatteryState {
    enum class Kind { Unknown, NotPresent, Discharging, Idle, Charging };
    Kind kind = Kind::Unknown;
    uint8_t percent = 0; // valid when kind != Unknown
};

// The input router: physical devices → backend → identity → player
// assignment → SYSTEM ACTION FILTER → semantic state → shell/title (§19).
class IInputRouter {
public:
    virtual ~IInputRouter() = default;

    // Poll one frame: refresh device list and readings. Returns the OR of
    // system actions detected this frame (consumed by the session layer).
    virtual ConsoleAction Poll(ConsoleAction prior_actions) = 0;

    // Latest semantic state for a player slot (post-filter).
    virtual GamepadState StateFor(ConsolePlayerId player) const = 0;

    virtual size_t DeviceCount() const = 0;
};

} // namespace dc
