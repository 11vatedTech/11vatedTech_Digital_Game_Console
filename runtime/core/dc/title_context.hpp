// dc/title_context.hpp — native title runtime context (DK0-M2 §30 → M3 seed).
// Canon DC-CANON-001 §16: the runtime injects a typed context; titles never
// parse raw platform state. This is the transport embryo: the supervisor
// serializes LaunchContext into a versioned JSON envelope (DC_TITLE_CONTEXT/1)
// and the title parses it back with the core strict JSON parser. The
// ConsoleServices pipe (canon §16.2) will replace the env-var transport in
// DK0-M3 without changing these types — they are the durable contract.
//
// Trust boundary (§C11): the envelope carries session truth but no secrets.
// Titles SHOULD treat it as advisory capability information; certification
// gates remain the runtime's authority, not the title's.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dc {

// Version of the launch-context envelope. Bump on any breaking field change;
// titles MUST reject envelopes whose schema id they do not understand.
inline constexpr const char* kTitleContextSchema = "dc.title-context/1";

struct TitleLaunchContext {
    // Schema/version envelope.
    std::string schema = kTitleContextSchema;

    // Identity (§30).
    std::string title_id;
    std::string title_version;
    std::string session_id;      // correlation id for journaling/telemetry
    uint64_t    user_id = 0;     // 0 = offline single-user profile (C9)

    // Capability truth (ADR-0022: host ≠ session). These are the JSON
    // documents produced by the qualification pipeline (dc.host-capability/2
    // shape), embedded verbatim so the title sees exactly what the platform
    // certified — no shell-side recombination (§40).
    std::string host_capability_json;     // dc.host-capability/2 document
    std::string session_experience_json;  // dc.session-experience/1 document
    // Session experience claims the active display path currently satisfies
    // (e.g. "DCX-UHD60"); empty when the panel provides none of them.
    std::vector<std::string> active_session_profiles;

    // Display context (rational refresh preserved — ADR-0022).
    uint32_t display_width = 0;
    uint32_t display_height = 0;
    uint32_t refresh_numerator = 0;    // refresh_hz = numerator / denominator
    uint32_t refresh_denominator = 0;

    // Lifecycle contract (§28/§33): system actions are platform-owned.
    bool controller_required = true;
    bool guide_owned_by_platform = true;
    bool offline_launch = true;        // C9: local-first session
};

} // namespace dc
