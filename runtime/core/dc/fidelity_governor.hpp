// dc/fidelity_governor.hpp — runtime Fidelity Governor (DK0-M4; ADR-0026).
//
// Deliberately SMALL (directive §19): it does not optimize, search, or
// invent states. It consumes the compiler's validated candidate set and
// current telemetry, chooses the best legal candidate, and paces changes
// with hysteresis + minimum dwell time. Selection is a pure function of
// (candidate set, budget, intent, telemetry history) → deterministic (§5).
//
// Decision events are low-frequency (§25): one record per adaptation, not
// per frame. High-frequency performance data stays in the telemetry system.
#pragma once

#include "dc/fidelity.hpp"
#include "dc/fidelity_compiler.hpp"

#include <deque>
#include <string>
#include <vector>

namespace dc::fidelity {

// Versioned governor policy (directive §22/§28: data, not code).
struct GovernorPolicy {
    std::string version = "1";
    int down_windows = 3;        // consecutive pressure windows before a step-down
    int up_windows = 6;          // consecutive healthy windows before a step-up
    int min_dwell_windows = 8;   // no change within N windows of the last change
    double pressure_ratio = 1.05;   // cost > ratio * budget ⇒ pressure
    double recovery_ratio = 0.85;   // cost < ratio * budget ⇒ healthy headroom
};

// One observation window (title-supplied, §10: measured, never fabricated —
// unknown metrics stay unknown).
struct TelemetryWindow {
    Metric gpu_ms;      // window-average GPU frame cost
    Metric cpu_ms;
    Metric vram_mb;     // current residency
    Metric io_mbps;
    bool thermal_throttled = false;  // host-provided; UNKNOWN if never set
};

// A journaled decision (directive §25 trace record).
struct DecisionEvent {
    uint64_t window_index = 0;
    std::string previous_candidate;
    std::string new_candidate;
    SelectionReason reason = SelectionReason::InitialSelection;
    std::string detail;      // binding axis / context, human-inspectable
};

// The governor. Not a singleton, not global (directive §13): the title
// session owns it explicitly and feeds it through ConsoleServices.
class FidelityGovernor {
public:
    FidelityGovernor(CandidateSet set, Budget budget, GovernorPolicy policy,
                     Intent intent);

    // Initial selection (directive §20): best legal candidate for the
    // current intent. Deterministic; recorded as INITIAL_SELECTION.
    Selection SelectInitial(const std::string& session_profile);

    // Advance one telemetry window (directive §22). Returns non-empty when
    // the selection changed. Enforces hysteresis (down_windows/up_windows),
    // minimum dwell, and never leaves the validated candidate set.
    std::vector<DecisionEvent> Observe(const TelemetryWindow& w);

    // Player intent change (§16): policy input, not a preset. Re-evaluates
    // deterministically; recorded as PLAYER_INTENT_CHANGED.
    std::vector<DecisionEvent> SetIntent(Intent i);

    // Developer diagnostics / deterministic visual capture ONLY (§35): pin
    // selection to one compiled candidate. The id must exist in the validated
    // set and admit the runtime budget — the governor never leaves the
    // artifact set, and adaptation is disabled while pinned.
    bool PinCandidate(const std::string& candidate_id);

    const Selection& current() const { return current_; }
    const GovernorPolicy& policy() const { return policy_; }
    const std::vector<DecisionEvent>& trace() const { return trace_; }
    bool initial_selected() const { return initial_selected_; }
    bool pinned() const { return pinned_; }

private:
    // `from` (current candidate) constrains dynamic-legal domain changes
    // (§23); null = launch-time selection where all domains are selectable.
    const Candidate* BestForIntent(Intent i, const Candidate* from) const;
    const Candidate* FindCandidate(const std::string& id) const;
    std::vector<DecisionEvent> TransitionTo(const Candidate* next, SelectionReason r,
                                            const std::string& detail);

    CandidateSet set_;
    Budget budget_;
    GovernorPolicy policy_;
    Intent intent_ = Intent::Automatic;

    Selection current_;
    bool initial_selected_ = false;

    // hysteresis state
    int pressure_windows_ = 0;   // consecutive windows in pressure
    int healthy_windows_ = 0;    // consecutive windows with safe headroom
    int since_change_ = 0;       // windows since last transition (dwell)
    uint64_t window_index_ = 0;
    bool pinned_ = false;        // developer pin (§35): adaptation suspended

    std::vector<DecisionEvent> trace_;
};

} // namespace dc::fidelity
