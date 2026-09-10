// input_router_windows.cpp — GameInput-backed IInputRouter (DK0-M2 §34/§35).
// Owns the Guide/Share system buttons through RegisterSystemButtonCallback
// and SetFocusPolicy: the console, not the title, owns system actions.
// GameInput types stay inside this file.
#include "dc/input.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include "GameInput.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

using namespace GameInput::v3;

namespace dcwin {

namespace {

class GameInputRouter final : public dc::IInputRouter {
public:
    GameInputRouter() {
        if (SUCCEEDED(GameInputCreate(&gi_)) && gi_) {
            // Focus policy (§35): the console owns Guide/Share even while a
            // title holds the foreground. v3 policy is enable-based: request
            // background delivery for both system buttons; default input
            // gating stays per-title via the activation contract.
            gi_->SetFocusPolicy(static_cast<GameInputFocusPolicy>(
                GameInputEnableBackgroundGuideButton |
                GameInputEnableBackgroundShareButton));

            gi_->RegisterSystemButtonCallback(
                nullptr,
                static_cast<GameInputSystemButtons>(GameInputSystemButtonGuide |
                                                    GameInputSystemButtonShare),
                this,
                [](GameInputCallbackToken, void* context,
                   IGameInputDevice*, uint64_t,
                   GameInputSystemButtons currentButtons,
                   GameInputSystemButtons previousButtons) {
                    auto* self = static_cast<GameInputRouter*>(context);
                    GameInputSystemButtons pressed = currentButtons & ~previousButtons;
                    uint32_t add = 0;
                    if (pressed & GameInputSystemButtonGuide) add |= static_cast<uint32_t>(dc::ConsoleAction::Guide);
                    if (pressed & GameInputSystemButtonShare) add |= static_cast<uint32_t>(dc::ConsoleAction::Share);
                    if (add) self->pending_actions_.fetch_or(add, std::memory_order_relaxed);
                },
                &system_button_token_);
        }
    }

    ~GameInputRouter() override {
        if (gi_) {
            if (system_button_token_) gi_->UnregisterCallback(system_button_token_);
            gi_->Release();
        }
    }

    dc::ConsoleAction Poll(dc::ConsoleAction prior_actions) override {
        // Drain and return system actions detected since last poll.
        dc::ConsoleAction actions = static_cast<dc::ConsoleAction>(
            pending_actions_.exchange(0, std::memory_order_relaxed) |
            static_cast<uint32_t>(prior_actions));

        // Refresh the primary gamepad state (device 0 semantics for DK0-M2).
        IGameInputReading* reading = nullptr;
        if (gi_ && SUCCEEDED(gi_->GetCurrentReading(GameInputKindGamepad, nullptr, &reading)) && reading) {
            GameInputGamepadState gp{};
            if (reading->GetGamepadState(&gp)) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                last_state_.left_x = gp.leftThumbstickX;
                last_state_.left_y = gp.leftThumbstickY;
                last_state_.right_x = gp.rightThumbstickX;
                last_state_.right_y = gp.rightThumbstickY;
                last_state_.left_trigger = gp.leftTrigger;
                last_state_.right_trigger = gp.rightTrigger;
                last_state_.buttons = MapButtons(gp.buttons);
            }
            reading->Release();
        }
        return actions;
    }

    dc::GamepadState StateFor(dc::ConsolePlayerId) const override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return last_state_;
    }

    size_t DeviceCount() const override {
        if (!gi_) return 0;
        // Gamepad devices are those that produce gamepad readings; count via
        // the current reading's device plus the blocking enumeration cost is
        // avoided here — DK0-M2 uses a single active gamepad model.
        IGameInputReading* reading = nullptr;
        if (SUCCEEDED(gi_->GetCurrentReading(GameInputKindGamepad, nullptr, &reading))) {
            reading->Release();
            return 1;
        }
        return 0;
    }

private:
    static uint32_t MapButtons(GameInputGamepadButtons gi_buttons) {
        uint32_t out = 0;
        auto set = [&](dc::GamepadButton b, bool cond) {
            if (cond) out |= 1u << static_cast<uint32_t>(b);
        };
        set(dc::GamepadButton::South, gi_buttons & GameInputGamepadA);
        set(dc::GamepadButton::East, gi_buttons & GameInputGamepadB);
        set(dc::GamepadButton::West, gi_buttons & GameInputGamepadX);
        set(dc::GamepadButton::North, gi_buttons & GameInputGamepadY);
        set(dc::GamepadButton::DpadUp, gi_buttons & GameInputGamepadDPadUp);
        set(dc::GamepadButton::DpadDown, gi_buttons & GameInputGamepadDPadDown);
        set(dc::GamepadButton::DpadLeft, gi_buttons & GameInputGamepadDPadLeft);
        set(dc::GamepadButton::DpadRight, gi_buttons & GameInputGamepadDPadRight);
        set(dc::GamepadButton::LeftShoulder, gi_buttons & GameInputGamepadLeftShoulder);
        set(dc::GamepadButton::RightShoulder, gi_buttons & GameInputGamepadRightShoulder);
        set(dc::GamepadButton::LeftStickClick, gi_buttons & GameInputGamepadLeftThumbstick);
        set(dc::GamepadButton::RightStickClick, gi_buttons & GameInputGamepadRightThumbstick);
        set(dc::GamepadButton::Start, gi_buttons & GameInputGamepadMenu);
        set(dc::GamepadButton::Select, gi_buttons & GameInputGamepadView);
        return out;
    }

    IGameInput* gi_ = nullptr;
    GameInputCallbackToken system_button_token_ = 0;
    std::atomic<uint32_t> pending_actions_{0};
    mutable std::mutex state_mutex_;
    dc::GamepadState last_state_{};
};

} // namespace

} // namespace dcwin

namespace dc {

IInputRouter* CreateGameInputRouter() {
    return new (std::nothrow) dcwin::GameInputRouter();
}

void DestroyGameInputRouter(IInputRouter* r) {
    delete r;
}

} // namespace dc
