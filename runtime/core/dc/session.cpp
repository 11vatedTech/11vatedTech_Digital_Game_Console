// session.cpp — state machine edges, names, deadlines (DK0-M2).
// Legal-edge table is the single source of truth for session flow (§25).
#include "dc/session.hpp"

#include <unordered_set>

namespace dc {

namespace {

// Edge table: from → legal successors. Anything not listed is illegal.
struct Edge { SessionState from; std::unordered_set<SessionState> to; };

const Edge kEdges[] = {
    // Bring-up
    {SessionState::SessionOff,
     {SessionState::SessionStarting}},
    {SessionState::SessionStarting,
     {SessionState::HostValidating, SessionState::Recovery, SessionState::SessionExit}},
    {SessionState::HostValidating,
     {SessionState::SessionCapabilitiesResolving, SessionState::Recovery,
      SessionState::SessionExit, SessionState::SafeMode}},
    {SessionState::SessionCapabilitiesResolving,
     {SessionState::InputAcquiring, SessionState::Recovery, SessionState::SessionExit}},
    {SessionState::InputAcquiring,
     {SessionState::ShellStarting, SessionState::Recovery, SessionState::SessionExit}},
    {SessionState::ShellStarting,
     {SessionState::ShellActive, SessionState::Recovery, SessionState::SessionExit}},

    // Shell steady state
    {SessionState::ShellActive,
     {SessionState::TitleRequested, SessionState::SessionExit,
      SessionState::Recovery, SessionState::SafeMode}},

    // Title lifecycle
    {SessionState::TitleRequested,
     {SessionState::TitleValidating, SessionState::ShellActive}}, // reject → back to shell
    {SessionState::TitleValidating,
     {SessionState::TitleStarting, SessionState::ShellActive}},   // invalid title → shell
    {SessionState::TitleStarting,
     {SessionState::TitleActive, SessionState::TitleCrashed,
      SessionState::ShellRecovering, SessionState::Recovery}},
    {SessionState::TitleActive,
     {SessionState::TitleStopping, SessionState::TitleSuspended,
      SessionState::TitleCrashed, SessionState::ShellRecovering, SessionState::Recovery}},
    {SessionState::TitleSuspended,
     {SessionState::TitleResuming, SessionState::TitleStopping, SessionState::TitleCrashed}},
    {SessionState::TitleResuming,
     {SessionState::TitleActive, SessionState::TitleCrashed, SessionState::ShellRecovering}},
    {SessionState::TitleStopping,
     {SessionState::ShellRecovering, SessionState::TitleCrashed}},
    {SessionState::TitleCrashed,
     {SessionState::ShellRecovering, SessionState::Recovery}},
    {SessionState::ShellRecovering,
     {SessionState::ShellActive, SessionState::Recovery, SessionState::SafeMode}},

    // Recovery outcomes
    {SessionState::Recovery,
     {SessionState::ShellActive, SessionState::SessionExit, SessionState::SafeMode}},

    // Terminals
    {SessionState::SessionExit, {}},
    {SessionState::SafeMode, {}},
};

const Edge* FindEdge(SessionState from) {
    for (const auto& e : kEdges) {
        if (e.from == from) return &e;
    }
    return nullptr;
}

} // namespace

const char* SessionStateName(SessionState s) {
    switch (s) {
        case SessionState::SessionOff: return "SESSION_OFF";
        case SessionState::SessionStarting: return "SESSION_STARTING";
        case SessionState::HostValidating: return "HOST_VALIDATING";
        case SessionState::SessionCapabilitiesResolving: return "SESSION_CAPABILITIES_RESOLVING";
        case SessionState::InputAcquiring: return "INPUT_ACQUIRING";
        case SessionState::ShellStarting: return "SHELL_STARTING";
        case SessionState::ShellActive: return "SHELL_ACTIVE";
        case SessionState::TitleRequested: return "TITLE_REQUESTED";
        case SessionState::TitleValidating: return "TITLE_VALIDATING";
        case SessionState::TitleStarting: return "TITLE_STARTING";
        case SessionState::TitleActive: return "TITLE_ACTIVE";
        case SessionState::TitleStopping: return "TITLE_STOPPING";
        case SessionState::TitleSuspended: return "TITLE_SUSPENDED";
        case SessionState::TitleResuming: return "TITLE_RESUMING";
        case SessionState::TitleCrashed: return "TITLE_CRASHED";
        case SessionState::ShellRecovering: return "SHELL_RECOVERING";
        case SessionState::Recovery: return "RECOVERY";
        case SessionState::SessionExit: return "SESSION_EXIT";
        case SessionState::SafeMode: return "SAFE_MODE";
    }
    return "UNKNOWN";
}

const char* TransitionReasonName(TransitionReason r) {
    switch (r) {
        case TransitionReason::None: return "none";
        case TransitionReason::Requested: return "requested";
        case TransitionReason::BootSequence: return "boot_sequence";
        case TransitionReason::TitleExitClean: return "title_exit_clean";
        case TransitionReason::TitleCrash: return "title_crash";
        case TransitionReason::TitleHang: return "title_hang";
        case TransitionReason::GuideActivated: return "guide_activated";
        case TransitionReason::TopologyChanged: return "topology_changed";
        case TransitionReason::CapabilityInvalidated: return "capability_invalidated";
        case TransitionReason::TimeoutExpired: return "timeout_expired";
        case TransitionReason::FatalError: return "fatal_error";
        case TransitionReason::UserExit: return "user_exit";
    }
    return "unknown";
}

bool SessionStateMachine::IsLegal(SessionState from, SessionState to) {
    const Edge* e = FindEdge(from);
    if (!e) return false;
    return e->to.count(to) != 0;
}

uint64_t SessionStateMachine::DefaultTimeoutNs(SessionState s) {
    // Conservative development defaults (§25: every transition has a timeout).
    // 5 s for mechanical steps, 15 s for capability/bring-up resolution,
    // 30 s for title start (disk + shader first-run), 10 s for recoveries.
    constexpr uint64_t kNs = 1'000'000'000ull;
    switch (s) {
        case SessionState::SessionStarting: return 5 * kNs;
        case SessionState::HostValidating: return 15 * kNs;
        case SessionState::SessionCapabilitiesResolving: return 15 * kNs;
        case SessionState::InputAcquiring: return 5 * kNs;
        case SessionState::ShellStarting: return 5 * kNs;
        case SessionState::TitleValidating: return 5 * kNs;
        case SessionState::TitleStarting: return 30 * kNs;
        case SessionState::TitleStopping: return 10 * kNs;
        case SessionState::TitleResuming: return 10 * kNs;
        case SessionState::ShellRecovering: return 10 * kNs;
        case SessionState::Recovery: return 30 * kNs;
        default: return 0; // steady states have no deadline
    }
}

SessionStateMachine::SessionStateMachine() = default;

TransitionResult SessionStateMachine::Transition(SessionState to, TransitionReason reason,
                                                 uint64_t monotonic_ns,
                                                 uint64_t correlation_id) {
    SessionTransition t;
    t.from = state_;
    t.to = to;
    t.reason = reason;
    t.monotonic_ns = monotonic_ns;
    t.correlation_id = correlation_id;
    t.timeout_ns = DefaultTimeoutNs(state_);

    if (to == state_) return TransitionResult::IllegalTransition;
    if (!IsLegal(state_, to)) {
        journal_.push_back(t);
        t.timed_out = false;
        return TransitionResult::IllegalTransition;
    }

    state_ = to;
    state_entered_ns_ = monotonic_ns;
    journal_.push_back(t);
    return TransitionResult::Ok;
}

bool SessionStateMachine::CheckTimeout(uint64_t monotonic_ns) {
    const uint64_t budget = DefaultTimeoutNs(state_);
    if (budget == 0) return false;
    if (monotonic_ns <= state_entered_ns_) return false;
    if (monotonic_ns - state_entered_ns_ < budget) return false;

    // Deadline exceeded: force the recovery edge for this state.
    SessionState recovery_target;
    switch (state_) {
        case SessionState::SessionStarting:
        case SessionState::HostValidating:
        case SessionState::SessionCapabilitiesResolving:
        case SessionState::InputAcquiring:
        case SessionState::ShellStarting:
            recovery_target = SessionState::Recovery;
            break;
        case SessionState::TitleRequested:
        case SessionState::TitleValidating:
            recovery_target = SessionState::ShellActive; // reject, back to shell
            break;
        case SessionState::TitleStarting:
        case SessionState::TitleStopping:
        case SessionState::TitleResuming:
        case SessionState::TitleCrashed:
        case SessionState::ShellRecovering:
            recovery_target = SessionState::Recovery;
            break;
        default:
            return false; // no deadline here anyway
    }

    SessionTransition t;
    t.from = state_;
    t.to = recovery_target;
    t.reason = TransitionReason::TimeoutExpired;
    t.monotonic_ns = monotonic_ns;
    t.timeout_ns = budget;
    t.timed_out = true;
    state_ = recovery_target;
    state_entered_ns_ = monotonic_ns;
    journal_.push_back(t);
    return true;
}

} // namespace dc
