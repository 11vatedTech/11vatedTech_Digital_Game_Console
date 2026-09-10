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

} // namespace dc
