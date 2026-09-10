// title_registry.cpp — JSON-backed title registry (DK0-M2 §28).
#include "dc/title_registry.hpp"
#include "dc/json.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace dc {

namespace {

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

// Minimal JSON string escaping for registry writes.
std::string JsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out;
}

std::vector<std::string> StringArray(const json::Value* v) {
    std::vector<std::string> out;
    if (!v || !v->is_array()) return out;
    for (const auto& e : v->as_array()) {
        if (e.is_string()) out.push_back(e.as_string());
    }
    return out;
}

} // namespace

bool TitleRegistry::LoadFromDirectory(const std::string& dir, std::vector<std::string>* out_errors) {
    entries_.clear();
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        if (out_errors) out_errors->push_back("registry directory not found: " + dir);
        return false;
    }
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".json") continue;
        if (e.path().filename().string().find(".title.") == std::string::npos) continue;
        std::string error;
        auto parsed = json::ParseFile(e.path().string(), error);
        if (!parsed) {
            if (out_errors) out_errors->push_back("malformed title file: " + e.path().string() + " (" + error + ")");
            continue;
        }
        if (!parsed->is_object()) continue;
        const json::Value& root = *parsed;

        TitleEntry entry;
        if (const auto* v = root.find("title_id"); v && v->is_string()) entry.title_id = v->as_string();
        if (const auto* v = root.find("name"); v && v->is_string()) entry.name = v->as_string();
        if (const auto* v = root.find("version"); v && v->is_string()) entry.version = v->as_string();
        if (const auto* v = root.find("entrypoint"); v && v->is_string()) entry.entrypoint = v->as_string();
        entry.required_host_profiles = StringArray(root.find("required_host_profiles"));
        entry.required_session_profiles = StringArray(root.find("required_session_profiles"));
        if (const auto* v = root.find("controller_required")) entry.controller_required = v->as_bool(true);
        if (const auto* v = root.find("offline_launch")) entry.offline_launch = v->as_bool(true);

        if (entry.title_id.empty() || entry.entrypoint.empty()) {
            if (out_errors) out_errors->push_back("title file missing title_id/entrypoint: " + e.path().string());
            continue;
        }
        entries_.push_back(std::move(entry));
    }
    return true;
}

bool TitleRegistry::Register(const TitleEntry& entry, const std::string& dir) {
    if (entry.title_id.empty() || entry.entrypoint.empty()) return false;
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::ostringstream o;
    o << "{\n";
    o << "  \"title_id\": \"" << JsonEscape(entry.title_id) << "\",\n";
    o << "  \"name\": \"" << JsonEscape(entry.name) << "\",\n";
    o << "  \"version\": \"" << JsonEscape(entry.version) << "\",\n";
    o << "  \"entrypoint\": \"" << JsonEscape(entry.entrypoint) << "\",\n";
    o << "  \"required_host_profiles\": [";
    for (size_t i = 0; i < entry.required_host_profiles.size(); ++i) {
        o << (i ? ", " : "") << "\"" << JsonEscape(entry.required_host_profiles[i]) << "\"";
    }
    o << "],\n";
    o << "  \"required_session_profiles\": [";
    for (size_t i = 0; i < entry.required_session_profiles.size(); ++i) {
        o << (i ? ", " : "") << "\"" << JsonEscape(entry.required_session_profiles[i]) << "\"";
    }
    o << "],\n";
    o << "  \"controller_required\": " << (entry.controller_required ? "true" : "false") << ",\n";
    o << "  \"offline_launch\": " << (entry.offline_launch ? "true" : "false") << "\n";
    o << "}\n";

    // Sanitize the file name from the title id (keep alnum, dot, dash).
    std::string safe;
    for (char c : entry.title_id) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-') safe += c;
    }
    if (safe.empty()) return false;
    WriteText(fs::path(dir) / (safe + ".title.json"), o.str());

    // Update in-memory list (replace if present).
    for (auto& e : entries_) {
        if (e.title_id == entry.title_id) {
            e = entry;
            return true;
        }
    }
    entries_.push_back(entry);
    return true;
}

const TitleEntry* TitleRegistry::Find(const std::string& title_id) const {
    for (const auto& e : entries_) {
        if (e.title_id == title_id) return &e;
    }
    return nullptr;
}

} // namespace dc
