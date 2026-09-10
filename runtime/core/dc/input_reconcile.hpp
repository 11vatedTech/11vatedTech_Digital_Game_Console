// dc/input_reconcile.hpp — controller dedup/reconciliation policy (§K).
// Invariant: one physical controller must never surface as two players
// through two backends (GameInput/SDL3/XInput, ledger §27). The policy is a
// pure, host-agnostic function so it can be tested without hardware; hosts
// feed their per-backend device lists and apply the reconciled result.
//
// Policy (priority order):
//   1. primary backend devices (GameInput on Windows) are accepted;
//   2. a lower-priority device is accepted only when NO higher-priority
//      device was accepted, or when its hardware identity is known and
//      distinct from every accepted device;
//   3. matching or unknown identity under a claimed higher backend →
//      conservative duplicate suppression (a console must never emit a
//      ghost second player; showing one pad once is recoverable, counting
//      one pad twice is not).
//
// The function never invents devices: absent backends simply contribute
// nothing. Player slots are assigned deterministically in acceptance order
// (sorted by (backend_priority, id) for stability).
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace dc {

// Backend priority: lower value wins when the same physical device appears
// in several backends. Values align with the ledger §27 backend stack.
enum class BackendPriority : uint32_t {
    Primary = 0,    // GameInput (Windows)
    Secondary = 1,  // SDL3 (portable)
    Legacy = 2,     // XInput / RawInput floor
};

struct ReconcileDevice {
    std::string backend;      // informational: "gameinput" | "sdl3" | "xinput"
    std::string id;           // backend-specific device id (stable per backend)
    std::string hardware_id;  // cross-backend physical identity when known
                              // (e.g. Bluetooth/Wired path); empty if unknown
};

struct ReconciledDevice : ReconcileDevice {
    uint32_t player = 0;      // assigned slot, 1-based; 0 = unassigned
    uint32_t prio_value = 0;  // BackendPriority of the winning occurrence
};

// Deduplicate and assign players. Deterministic: identical input → identical
// output (byte-stable ordering). Same-id duplicates inside one backend are
// also collapsed (backend hiccup protection).
inline std::vector<ReconciledDevice> ReconcileControllers(
    const std::vector<ReconcileDevice>& primary,
    const std::vector<ReconcileDevice>& secondary,
    const std::vector<ReconcileDevice>& legacy) {
    struct Candidate {
        ReconcileDevice dev;
        BackendPriority prio;
    };
    std::vector<Candidate> candidates;
    for (const auto& d : primary)
        candidates.push_back({d, BackendPriority::Primary});
    for (const auto& d : secondary)
        candidates.push_back({d, BackendPriority::Secondary});
    for (const auto& d : legacy)
        candidates.push_back({d, BackendPriority::Legacy});

    // Deterministic acceptance order: backend priority, then id.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  if (a.prio != b.prio) {
                      return static_cast<uint32_t>(a.prio) <
                             static_cast<uint32_t>(b.prio);
                  }
                  if (a.dev.id != b.dev.id) return a.dev.id < b.dev.id;
                  return a.dev.backend < b.dev.backend;
              });

    std::vector<ReconciledDevice> accepted;
    for (auto& c : candidates) {
        bool higher_accepted = false;
        bool duplicate = false;
        for (const auto& a : accepted) {
            if (static_cast<uint32_t>(c.prio) > a.prio_value)
                higher_accepted = true;
            // Cross-backend match on hardware identity, or same-backend
            // match on backend id — either means the same physical device.
            if ((!c.dev.hardware_id.empty() &&
                 c.dev.hardware_id == a.hardware_id) ||
                (c.dev.backend == a.backend && c.dev.id == a.id)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        // Conservative suppression: a lower-backend device with unknown
        // hardware identity under an already-claimed higher backend is
        // presumed to be the same physical pad (§K invariant).
        if (higher_accepted && c.dev.hardware_id.empty()) continue;

        ReconciledDevice out;
        out.backend = c.dev.backend;
        out.id = c.dev.id;
        out.hardware_id = c.dev.hardware_id;
        out.player = static_cast<uint32_t>(accepted.size()) + 1;
        out.prio_value = static_cast<uint32_t>(c.prio);
        accepted.push_back(std::move(out));
    }
    return accepted;
}

} // namespace dc
