// dc/title_registry.hpp — native title registry (DK0-M2 §28).
// Minimal registration compatible with the future .11g package model: the
// fields below are the subset .11g will carry; the registry file format is
// deliberately trivial JSON so migration to .11g manifests is mechanical.
#pragma once

#include <string>
#include <vector>

namespace dc {

struct TitleEntry {
    std::string title_id;        // reverse-DNS, e.g. "com.11vated.samples.d3d12-orbit"
    std::string name;            // player-facing display name
    std::string version = "0.0.1";
    std::string entrypoint;      // executable path (relative to install root)
    std::vector<std::string> required_host_profiles;     // e.g. DCP-2026-BASE
    std::vector<std::string> required_session_profiles;  // e.g. DCX-UHD60
    bool controller_required = true;
    bool offline_launch = true;  // C9: local-first by default
};

class TitleRegistry {
public:
    // Load all title entries from a directory of *.title.json files.
    // Returns false only on a directory-level failure; malformed individual
    // files are skipped and reported through out_errors.
    bool LoadFromDirectory(const std::string& dir, std::vector<std::string>* out_errors = nullptr);

    // Register/overwrite a title entry, writing <id>.title.json into dir.
    bool Register(const TitleEntry& entry, const std::string& dir);

    const std::vector<TitleEntry>& Entries() const { return entries_; }

    const TitleEntry* Find(const std::string& title_id) const;

private:
    std::vector<TitleEntry> entries_;
};

} // namespace dc
