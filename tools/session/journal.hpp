// session_support.hpp — shared session-process support (DK0-M2 §26).
// Durable append-only journal (JSONL, one transition per line, flushed on
// every append so a crash cannot destroy prior history) and UTC stamping.
#pragma once

#include "../../runtime/core/dc/session.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace dcsess {

namespace fs = std::filesystem;

inline std::string NowStamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

class SessionJournal {
public:
    explicit SessionJournal(const fs::path& path) : path_(path) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        file_.open(path, std::ios::app | std::ios::binary);
    }
    bool ok() const { return static_cast<bool>(file_); }

    void Append(const dc::SessionTransition& t, const std::string& session_id) {
        if (!file_) return;
        file_ << "{"
              << "\"session\":\"" << session_id << "\","
              << "\"from\":\"" << dc::SessionStateName(t.from) << "\","
              << "\"to\":\"" << dc::SessionStateName(t.to) << "\","
              << "\"reason\":\"" << dc::TransitionReasonName(t.reason) << "\","
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

} // namespace dcsess
