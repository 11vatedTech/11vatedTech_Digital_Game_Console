// stagecert.cpp — dc-stage-certification (directive §20–§21).
// Stages a qualification run OUTSIDE the OneDrive-synced repository so storage
// and thermal measurements are not contaminated by sync activity:
//   1. copies required binaries + profile registry to a local non-synced dir
//   2. records source provenance (path, revision, dirty state)
//   3. records environment context (power, load, time-since-last-run)
//   4. executes dc-hostprof --qualify <mode> there
//   5. copies the immutable run directory back into evidence/qualification/runs
// Thermal guardrails: refuses to start a certification run sooner than
// --min-gap-minutes after the previous run unless --force is given.
//
// This tool never modifies or deletes the user's repository.
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string NowStamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void WriteText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
}

// Escape a string into a JSON string literal (minimal, ASCII-safe).
std::string JsonStr(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

// Parse a string field out of flat JSON (evidence files are machine-written,
// so a targeted scan is acceptable for the two fields we need here).
std::string ScanStringField(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t k = json.find(pat);
    if (k == std::string::npos) return "";
    k = json.find(':', k + pat.size());
    if (k == std::string::npos) return "";
    ++k;
    while (k < json.size() && (json[k] == ' ' || json[k] == '\t')) ++k;
    if (k >= json.size() || json[k] != '"') return "";
    size_t end = json.find('"', k + 1);
    if (end == std::string::npos) return "";
    return json.substr(k + 1, end - k - 1);
}

// Power source: AC or battery (GetSystemPowerStatus).
std::string PowerSource() {
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps)) return "unknown";
    if (ps.ACLineStatus == 1) return "ac";
    if (ps.ACLineStatus == 0) return "battery";
    return "unknown";
}

// Performance power scheme via powercfg (best-effort, non-fatal).
std::string PowerScheme() {
    FILE* f = _popen("powercfg /getactivescheme", "r");
    if (!f) return "unknown";
    char buf[512] = {};
    std::string out;
    while (fgets(buf, sizeof(buf), f)) out += buf;
    _pclose(f);
    // trim
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return out.empty() ? "unknown" : out;
}

