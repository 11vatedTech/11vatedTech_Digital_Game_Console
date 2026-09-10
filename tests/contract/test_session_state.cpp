// test_session_state.cpp — session state machine contract tests (DK0-M2 §42):
// legal transitions, illegal transitions, transition timeouts, crash recovery,
// shell recovery, full boot + title lifecycle walk, journal integrity.
#include "dc/session.hpp"

#include <cassert>
#include <cstdio>

using namespace dc;

namespace {

uint64_t Ns(uint64_t seconds) { return seconds * 1'000'000'000ull; }

void TestBootSequence() {
    SessionStateMachine sm;
    assert(sm.state() == SessionState::SessionOff);
    uint64_t t = 0;
    assert(sm.Transition(SessionState::SessionStarting, TransitionReason::BootSequence, t, 1) == TransitionResult::Ok);
    t += 1'000'000;
    assert(sm.Transition(SessionState::HostValidating, TransitionReason::BootSequence, t, 1) == TransitionResult::Ok);
    t += 1'000'000;
    assert(sm.Transition(SessionState::SessionCapabilitiesResolving, TransitionReason::BootSequence, t, 1) == TransitionResult::Ok);
    t += 1'000'000;
    assert(sm.Transition(SessionState::InputAcquiring, TransitionReason::BootSequence, t, 1) == TransitionResult::Ok);
    t += 1'000'000;
    assert(sm.Transition(SessionState::ShellStarting, TransitionReason::BootSequence, t, 1) == TransitionResult::Ok);
    t += 1'000'000;
    assert(sm.Transition(SessionState::ShellActive, TransitionReason::BootSequence, t, 1) == TransitionResult::Ok);
    assert(sm.state() == SessionState::ShellActive);
    assert(sm.Journal().size() == 6);
    std::printf("  boot sequence OK\n");
}

void TestIllegalTransitions() {
    SessionStateMachine sm;
    uint64_t t = 0;
    // Cannot jump from OFF directly to SHELL_ACTIVE.
    assert(sm.Transition(SessionState::ShellActive, TransitionReason::Requested, t, 2) == TransitionResult::IllegalTransition);
    assert(sm.state() == SessionState::SessionOff);
    // Bring up to shell, then attempt illegal jumps.
    assert(sm.Transition(SessionState::SessionStarting, TransitionReason::BootSequence, t, 2) == TransitionResult::Ok);
    assert(sm.Transition(SessionState::HostValidating, TransitionReason::BootSequence, t, 2) == TransitionResult::Ok);
    // Cannot start a title from HOST_VALIDATING.
    assert(sm.Transition(SessionState::TitleRequested, TransitionReason::Requested, t, 2) == TransitionResult::IllegalTransition);
    // Cannot exit mid-bring-up to a terminal from an invalid edge.
    assert(sm.Transition(SessionState::TitleActive, TransitionReason::Requested, t, 2) == TransitionResult::IllegalTransition);
    // Self-transition is illegal.
    assert(sm.Transition(SessionState::HostValidating, TransitionReason::None, t, 2) == TransitionResult::IllegalTransition);
    std::printf("  illegal transitions rejected OK\n");
}

void TestTitleLifecycle() {
    SessionStateMachine sm;
    uint64_t t = 0;
    auto step = [&](SessionState s, TransitionReason r) {
        assert(sm.Transition(s, r, t, 100) == TransitionResult::Ok);
        t += 1'000'000;
    };
    step(SessionState::SessionStarting, TransitionReason::BootSequence);
    step(SessionState::HostValidating, TransitionReason::BootSequence);
    step(SessionState::SessionCapabilitiesResolving, TransitionReason::BootSequence);
    step(SessionState::InputAcquiring, TransitionReason::BootSequence);
    step(SessionState::ShellStarting, TransitionReason::BootSequence);
    step(SessionState::ShellActive, TransitionReason::BootSequence);

    step(SessionState::TitleRequested, TransitionReason::Requested);
    step(SessionState::TitleValidating, TransitionReason::Requested);
    step(SessionState::TitleStarting, TransitionReason::Requested);
    step(SessionState::TitleActive, TransitionReason::Requested);

    // Guide press → suspend path (canon §9).
    step(SessionState::TitleSuspended, TransitionReason::GuideActivated);
    step(SessionState::TitleResuming, TransitionReason::Requested);
    step(SessionState::TitleActive, TransitionReason::Requested);

    // Clean exit → shell recovery → shell.
    step(SessionState::TitleStopping, TransitionReason::TitleExitClean);
    step(SessionState::ShellRecovering, TransitionReason::TitleExitClean);
    step(SessionState::ShellActive, TransitionReason::TitleExitClean);
    assert(sm.state() == SessionState::ShellActive);
    std::printf("  title lifecycle (suspend/resume/stop) OK\n");
}

void TestCrashRecovery() {
    SessionStateMachine sm;
    uint64_t t = 0;
    auto step = [&](SessionState s, TransitionReason r) {
        assert(sm.Transition(s, r, t, 200) == TransitionResult::Ok);
        t += 1'000'000;
    };
    step(SessionState::SessionStarting, TransitionReason::BootSequence);
    step(SessionState::HostValidating, TransitionReason::BootSequence);
    step(SessionState::SessionCapabilitiesResolving, TransitionReason::BootSequence);
    step(SessionState::InputAcquiring, TransitionReason::BootSequence);
    step(SessionState::ShellStarting, TransitionReason::BootSequence);
    step(SessionState::ShellActive, TransitionReason::BootSequence);
    step(SessionState::TitleRequested, TransitionReason::Requested);
    step(SessionState::TitleValidating, TransitionReason::Requested);
    step(SessionState::TitleStarting, TransitionReason::Requested);
    step(SessionState::TitleActive, TransitionReason::Requested);

    // Crash: TITLE_ACTIVE → TITLE_CRASHED → RECOVERY → SHELL_ACTIVE (§37).
    step(SessionState::TitleCrashed, TransitionReason::TitleCrash);
    step(SessionState::Recovery, TransitionReason::TitleCrash);
    step(SessionState::ShellActive, TransitionReason::TitleCrash);
    assert(sm.state() == SessionState::ShellActive);

    // Crash during STARTING is also legal (title never became active).
    step(SessionState::TitleRequested, TransitionReason::Requested);
    step(SessionState::TitleValidating, TransitionReason::Requested);
    step(SessionState::TitleStarting, TransitionReason::Requested);
    step(SessionState::TitleCrashed, TransitionReason::TitleCrash);
    step(SessionState::ShellRecovering, TransitionReason::TitleCrash);
    step(SessionState::ShellActive, TransitionReason::TitleCrash);
    std::printf("  crash recovery paths OK\n");
}

void TestTimeouts() {
    SessionStateMachine sm;
    uint64_t t = 0;
    assert(sm.Transition(SessionState::SessionStarting, TransitionReason::BootSequence, t, 300) == TransitionResult::Ok);

    // Before the 5 s deadline: no timeout.
    t = Ns(4);
    assert(!sm.CheckTimeout(t));
    assert(sm.state() == SessionState::SessionStarting);

    // After the deadline: forced to RECOVERY with timed_out recorded.
    t = Ns(6);
    assert(sm.CheckTimeout(t));
    assert(sm.state() == SessionState::Recovery);
    const auto& j = sm.Journal();
    assert(!j.empty() && j.back().timed_out);
    assert(j.back().reason == TransitionReason::TimeoutExpired);

    // Steady states have no deadline.
    assert(SessionStateMachine::DefaultTimeoutNs(SessionState::ShellActive) == 0);
    assert(SessionStateMachine::DefaultTimeoutNs(SessionState::SessionOff) == 0);
    // Title start has the longest budget.
    assert(SessionStateMachine::DefaultTimeoutNs(SessionState::TitleStarting) == Ns(30));
    std::printf("  transition timeouts OK\n");
}

void TestTimeoutTitleStartRejectsToShell() {
    SessionStateMachine sm;
    uint64_t t = 0;
    auto step = [&](SessionState s, TransitionReason r) {
        assert(sm.Transition(s, r, t, 400) == TransitionResult::Ok);
        t += 1'000'000;
    };
    step(SessionState::SessionStarting, TransitionReason::BootSequence);
    step(SessionState::HostValidating, TransitionReason::BootSequence);
    step(SessionState::SessionCapabilitiesResolving, TransitionReason::BootSequence);
    step(SessionState::InputAcquiring, TransitionReason::BootSequence);
    step(SessionState::ShellStarting, TransitionReason::BootSequence);
    step(SessionState::ShellActive, TransitionReason::BootSequence);
    step(SessionState::TitleRequested, TransitionReason::Requested);
    step(SessionState::TitleValidating, TransitionReason::Requested);
    step(SessionState::TitleStarting, TransitionReason::Requested);
    // Title hangs 31 s in TITLE_STARTING → timeout returns to SHELL_ACTIVE
    // (reject-to-shell, not RECOVERY — the shell is still healthy).
    t += Ns(31);
    assert(sm.CheckTimeout(t));
    assert(sm.state() == SessionState::ShellActive);
    assert(sm.Journal().back().timed_out);
    std::printf("  title-start timeout → shell OK\n");
}

void TestJournalIntegrity() {
    SessionStateMachine sm;
    uint64_t t = 0;
    sm.Transition(SessionState::SessionStarting, TransitionReason::BootSequence, t, 500);
    t += 1'000'000;
    sm.Transition(SessionState::HostValidating, TransitionReason::BootSequence, t, 500);
    // Illegal attempts are journaled but do not change state.
    sm.Transition(SessionState::ShellActive, TransitionReason::Requested, t, 500);
    const auto& j = sm.Journal();
    assert(j.size() == 3);
    assert(j[0].from == SessionState::SessionOff && j[0].to == SessionState::SessionStarting);
    assert(j[0].monotonic_ns == 0 && j[0].correlation_id == 500);
    assert(j[2].from == j[2].to); // rejected edge recorded as no-op
    assert(sm.state() == SessionState::HostValidating);
    std::printf("  journal integrity OK\n");
}

} // namespace

int main() {
    std::printf("session_state contract tests:\n");
    TestBootSequence();
    TestIllegalTransitions();
    TestTitleLifecycle();
    TestCrashRecovery();
    TestTimeouts();
    TestTimeoutTitleStartRejectsToShell();
    TestJournalIntegrity();
    std::printf("all session_state tests passed\n");
    return 0;
}
