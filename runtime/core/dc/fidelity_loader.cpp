// dc/fidelity_loader.cpp — package-view fidelity loading (DK0-M4 §28/§30).
// See fidelity_loader.hpp for the boundary contract.
#define _CRT_SECURE_NO_WARNINGS
#include "dc/fidelity_loader.hpp"

#include "dc/hash.hpp"

#include <cstdio>
#include <cstdlib>

namespace dc::fidelity {

std::string ReadTextFile(const std::string& path, bool* ok) {
    *ok = false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string out;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (!bad) *ok = true;
    return out;
}

namespace {

bool VerifyArtifactMatchesContract(const CandidateSet& cs, const Contract& c,
                                   std::string* reason, std::string* detail) {
    // §29: an artifact compiled from a different contract (or by a different
    // compiler) must never silently drive a title.
    if (cs.title_id != c.title_id) {
        *reason = "FIDELITY_VERSION_MISMATCH";
        *detail = "candidate title_id '" + cs.title_id + "' != contract '" +
                  c.title_id + "'";
        return false;
    }
    if (cs.contract_version != c.contract_version) {
        *reason = "FIDELITY_VERSION_MISMATCH";
        *detail = "artifact contract_version '" + cs.contract_version +
                  "' != contract '" + c.contract_version + "'";
        return false;
    }
    if (cs.compiler_version != std::string(kCompilerVersion)) {
        *reason = "FIDELITY_VERSION_MISMATCH";
        *detail = "artifact compiler_version '" + cs.compiler_version +
                  "' != runtime compiler '" + kCompilerVersion + "'";
        return false;
    }
    // §29 contract binding: the artifact must have been compiled from THIS
    // canonical contract. A material contract edit invalidates stale
    // artifacts instead of silently driving the title with old assumptions.
    if (cs.contract_sha256 != dc::hash::Sha256::Hex(ContractJson(c))) {
        *reason = "FIDELITY_VERSION_MISMATCH";
        *detail = "artifact contract_sha256 does not match the canonical "
                  "contract (stale artifact after contract edit)";
        return false;
    }
    return true;
}

} // namespace

LoadedFidelity LoadFromPackageView(const std::string& gen_view_dir) {
    LoadedFidelity lf;
    // The gen view is normally the launch working directory (M3: the package
    // IS the install root). A dev-run launched from the exe subdirectory
    // still resolves: canonicalize, then search upward a bounded number of
    // steps for the manifests/ directory (at most: exe dir → gen view; and
    // never beyond the library subtree in practice).
    std::string base = gen_view_dir;
    {
        // Canonicalize FIRST so a relative launch dir (".") resolves against
        // the process CWD; then normalize separators. Without this the search
        // would strip the trailing slash of ".", get npos, and give up.
        char abs[_MAX_PATH];
        if (_fullpath(abs, base.c_str(), _MAX_PATH) != nullptr) base = abs;
        for (char& ch : base) if (ch == '\\') ch = '/';
    }
    for (int up = 0; up < 4; ++up) {
        bool ok = false;
        const std::string probe = base + "/manifests/fidelity.json";
        ReadTextFile(probe, &ok);
        if (ok) break;
        const size_t slash = base.find_last_of('/');
        if (slash == std::string::npos || slash == 0) break;
        base = base.substr(0, slash);
    }
    const std::string contract_path = base + "/manifests/fidelity.json";
    const std::string candidates_path =
        base + "/manifests/fidelity.candidates.json";

    bool ok = false;
    lf.contract_text = ReadTextFile(contract_path, &ok);
    if (!ok) {
        lf.reason_code = "FIDELITY_CONTRACT_MISSING";
        lf.detail = contract_path;
        return lf;
    }
    ParseResult pc = ParseContract(lf.contract_text);
    if (!pc.ok) {
        lf.reason_code = pc.reason_code.empty() ? "FIDELITY_CONTRACT_INVALID"
                                                : pc.reason_code;
        lf.detail = pc.detail;
        return lf;
    }
    lf.contract = std::move(pc.contract);

    bool cok = false;
    lf.candidates_text = ReadTextFile(candidates_path, &cok);
    if (!cok) {
        // Allowed: a dev-time package without a compiled artifact. The
        // caller may compile in-process; production activation requires it.
        lf.reason_code = "FIDELITY_CANDIDATES_MISSING";
        lf.detail = candidates_path;
        return lf;
    }
    ParseResult ps = ParseCandidateSet(lf.candidates_text);
    if (!ps.ok || !ps.candidate_set) {
        // The loader's published reason vocabulary (fidelity_loader.hpp) is
        // FIDELITY_CANDIDATES_INVALID; the parse-level reason + detail are
        // preserved in the detail so nothing is hidden.
        lf.reason_code = "FIDELITY_CANDIDATES_INVALID";
        lf.detail = ps.reason_code.empty() ? ps.detail
                                           : ps.reason_code + ": " + ps.detail;
        return lf;
    }
    if (!VerifyArtifactMatchesContract(*ps.candidate_set, lf.contract,
                                       &lf.reason_code, &lf.detail)) {
        return lf;
    }
    lf.candidates = std::move(*ps.candidate_set);
    lf.ok = true;
    return lf;
}

PackageFidelitySummary Summarize(const LoadedFidelity& lf) {
    PackageFidelitySummary s;
    s.domains = lf.contract.domains.size();
    for (const Domain& d : lf.contract.domains) s.states += d.states.size();
    for (const Candidate& c : lf.candidates.candidates)
        if (c.pareto) ++s.pareto_candidates;
    s.contract_version = lf.contract.contract_version;
    s.compiler_version = lf.candidates.compiler_version;
    return s;
}

} // namespace dc::fidelity
