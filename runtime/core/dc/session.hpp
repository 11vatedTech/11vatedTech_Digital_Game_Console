// dc/session.hpp — Digital Console session state machine (DK0-M2; canon §9,
// directive §25). Host-agnostic core: transitions, journaling, timeouts.
// Determinism: every transition carries a monotonic timestamp supplied by the
// caller (no hidden clock reads inside the state machine itself).
//
// State set (directive §25; superset of canon §9 boot flow, session-scoped):
//   SESSION_OFF → SESSION_STARTING → HOST_VALIDATING →
//   SESSION_CAPABILITIES_RESOLVING → INPUT_ACQUIRING → SHELL_STARTING →
//   SHELL_ACTIVE
// Title path:
//   SHELL_ACTIVE → TITLE_REQUESTED → TITLE_VALIDATING → TITLE_STARTING →
//   TITLE_ACTIVE → TITLE_STOPPING → SHELL_RECOVERING → SHELL_ACTIVE
// Failure:
//   TITLE_ACTIVE → TITLE_CRASHED → RECOVERY → SHELL_ACTIVE
//   (RECOVERY may also exit to SESSION_EXIT or SAFE_MODE)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dc {

enum class SessionState {
    SessionOff,
    SessionStarting,
    HostValidating,
    SessionCapabilitiesResolving,
    InputAcquiring,
    ShellStarting,
    ShellActive,
    TitleRequested,
    TitleValidating,
    TitleStarting,
    TitleActive,
    TitleStopping,
    TitleSuspended,
    TitleResuming,
    TitleCrashed,
    ShellRecovering,
    Recovery,
    SessionExit,
    SafeMode,
};

const char* SessionStateName(SessionState s);

// Why a transition happened (stable reason codes; journal + diagnostics).
enum class TransitionReason {
    None,
    Requested,            // user/shell action
    BootSequence,         // normal session bring-up
    TitleExitClean,       // title returned cleanly
    TitleCrash,           // supervisor detected abnormal termination
    TitleHang,            // supervisor timeout
    GuideActivated,       // system button routing
    TopologyChanged,      // display/audio/input topology event
    CapabilityInvalidated,// host/session capability stale
    TimeoutExpired,       // transition deadline exceeded
    FatalError,           // unrecoverable internal error
    UserExit,             // user chose to end the session
};

const char* TransitionReasonName(TransitionReason r);

// Result of an attempted transition.
enum class TransitionResult {
    Ok,
    IllegalTransition,   // no edge between these states
    TimeoutExpired,      // deadline for the current state passed
    InvalidState,        // unknown state value
};

struct SessionTransition {
    SessionState from = SessionState::SessionOff;
    SessionState to = SessionState::SessionOff;
    TransitionReason reason = TransitionReason::None;
    uint64_t monotonic_ns = 0;   // caller-supplied monotonic timestamp
    uint64_t correlation_id = 0; // links related transitions (session/title ids)
    uint64_t timeout_ns = 0;     // deadline budget for the transition (0 = none)
    bool timed_out = false;      // deadline was exceeded
};

// Session journal: append-only, in-memory core record of transitions.
// The session process persists this to disk (dc-session journal, §26); the
// core keeps the typed truth. A crash cannot destroy previously appended
// entries because the journal is append-only by contract.
class SessionStateMachine {
public:
    SessionStateMachine();

    SessionState state() const { return state_; }
    uint64_t state_entered_ns() const { return state_entered_ns_; }

    // Attempt a transition. Returns Ok and appends to the journal on success.
    TransitionResult Transition(SessionState to, TransitionReason reason,
                                uint64_t monotonic_ns, uint64_t correlation_id);

    // Check whether the time spent in the current state exceeds its deadline.
    // Returns true and force-transitions to RECOVERY when a timeout fires.
    bool CheckTimeout(uint64_t monotonic_ns);

    // Deadline budget for a state (0 = no deadline). Values are conservative
    // development defaults; the session process may override via policy.
    static uint64_t DefaultTimeoutNs(SessionState s);

    const std::vector<SessionTransition>& Journal() const { return journal_; }

    // Legal-edge query (exposed for tests and the shell's UI gating).
    static bool IsLegal(SessionState from, SessionState to);

private:
    SessionState state_ = SessionState::SessionOff;
    uint64_t state_entered_ns_ = 0;
    std::vector<SessionTransition> journal_;
};

} // namespace dc
