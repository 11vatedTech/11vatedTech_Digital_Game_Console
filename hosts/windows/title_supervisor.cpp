// title_supervisor_windows.cpp — Windows title supervision (DK0-M2 §27).
// Job Object ownership: the title and every descendant are bound to a job
// with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE so session teardown cannot leave
// orphans. Crash detection distinguishes clean exit from abnormal
// termination via GetExitCodeProcess (NTSTATUS-style codes) and the process
// exit reason where available.
#include "dc/title_supervisor.hpp"
#include "dc/title_context_json.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace dcwin {

namespace {

// Job name includes the PID to avoid collisions across sessions.
std::wstring JobNameForProcess(DWORD pid) {
    wchar_t buf[64];
    std::swprintf(buf, 64, L"dc-title-job-%lu", static_cast<unsigned long>(pid));
    return buf;
}

// Environment variable carrying the versioned launch-context envelope
// (dc.title-context/1). The title parses it with the core strict parser;
// a foreign schema id is rejected (title_context_json.hpp).
constexpr const char* kContextEnvVar = "DC_TITLE_CONTEXT";

class WindowsTitleSupervisor final : public dc::ITitleSupervisor {
public:
    ~WindowsTitleSupervisor() override {
        // RAII safety net: if the owner forgot to wait/terminate, kill the
        // tree so nothing outlives the supervisor.
        if (job_ && running_) TerminateTree();
        if (job_) CloseHandle(job_);
        if (proc_handle_) CloseHandle(proc_handle_);
    }

    dc::TitleLaunchResult Launch(const std::string& executable,
                                 const std::string& working_dir,
                                 const std::string& command_line) override {
        return Launch(executable, working_dir, command_line,
                      dc::TitleLaunchContext{});
    }

    dc::TitleLaunchResult Launch(const std::string& executable,
                                 const std::string& working_dir,
                                 const std::string& command_line,
                                 const dc::TitleLaunchContext& context) override {
        dc::TitleLaunchResult res;
        if (running_) {
            res.error = "supervisor already owns a running title";
            return res;
        }

        // Create the job first so the initial process is born inside it.
        if (!job_) {
            job_ = CreateJobObjectW(nullptr, nullptr);
            if (!job_) {
                res.error = "CreateJobObject failed: " + std::to_string(GetLastError());
                return res;
            }
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
            JOB_OBJECT_LIMIT_BREAKAWAY_OK; // allow explicit breakaway if a title needs the debugger
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits))) {
            res.error = "SetInformationJobObject failed: " + std::to_string(GetLastError());
            return res;
        }

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::string cmdline = "\"" + executable + "\"";
        if (!command_line.empty()) cmdline += " " + command_line;
        std::vector<char> buf(cmdline.begin(), cmdline.end());
        buf.push_back('\0');

        // Environment block: inherit the parent plus the typed launch-context
        // envelope (canon §16 embryo; §30). The envelope is versioned and
        // schema-checked by the title — never a loose bag of variables.
        std::vector<char> envbuf;
        const bool have_context = !context.schema.empty();
        if (have_context) {
            std::string envelope = dc::SerializeTitleContext(context);
            std::string entry = std::string(kContextEnvVar) + "=" + envelope;
            LPCH parent_env = ::GetEnvironmentStringsA();
            if (parent_env) {
                for (const char* p = parent_env; *p;) {
                    std::string var(p);
                    // Never forward a stale envelope from our own environment.
                    if (var.rfind(std::string(kContextEnvVar) + "=", 0) != 0)
                        envbuf.insert(envbuf.end(), var.begin(), var.end());
                    envbuf.push_back('\0');
                    p += var.size() + 1;
                }
                ::FreeEnvironmentStringsA(parent_env);
            }
            envbuf.insert(envbuf.end(), entry.begin(), entry.end());
            envbuf.push_back('\0');
            envbuf.push_back('\0');
        }

