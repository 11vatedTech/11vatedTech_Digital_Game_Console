// dc/fidelity_loader.hpp — load fidelity data from an INSTALLED package view
// (DK0-M4 §28/§30). The runtime never reads fidelity files from the source
// tree: the generation view is authoritative (M3 established the package as
// the unit of trust). Fail-closed on version mismatch (§29).
//
// Layout (matches packages/fidelitylab + tools/fidelity-compile):
//   <gen_view>/manifests/fidelity.json              title contract
//   <gen_view>/manifests/fidelity.candidates.json   compiled candidate set
//
// The load is the package→runtime boundary for fidelity: every artifact is
// parsed with the core strict parsers and every version is checked before
// any data is handed to the governor.
#pragma once

#include "dc/fidelity.hpp"
#include "dc/fidelity_compiler.hpp"

#include <string>
#include <vector>

namespace dc::fidelity {

// Read a file fully; *ok=false (no throw) when unreadable.
std::string ReadTextFile(const std::string& path, bool* ok);

struct LoadedFidelity {
    bool ok = false;
    std::string reason_code;   // FIDELITY_* structured reason on failure
    std::string detail;

    Contract contract;         // from the package (parsed)
    std::string contract_text; // verbatim bytes (hash/evidence linkage)
    std::string candidates_text;
    CandidateSet candidates;   // compiled artifact (parsed + verified)
};

// Load both artifacts from a generation view. Fails closed when:
//   - the contract file is missing/invalid            → FIDELITY_CONTRACT_MISSING / _SCHEMA_INVALID
//   - the candidate artifact is missing (allowed for
//     a dev-run without compile; caller may compile)  → FIDELITY_CANDIDATES_MISSING
//   - candidate artifact invalid                      → FIDELITY_CANDIDATES_INVALID
//   - versions disagree (contract↔artifact, compiler) → FIDELITY_VERSION_MISMATCH
LoadedFidelity LoadFromPackageView(const std::string& gen_view_dir);

// Convenience: the single best candidate for a target utility ordering is
// the governor's job; this only exposes counts for preflight logging.
struct PackageFidelitySummary {
    size_t domains = 0;
    size_t states = 0;
    size_t pareto_candidates = 0;
    std::string contract_version;
    std::string compiler_version;
};
PackageFidelitySummary Summarize(const LoadedFidelity& lf);

} // namespace dc::fidelity
