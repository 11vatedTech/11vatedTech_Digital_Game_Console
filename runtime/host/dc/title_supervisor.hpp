// dc/title_supervisor.hpp — native title supervision (DK0-M2 §27).
// The supervisor owns the full title process tree through a Windows Job
// Object: launch, tracking, clean exit request, timeout, forced termination,
// crash detection, and child cleanup. Titles never outlive the session.
// Windows types stay in the Windows implementation; this interface is the
// host-agnostic contract (canon §39.1).
#pragma once

#include <cstdint>
#include <string>

#include "dc/title_context.hpp"

namespace dc {

enum class TitleExitKind {
    NotStarted,
    Clean,          // process exited with a normal code
    Crashed,        // abnormal termination (exception/access violation)
    Terminated,     // supervisor forced termination (timeout/user)
    StillRunning,   // wait timed out but the process lives (caller decides)
};

struct TitleLaunchResult {
    bool ok = false;
    uint32_t process_id = 0;
    std::string error; // HRESULT or Win32 message on failure
};

struct TitleExitReport {
    TitleExitKind kind = TitleExitKind::NotStarted;
    uint32_t exit_code = 0;
    bool job_cleanup_complete = true; // all child processes were killed
};

// Result of a SuspendGame/ResumeGame request (§19 QR1 boundary).
enum class TitleNotifyResult {
    Ok,
    NotRunning,
    Failed,
};
const char* TitleNotifyResultName(TitleNotifyResult r);

// §19 QR1 lifecycle messages (window channel, WM_APP range — Win32 guarantees
// titles forward unknown WM_APP+X to DefWindowProc, so old titles ignore
// them safely). Shared by supervisor (sender) and title (handler).
inline constexpr uint32_t kTitleMsgSuspend = 0xD350; // WM_APP|'SP'
inline constexpr uint32_t kTitleMsgResume  = 0xD352; // WM_APP|'SR'

// Event-name derivation shared by supervisor and title: the ACK event is
// "DC_TITLE_EVT_<title_pid>_ACK"; the title creates it at startup (auto-reset)
// and SetEvents it when a checkpoint/resume completes.
inline void TitleEventName(const wchar_t* what, unsigned long pid,
                           wchar_t* out, size_t cch) {
    _snwprintf_s(out, cch, _TRUNCATE, L"DC_TITLE_EVT_%lu_%s", pid, what);
}

class ITitleSupervisor {
public:
    virtual ~ITitleSupervisor() = default;

    // Launch the title executable with the given working directory and
    // command line. The process (and its whole tree) joins a Job Object
    // owned by the supervisor. The job is configured so that a session
    // teardown kills every descendant even if the title misbehaves.
    virtual TitleLaunchResult Launch(const std::string& executable,
                                     const std::string& working_dir,
                                     const std::string& command_line) = 0;

    // Typed launch contract (canon §16 embryo): the supervisor serializes the
    // LaunchContext into the versioned DC_TITLE_CONTEXT/1 envelope and the
    // title parses it back. Transport is an environment variable so the
    // context never appears on a command line (visible via tasklist) and
    // never requires a file path contract.
    virtual TitleLaunchResult Launch(const std::string& executable,
                                     const std::string& working_dir,
                                     const std::string& command_line,
                                     const TitleLaunchContext& context) {
        // Default: transport-unaware supervisors fall back to the plain
        // launch; titles then see an absent envelope and use defaults.
        (void)context;
        return Launch(executable, working_dir, command_line);
    }

    // Non-blocking status: true while the title process is alive.
    virtual bool IsRunning() const = 0;

    // Request a clean exit (graceful WM_CLOSE path when the title has a
    // window; reserved for the runtime contract's OnDeactivated flow).
    virtual void RequestExit() = 0;

    // Wait up to timeout_ms for exit. Returns the exit report.
    virtual TitleExitReport WaitExit(uint32_t timeout_ms) = 0;

    // Force-kill the entire job tree immediately.
    virtual void TerminateTree() = 0;

    // §19 QR1 boundary — native title-authored checkpointing. Sends the
    // lifecycle signal to the title (message to its windows + broadcast
    // event to the whole job tree so helper processes see it too), waits a
    // bounded time for acknowledgment, then releases the tree. The session
    // state machine remains the sole owner of TITLE_SUSPENDED semantics.
    virtual TitleNotifyResult SuspendGame(uint32_t timeout_ms) = 0;
    virtual TitleNotifyResult ResumeGame(uint32_t timeout_ms) = 0;

    // Human-readable exit classification for the journal.
    static const char* ExitKindName(TitleExitKind k) {
        switch (k) {
            case TitleExitKind::NotStarted: return "not_started";
            case TitleExitKind::Clean: return "clean";
            case TitleExitKind::Crashed: return "crashed";
            case TitleExitKind::Terminated: return "terminated";
            case TitleExitKind::StillRunning: return "still_running";
        }
        return "unknown";
    }
};

ITitleSupervisor* CreateTitleSupervisor();  // Windows implementation
void DestroyTitleSupervisor(ITitleSupervisor* s);

const char* ExitKindName(TitleExitKind k);
const char* TitleNotifyResultName(TitleNotifyResult r);

} // namespace dc