        BOOL ok = CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                                 CREATE_SUSPENDED,
                                 have_context ? envbuf.data() : nullptr,
                                 working_dir.empty() ? nullptr : working_dir.c_str(),
                                 &si, &pi);
        if (!ok) {
            res.error = "CreateProcess failed: " + std::to_string(GetLastError());
            return res;
        }

        // Assign to the job BEFORE resuming so no child escapes the tree.
        if (!AssignProcessToJobObject(job_, pi.hProcess)) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            res.error = "AssignProcessToJobObject failed: " + std::to_string(GetLastError());
            return res;
        }
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);

        if (proc_handle_) CloseHandle(proc_handle_);
        proc_handle_ = pi.hProcess;
        proc_pid_ = pi.dwProcessId;
        running_ = true;

        res.ok = true;
        res.process_id = pi.dwProcessId;
        return res;
    }

    bool IsRunning() const override {
        if (!running_ || !proc_handle_) return false;
        DWORD code = 0;
        if (!GetExitCodeProcess(proc_handle_, &code)) return false;
        return code == STILL_ACTIVE;
    }

    void RequestExit() override {
        // Graceful path: post WM_CLOSE to the title's windows. The canonical
        // runtime contract (OnDeactivated / exit request) will replace this;
        // for the DK0-M2 sample title, WM_CLOSE is the honest clean-exit path.
        if (!running_) return;
        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            auto* self = reinterpret_cast<WindowsTitleSupervisor*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == self->proc_pid_) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return TRUE;
        }, reinterpret_cast<LPARAM>(this));
    }

    dc::TitleExitReport WaitExit(uint32_t timeout_ms) override {
        dc::TitleExitReport report;
        if (!proc_handle_) {
            report.kind = dc::TitleExitKind::NotStarted;
            return report;
        }
        if (!running_) {
            // Already reaped (e.g. by TerminateTree): report the recorded code.
            DWORD code = 0;
            GetExitCodeProcess(proc_handle_, &code);
            report.exit_code = code;
            report.kind = (code & 0x80000000u) ? dc::TitleExitKind::Crashed
                                               : dc::TitleExitKind::Terminated;
            return report;
        }
        DWORD wait = WaitForSingleObject(proc_handle_, timeout_ms);
        if (wait == WAIT_TIMEOUT) {
            report.kind = dc::TitleExitKind::StillRunning;
            return report;
        }
        running_ = false;

        DWORD code = 0;
        GetExitCodeProcess(proc_handle_, &code);
        report.exit_code = code;

        // Classification (console semantics): ONLY exit code 0 is a clean
        // exit. High-bit codes are NTSTATUS exceptions (crash). Any other
        // code is abnormal-termination: an externally forced kill (e.g.
        // taskkill /F → exit code 1) or a title failure exit. Treating
        // nonzero as clean would mask real crashes (DevKit-0 2026-09-10:
        // taskkill of the sample was classified "clean").
        if (code == STILL_ACTIVE) {
            report.kind = dc::TitleExitKind::StillRunning;
        } else if (code & 0x80000000u) {
            report.kind = dc::TitleExitKind::Crashed;
        } else if (code == 0) {
            report.kind = dc::TitleExitKind::Clean;
        } else {
            report.kind = dc::TitleExitKind::Crashed;
        }
        return report;
    }

    void TerminateTree() override {
        if (!job_) return;
        // TerminateJobObject kills every process in the job atomically.
        TerminateJobObject(job_, 1);
        if (proc_handle_ && running_) {
            WaitForSingleObject(proc_handle_, 2000);
            running_ = false;
        }
    }

private:
    HANDLE job_ = nullptr;
    HANDLE proc_handle_ = nullptr;
    DWORD proc_pid_ = 0;
    bool running_ = false;
};

} // namespace

} // namespace dcwin

namespace dc {

ITitleSupervisor* CreateTitleSupervisor() {
    return new (std::nothrow) dcwin::WindowsTitleSupervisor();
}

void DestroyTitleSupervisor(ITitleSupervisor* s) {
    delete s;
}

} // namespace dc
