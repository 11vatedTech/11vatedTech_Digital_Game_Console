// dc-session — the Digital Console session runtime (DK0-M2 §24–§26, §34).
// Owns the gaming session: session state machine, durable journal, title
// supervision through Job Objects, and Guide routing. The shell process is
// next; this executable proves the runtime contracts end-to-end.
//
// Development entry points (controller-first E2E is driven through these):
//   dc-session --shell               interactive controller-first shell
//   dc-session --shell --selftest N  CI selftest: boot, render N frames, exit
//   dc-session --run [title_id]      start session, optionally launch title
//   dc-session --journal             print the session journal
//   dc-session --list-titles         print registered titles
#include "../../runtime/core/dc/session.hpp"
#include "../../runtime/core/dc/title_registry.hpp"
#include "../../runtime/host/dc/title_supervisor.hpp"

// Interactive shell (tools/session/shell.cpp).
int RunShell(int selftest_frames);

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace dc;

namespace {

// ---------------------------------------------------------------------------
// Durable session journal (§26): append-only JSONL, one transition per line.
// A crash cannot destroy prior history: each entry is flushed on append.
// ---------------------------------------------------------------------------
class SessionJournal {
public:
    explicit SessionJournal(const fs::path& path) : path_(path) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        file_.open(path, std::ios::app | std::ios::binary);
    }
    bool ok() const { return static_cast<bool>(file_); }

    void Append(const SessionTransition& t, const std::string& session_id) {
        if (!file_) return;
        file_ << "{"
              << "\"session\":\"" << session_id << "\","
              << "\"from\":\"" << SessionStateName(t.from) << "\","
              << "\"to\":\"" << SessionStateName(t.to) << "\","
              << "\"reason\":\"" << TransitionReasonName(t.reason) << "\","
              << "\"monotonic_ns\":" << t.monotonic_ns << ","
              << "\"correlation\":" << t.correlation_id << ","
              << "\"timed_out\":" << (t.timed_out ? "true" : "false")
              << "}\n";
        file_.flush();
    }

private:
    fs::path path_;
    std::ofstream file_;
};

std::string NowStamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