// CPU/GPU load proxy: GetSystemTimes delta over a short window.
double CpuLoadPercent() {
    FILETIME idle1, kern1, user1, idle2, kern2, user2;
    GetSystemTimes(&idle1, &kern1, &user1);
    Sleep(300);
    GetSystemTimes(&idle2, &kern2, &user2);
    auto to64 = [](const FILETIME& ft) {
        return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    unsigned long long idle_d = to64(idle2) - to64(idle1);
    unsigned long long kern_d = to64(kern2) - to64(kern1); // includes idle
    unsigned long long user_d = to64(user2) - to64(user1);
    unsigned long long total = kern_d + user_d;
    if (total == 0) return 0.0;
    double busy = static_cast<double>(total - idle_d) / static_cast<double>(total);
    return busy * 100.0;
}

// Find the most recent run directory in evidence/qualification/runs.
std::string NewestRunStamp(const fs::path& runs_dir) {
    std::string newest;
    std::error_code ec;
    if (!fs::exists(runs_dir, ec)) return newest;
    for (const auto& e : fs::directory_iterator(runs_dir, ec)) {
        std::string name = e.path().filename().string();
        // Run IDs begin with a UTC timestamp: 2026-09-07T...
        if (name.size() > 19 && name.compare(newest) > 0) newest = name;
    }
    return newest;
}

// Parse "YYYY-MM-DDTHH:MM:SSZ" into a time_t.
std::time_t ParseStamp(const std::string& s) {
    if (s.size() < 19) return 0;
    std::tm tm{};
    int y, mo, d, h, mi, sec;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return 0;
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
    tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = sec;
    return _mkgmtime(&tm);
}

void PrintUsage() {
    std::printf(
        "dc-stage-certification — quiet-system qualification staging\n\n"
        "Usage:\n"
        "  dc-stage-certification --mode quick|standard|certification [options]\n\n"
        "Options:\n"
        "  --mode MODE        Qualification mode to run (required)\n"
        "  --min-gap-min N    Minimum minutes since previous run before starting\n"
        "                     a NEW qualification (default: 30 for standard/cert,\n"
        "                     5 for quick; guardrail against heat-soak, §21)\n"
        "  --force            Bypass the minimum-gap guardrail (recorded in manifest)\n"
        "  --hostprof PATH    Path to dc-hostprof.exe (default: build/Release)\n"
        "  --help             Show this help\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string mode;
    int min_gap_arg = -1;
    bool force = false;
    fs::path hostprof = "build/Release/dc-hostprof.exe";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else if (std::strcmp(argv[i], "--min-gap-min") == 0 && i + 1 < argc) {
            min_gap_arg = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--force") == 0) {
            force = true;
        } else if (std::strcmp(argv[i], "--hostprof") == 0 && i + 1 < argc) {
            hostprof = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            PrintUsage();
            return 0;
        }
    }
    if (mode.empty() || (mode != "quick" && mode != "standard" && mode != "certification")) {
        PrintUsage();
        return 2;
    }
    int default_gap = (mode == "quick") ? 5 : 30;
    int min_gap = (min_gap_arg >= 0) ? min_gap_arg : default_gap;

    // ---- 1. Environment context BEFORE any decision ----
    std::string power = PowerSource();
    std::string scheme = PowerScheme();
    double cpu_load = CpuLoadPercent();

    // Time since previous run (from the append-only run index on disk).
    fs::path runs_dir = "evidence/qualification/runs";
    std::string newest = NewestRunStamp(runs_dir);
    long long gap_min = -1;
    if (!newest.empty()) {
        std::time_t prev = ParseStamp(newest);
        if (prev > 0) {
            gap_min = static_cast<long long>(std::difftime(std::time(nullptr), prev) / 60.0);
        }
    }

    // ---- 2. Thermal guardrail (§21): refuse to create our own heat-soak ----
    if (gap_min >= 0 && gap_min < min_gap && !force) {
        std::printf("ENVIRONMENT_NOT_SUITABLE: previous qualification ran %lld min ago "
                    "(minimum gap %d min for %s mode). Use --force to override.\n",
                    gap_min, min_gap, mode.c_str());
        return 3;
    }

    // ---- 3. Stage to a local non-synced directory ----
    char tmp_root[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp_root);
    std::string stamp = NowStamp();
    std::string safe_stamp = stamp;
    for (char& c : safe_stamp) if (c == ':') c = '_';
    fs::path stage = fs::path(tmp_root) / ("dc-qual-" + safe_stamp);
    std::error_code ec;
    fs::create_directories(stage / "formats" / "profiles", ec);

    fs::path hp = fs::absolute(hostprof);
    if (!fs::exists(hp, ec)) {
        std::printf("fatal: dc-hostprof not found at %s\n", hp.string().c_str());
        return 2;
    }
    fs::copy(hp, stage / hp.filename(), fs::copy_options::overwrite_existing, ec);
    // dc-displayprobe: hostprof --qualify launches it as a child (VRR active
    // proof); it must be present in the staging dir for the proof to run.
    fs::path dp = hp.parent_path() / "dc-displayprobe.exe";
    if (fs::exists(dp, ec)) {
        fs::copy(dp, stage / dp.filename(), fs::copy_options::overwrite_existing, ec);
    } else {
        std::printf("warning: dc-displayprobe.exe missing; staged run will lack VRR active proof\n");
    }
    // Runtime DLL dependencies
    for (const char* dll : {"SDL3.dll"}) {
        fs::path dep = hp.parent_path() / dll;
        if (fs::exists(dep, ec)) fs::copy(dep, stage / dll, fs::copy_options::overwrite_existing, ec);
    }
    if (fs::exists("formats/profiles", ec)) {
        fs::copy("formats/profiles", stage / "formats" / "profiles",
                 fs::copy_options::recursive, ec);
    }

    // ---- 4. Execute qualification in the staging directory ----
    std::string cmd = (stage / hp.filename()).string() + " --qualify " + mode +
                      " --profiles \"" + (stage / "formats" / "profiles").string() + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');
    BOOL ok = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE,
                             0, nullptr, stage.string().c_str(), &si, &pi);
    if (!ok) {
        std::printf("fatal: failed to launch dc-hostprof (error %lu)\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // ---- 5. Copy the immutable run directory back ----
    fs::path src_runs = stage / "evidence" / "qualification" / "runs";
    std::string run_id;
    if (fs::exists(src_runs, ec)) {
        for (const auto& e : fs::directory_iterator(src_runs, ec)) {
            run_id = e.path().filename().string();
            fs::path dst = runs_dir / run_id;
            if (fs::exists(dst, ec)) {
                std::printf("fatal: run id collision %s (never overwrite, §6)\n", run_id.c_str());
                return 1;
            }
            fs::copy(e.path(), dst, fs::copy_options::recursive, ec);
        }
    }

    // ---- 6. Staging manifest with provenance (§20) ----
    std::string src_hash = "unknown";
    {
        // Best-effort git revision without mutating anything.
        FILE* g = _popen("git rev-parse --short HEAD 2>nul", "r");
        if (g) {
            char buf[64] = {};
            if (fgets(buf, sizeof(buf), g)) {
                src_hash = buf;
                while (!src_hash.empty() && (src_hash.back() == '\n' || src_hash.back() == '\r')) src_hash.pop_back();
            }
            _pclose(g);
        }
    }
    bool dirty = false;
    {
        FILE* g = _popen("git status --porcelain 2>nul", "r");
        if (g) {
            char buf[256];
            dirty = fgets(buf, sizeof(buf), g) != nullptr;
            _pclose(g);
        }
    }

    if (!run_id.empty()) {
        fs::path manifest = runs_dir / run_id / "staging.json";
        std::ostringstream m;
        m << "{\n";
        m << "  \"staging_tool\": \"dc-stage-certification/1\",\n";
        m << "  \"executed_utc\": " << JsonStr(stamp) << ",\n";
        m << "  \"source_repo_path\": " << JsonStr(fs::absolute(".").string()) << ",\n";
        m << "  \"source_revision\": " << JsonStr(src_hash) << ",\n";
        m << "  \"working_tree_dirty\": " << (dirty ? "true" : "false") << ",\n";
        m << "  \"execution_path\": " << JsonStr(stage.string()) << ",\n";
        m << "  \"onedrive_synced_execution\": false,\n";
        m << "  \"qualification_mode\": " << JsonStr(mode) << ",\n";
        m << "  \"exit_code\": " << exit_code << ",\n";
        m << "  \"environment\": {\n";
        m << "    \"power_source\": " << JsonStr(power) << ",\n";
        m << "    \"power_scheme\": " << JsonStr(scheme) << ",\n";
        m << "    \"cpu_load_percent_at_start\": " << cpu_load << ",\n";
        m << "    \"minutes_since_previous_run\": " << gap_min << ",\n";
        m << "    \"min_gap_enforced_min\": " << min_gap << ",\n";
        m << "    \"guardrail_forced\": " << (force ? "true" : "false") << "\n";
        m << "  }\n";
        m << "}\n";
        WriteText(manifest, m.str());
    }

    // Cleanup: remove the staging tree (evidence already copied back).
    fs::remove_all(stage, ec);

    std::printf("staged qualification %s -> run %s (exit %lu)\n",
                mode.c_str(), run_id.c_str(), exit_code);
    return (exit_code == 0 && !run_id.empty()) ? 0 : 1;
}
