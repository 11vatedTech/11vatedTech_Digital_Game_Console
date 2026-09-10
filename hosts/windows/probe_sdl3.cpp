// probe_sdl3.cpp — SDL3 fallback input backend discovery (DK0-M1H; directive §18).
// SDL3 is the portable semantic fallback (future ConsoleOS helper), NOT the
// primary Windows input authority. This probe proves IInputBackend-style
// backend independence: same dc::InputDevice output as the GameInput probe.
// SDL3 is dynamically loaded so machines without SDL3.dll degrade honestly
// to the GameInput/XInput path (C10). SDL types stay inside this file.
//
// NOTE (dedup policy, directive §18): GameInput remains primary on Windows;
// the probe runner must never run both backends over the same physical device.
// The current hostprof flow uses GameInput-first and falls back — SDL3 here is
// exercised only when explicitly requested (input backend selection), so no
// duplicate events can occur in this phase.
#include "dc/capability.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace dcwin {

using namespace dc;

namespace {

struct SdlCtx {
    HMODULE dll = nullptr;

    ~SdlCtx() {
        if (dll) {
            // Best-effort quit; SDL_Quit is safe to call once initialized.
            SDL_Quit();
            FreeLibrary(dll);
        }
    }
};

// Function pointer types resolved dynamically.
using PFN_SDL_Init = bool (*)(SDL_InitFlags);
using PFN_SDL_GetError = const char* (*)(void);
using PFN_SDL_GetNumGamepads = int (*)(void);
using PFN_SDL_OpenGamepad = SDL_Gamepad* (*)(SDL_JoystickID);
using PFN_SDL_CloseGamepad = void (*)(SDL_Gamepad*);
using PFN_SDL_GetGamepadName = const char* (*)(SDL_Gamepad*);
using PFN_SDL_GetGamepadVendor = Uint16 (*)(SDL_Gamepad*);
using PFN_SDL_GetGamepadProduct = Uint16 (*)(SDL_Gamepad*);
using PFN_SDL_GetGamepadProductVersion = Uint16 (*)(SDL_Gamepad*);
using PFN_SDL_GetGamepadProperties = SDL_PropertiesID (*)(SDL_Gamepad*);
using PFN_SDL_GetStringProperty = const char* (*)(SDL_PropertiesID, const char*, const char*);
using PFN_SDL_DestroyProperties = void (*)(SDL_PropertiesID);
using PFN_SDL_GetJoystickFromID = SDL_Joystick* (*)(SDL_JoystickID);
using PFN_SDL_JoystickIsVirtual = bool (*)(SDL_JoystickID);

bool LoadSdl3(SdlCtx& ctx) {
    ctx.dll = LoadLibraryW(L"SDL3.dll");
    if (!ctx.dll) return false;
    return true;
}

void* Sym(HMODULE dll, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(dll, name));
}

} // namespace

// Returns the number of gamepads discovered through SDL3 (0 if SDL3 is not
// loadable — callers fall back to the primary backend in that case).
size_t ProbeSdl3Gamepads(std::vector<InputDevice>& out) {
    SdlCtx ctx;
    if (!LoadSdl3(ctx)) return 0;

    auto init = reinterpret_cast<PFN_SDL_Init>(Sym(ctx.dll, "SDL_Init"));
    auto get_error = reinterpret_cast<PFN_SDL_GetError>(Sym(ctx.dll, "SDL_GetError"));
    auto num_pads = reinterpret_cast<PFN_SDL_GetNumGamepads>(Sym(ctx.dll, "SDL_GetNumGamepads"));
    auto open_pad = reinterpret_cast<PFN_SDL_OpenGamepad>(Sym(ctx.dll, "SDL_OpenGamepad"));
    auto close_pad = reinterpret_cast<PFN_SDL_CloseGamepad>(Sym(ctx.dll, "SDL_CloseGamepad"));
    auto pad_name = reinterpret_cast<PFN_SDL_GetGamepadName>(Sym(ctx.dll, "SDL_GetGamepadName"));
    auto pad_vid = reinterpret_cast<PFN_SDL_GetGamepadVendor>(Sym(ctx.dll, "SDL_GetGamepadVendor"));
    auto pad_pid = reinterpret_cast<PFN_SDL_GetGamepadProduct>(Sym(ctx.dll, "SDL_GetGamepadProduct"));
    auto pad_ver = reinterpret_cast<PFN_SDL_GetGamepadProductVersion>(Sym(ctx.dll, "SDL_GetGamepadProductVersion"));
    auto pad_props = reinterpret_cast<PFN_SDL_GetGamepadProperties>(Sym(ctx.dll, "SDL_GetGamepadProperties"));
    auto get_str_prop = reinterpret_cast<PFN_SDL_GetStringProperty>(Sym(ctx.dll, "SDL_GetStringProperty"));
    auto destroy_props = reinterpret_cast<PFN_SDL_DestroyProperties>(Sym(ctx.dll, "SDL_DestroyProperties"));

    auto sdl_free = reinterpret_cast<void (*)(void*)>(Sym(ctx.dll, "SDL_free"));
    if (!init || !num_pads || !open_pad || !close_pad || !pad_name || !sdl_free) return 0;

    // Gamepad-only init: no video/audio side effects from a probe.
    if (!init(SDL_INIT_GAMEPAD)) return 0;

    // SDL_GetGamepads returns a malloc'd array of joystick instance IDs.
    using PFN_SDL_GetGamepads = SDL_JoystickID* (*)(int*);
    auto get_pads = reinterpret_cast<PFN_SDL_GetGamepads>(Sym(ctx.dll, "SDL_GetGamepads"));
    if (!get_pads) return 0;

    int count = 0;
    SDL_JoystickID* ids = get_pads(&count);
    size_t discovered = 0;
    for (int i = 0; ids && i < count; ++i) {
        SDL_Gamepad* pad = open_pad(ids[i]);
        if (!pad) continue;

        InputDevice d;
        d.backend = "sdl3";
        d.kind = "gamepad";
        const char* name = pad_name(pad);
        d.name = name ? name : "SDL3 Gamepad";
        Uint16 vid = pad_vid ? pad_vid(pad) : 0;
        Uint16 pid = pad_pid ? pad_pid(pad) : 0;
        Uint16 ver = pad_ver ? pad_ver(pad) : 0;
        char idbuf[32];
        std::snprintf(idbuf, sizeof(idbuf), "sdl3-%04x-%04x-%04x", vid, pid, ver);
        d.id = idbuf;
        d.connection = "unknown";
        d.battery_percent = -1; // battery probing added with the runtime session
        // Rumble capability via gamepad properties (SDL3 3.2+ API).
        d.haptics = false;
        if (pad_props && get_str_prop) {
            SDL_PropertiesID props = pad_props(pad);
            if (props) {
                d.haptics = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
                if (destroy_props) destroy_props(props);
            }
        }
        out.push_back(std::move(d));
        ++discovered;
        close_pad(pad);
    }
    if (ids) sdl_free(ids);
    return discovered;
}

} // namespace dcwin
