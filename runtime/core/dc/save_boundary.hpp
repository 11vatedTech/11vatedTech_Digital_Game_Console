// dc/save_boundary.hpp — save ownership & compatibility (DK0-M3 §18).
//
// Ownership: saves belong to (user_id, game_id, save_schema) — never to a
// package version. A patch (0.1.0 -> 0.1.1) keeps the same save_schema, so
// saves remain recognized. A breaking save change bumps save_schema and
// declares migratable_from; migration is explicit, never silent destruction.
//
// Layout: <saves-root>/<user_id>/<game_id>/<save_schema>/
// The path contains NO version component, so patches cannot orphan saves.
#pragma once

#include "dc/package.hpp"

#include <string>
#include <vector>

namespace dc::save {

struct CompatibilityReport {
    enum class State {
        Compatible,        // same save_schema — load directly
        Migratable,        // stored schema is declared migratable_from
        Incompatible,      // breaking change without migration path
        NoSaves,           // nothing stored yet — fresh start, not a failure
    };
    State state = State::NoSaves;
    std::string detail;
};

CompatibilityReport CheckCompatibility(const std::string& stored_schema,
                                       const std::string& package_schema,
                                       const std::vector<std::string>& migratable_from);

// Save directory for (user, game, schema). No version component by design.
std::string SaveDir(const std::string& saves_root, uint64_t user_id,
                    const std::string& game_id, const std::string& save_schema);

// Write a save blob atomically (temp + rename via package store primitive).
bool WriteSave(const std::string& saves_root, uint64_t user_id,
               const std::string& game_id, const std::string& save_schema,
               const std::string& save_name, const std::string& bytes,
               std::string* err);

bool ReadSave(const std::string& saves_root, uint64_t user_id,
              const std::string& game_id, const std::string& save_schema,
              const std::string& save_name, std::string* out_bytes);

bool SaveExists(const std::string& saves_root, uint64_t user_id,
                const std::string& game_id, const std::string& save_schema,
                const std::string& save_name);

} // namespace dc::save
