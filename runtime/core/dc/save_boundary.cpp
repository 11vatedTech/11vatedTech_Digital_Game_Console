// dc/save_boundary.cpp — save ownership & compatibility (DK0-M3 §18).
#include "dc/save_boundary.hpp"

#include "dc/package_store.hpp"

#include <cstdio>

namespace dc::save {

CompatibilityReport CheckCompatibility(const std::string& stored_schema,
                                       const std::string& package_schema,
                                       const std::vector<std::string>& migratable_from) {
    CompatibilityReport r;
    if (stored_schema == package_schema) {
        r.state = CompatibilityReport::State::Compatible;
        r.detail = "save_schema " + stored_schema + " matches package";
        return r;
    }
    for (const auto& m : migratable_from) {
        if (m == stored_schema) {
            r.state = CompatibilityReport::State::Migratable;
            r.detail = "stored " + stored_schema + " is migratable to " + package_schema;
            return r;
        }
    }
    r.state = CompatibilityReport::State::Incompatible;
    r.detail = "stored " + stored_schema + " cannot load into " + package_schema +
               " (no migration declared)";
    return r;
}

std::string SaveDir(const std::string& saves_root, uint64_t user_id,
                    const std::string& game_id, const std::string& save_schema) {
    char user[32];
    std::snprintf(user, sizeof(user), "%llu", static_cast<unsigned long long>(user_id));
    return package::JoinPath(package::JoinPath(package::JoinPath(saves_root, user), game_id),
                             save_schema);
}

bool WriteSave(const std::string& saves_root, uint64_t user_id,
               const std::string& game_id, const std::string& save_schema,
               const std::string& save_name, const std::string& bytes, std::string* err) {
    if (save_name.empty() || save_name.find('/') != std::string::npos ||
        save_name.find('\\') != std::string::npos || save_name.find("..") != std::string::npos) {
        if (err) *err = "invalid save name";
        return false;
    }
    const std::string dir = SaveDir(saves_root, user_id, game_id, save_schema);
    if (!package::MakeDirs(dir)) {
        if (err) *err = "cannot create save directory";
        return false;
    }
    // Atomic: temp + rename so a crash never leaves a half-written save.
    if (!package::AtomicWriteText(package::JoinPath(dir, save_name), bytes)) {
        if (err) *err = "save write failed";
        return false;
    }
    return true;
}

bool ReadSave(const std::string& saves_root, uint64_t user_id,
              const std::string& game_id, const std::string& save_schema,
              const std::string& save_name, std::string* out_bytes) {
    if (save_name.empty() || save_name.find('/') != std::string::npos ||
        save_name.find('\\') != std::string::npos || save_name.find("..") != std::string::npos)
        return false;
    return package::ReadFileBytes(
        package::JoinPath(SaveDir(saves_root, user_id, game_id, save_schema), save_name),
        out_bytes);
}

bool SaveExists(const std::string& saves_root, uint64_t user_id,
                const std::string& game_id, const std::string& save_schema,
                const std::string& save_name) {
    return package::PathExists(
        package::JoinPath(SaveDir(saves_root, user_id, game_id, save_schema), save_name));
}

} // namespace dc::save