void PrintUsage() {
    std::printf(
        "dc-session — Digital Console session runtime (DK0-M2)\n\n"
        "Usage:\n"
        "  dc-session --run [TITLE_ID]   start a session; optionally launch a title\n"
        "  dc-session --list-titles      list registered titles\n"
        "  dc-session --journal          print the latest session journal\n\n"
        "Environment:\n"
        "  DC_REGISTRY_DIR   title registry directory (default: registry)\n"
        "  DC_JOURNAL_DIR    journal directory (default: evidence/sessions)\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string mode;
    std::string title_id;
    int selftest_frames = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shell") == 0) {
            mode = "shell";
        } else if (std::strcmp(argv[i], "--selftest") == 0) {
            if (i + 1 < argc) selftest_frames = std::atoi(argv[++i]);
            if (selftest_frames <= 0) selftest_frames = 90;
            if (mode.empty()) mode = "shell";
        } else if (std::strcmp(argv[i], "--run") == 0) {
            mode = "run";
            if (i + 1 < argc && argv[i + 1][0] != '-') title_id = argv[++i];
        } else if (std::strcmp(argv[i], "--list-titles") == 0) {
            mode = "list";
        } else if (std::strcmp(argv[i], "--journal") == 0) {
            mode = "journal";
        } else if (std::strcmp(argv[i], "--help") == 0) {
            PrintUsage();
            return 0;
        }
    }
    if (mode.empty()) { PrintUsage(); return 2; }

    const char* reg_env = std::getenv("DC_REGISTRY_DIR");
    fs::path registry_dir = reg_env ? reg_env : "registry";
    const char* jour_env = std::getenv("DC_JOURNAL_DIR");
    fs::path journal_dir = jour_env ? jour_env : "evidence/sessions";

    if (mode == "shell") return RunShell(selftest_frames);

    TitleRegistry registry;
    if (mode == "list") {
        std::vector<std::string> errors;
        if (!registry.LoadFromDirectory(registry_dir.string(), &errors)) {
            for (const auto& e : errors) std::fprintf(stderr, "error: %s\n", e.c_str());
            return 1;
        }
        for (const auto& t : registry.Entries()) {
            std::printf("%-40s %-24s v%s controller=%s offline=%s\n",
                        t.title_id.c_str(), t.name.c_str(), t.version.c_str(),
                        t.controller_required ? "yes" : "no",
                        t.offline_launch ? "yes" : "no");
        }
        return 0;
    }

    if (mode == "journal") {
        // Print the most recent journal file.
        std::error_code ec;
        if (!fs::exists(journal_dir, ec)) { std::printf("(no sessions)\n"); return 0; }
        fs::path newest;
        for (const auto& e : fs::directory_iterator(journal_dir, ec)) {
            if (e.path().extension() == ".jsonl" && e.path() > newest) newest = e.path();
        }
        if (newest.empty()) { std::printf("(no sessions)\n"); return 0; }
        std::ifstream f(newest);
        std::string line;
        while (std::getline(f, line)) std::printf("%s\n", line.c_str());
        return 0;
    }

    // ---- mode == run: the actual console session ----
    std::string session_id = "sess-" + NowStamp();
    SessionJournal journal(journal_dir / (session_id + ".jsonl"));
    if (!journal.ok()) {
        std::fprintf(stderr, "fatal: cannot open journal\n");
        return 1;
    }

    SessionStateMachine sm;
    uint64_t t = 0; // monotonic; QPC-based in the shell process, simple here
    auto transition = [&](SessionState to, TransitionReason reason) {
        TransitionResult r = sm.Transition(to, reason, t, 1);
        if (r == TransitionResult::Ok) {
            journal.Append(sm.Journal().back(), session_id);
            std::printf("[session] %s\n", SessionStateName(sm.state()));
            return true;
        }
        std::printf("[session] REJECTED %s -> %s\n",
                    SessionStateName(sm.state()), SessionStateName(to));
        return false;
    };

    // Bring-up (§25).
    transition(SessionState::SessionStarting, TransitionReason::BootSequence);
    transition(SessionState::HostValidating, TransitionReason::BootSequence);
    transition(SessionState::SessionCapabilitiesResolving, TransitionReason::BootSequence);
    transition(SessionState::InputAcquiring, TransitionReason::BootSequence);
    transition(SessionState::ShellStarting, TransitionReason::BootSequence);
    transition(SessionState::ShellActive, TransitionReason::BootSequence);

    if (!title_id.empty()) {
        // Title launch path (§27–§28): registry lookup → supervisor launch.
        std::vector<std::string> errors;
        registry.LoadFromDirectory(registry_dir.string(), &errors);
        const TitleEntry* entry = registry.Find(title_id);
        if (!entry) {
            std::fprintf(stderr, "error: title '%s' not registered\n", title_id.c_str());
            transition(SessionState::Recovery, TransitionReason::FatalError);
            transition(SessionState::ShellActive, TransitionReason::TitleCrash);
            return 1;
        }

        transition(SessionState::TitleRequested, TransitionReason::Requested);
        transition(SessionState::TitleValidating, TransitionReason::Requested);
        transition(SessionState::TitleStarting, TransitionReason::Requested);

        ITitleSupervisor* supervisor = CreateTitleSupervisor();
        fs::path exe = fs::absolute(entry->entrypoint);
        auto launch = supervisor->Launch(exe.string(),
                                         exe.parent_path().string(), "");
        if (!launch.ok) {
            std::fprintf(stderr, "error: launch failed: %s\n", launch.error.c_str());
            transition(SessionState::ShellActive, TransitionReason::TitleCrash);
            DestroyTitleSupervisor(supervisor);
            return 1;
        }
        std::printf("[title] launched pid=%u (%s)\n", launch.process_id, title_id.c_str());
        transition(SessionState::TitleActive, TransitionReason::Requested);

        // Supervise until exit. The Guide surface is not yet interactive
        // (shell process is next); the supervisor + state machine contracts
        // are what DK0-M2 proves here.
        TitleExitReport report;
        while (true) {
            report = supervisor->WaitExit(500);
            if (report.kind != TitleExitKind::StillRunning) break;
            t += 500'000'000ull;
            // Timeout machinery exercised on the state machine level.
            sm.CheckTimeout(t);
        }
        TransitionReason reason = (report.kind == TitleExitKind::Clean)
                                      ? TransitionReason::TitleExitClean
                                      : TransitionReason::TitleCrash;
        std::printf("[title] exit kind=%s code=%u\n",
                    ITitleSupervisor::ExitKindName(report.kind), report.exit_code);

        if (report.kind == TitleExitKind::Clean) {
            transition(SessionState::TitleStopping, TransitionReason::TitleExitClean);
        } else {
            transition(SessionState::TitleCrashed, TransitionReason::TitleCrash);
        }
        transition(SessionState::ShellRecovering, reason);
        transition(SessionState::ShellActive, reason);
        DestroyTitleSupervisor(supervisor);
    }

    // Session teardown (development: exit to desktop is the C3 escape hatch).
    transition(SessionState::SessionExit, TransitionReason::UserExit);
    std::printf("[session] ended (%s)\n", session_id.c_str());
    return 0;
}
