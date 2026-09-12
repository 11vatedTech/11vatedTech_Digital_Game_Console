// dc-fidelity-compile — offline Fidelity Compiler (DK0-M4; directive §7/§30).
//
// Input:  title fidelity contract (dc.fidelity/1) + host capability evidence
//         (dc.host-capability/2) + budget policy (dc.fidelity-budget/1)
// Output: validated candidate artifact (dc.fidelity-candidates/1), written
//         deterministically (§30: no timestamps, no randomness).
//
// Budget derivation (§18/§31 — data-driven, documented, never invented):
//   gpu_ms  = physical frame budget at target Hz × headroom fraction
//   cpu_ms  = same policy applied to the game-thread budget
//   vram_mb = measured VRAM × policy safe fraction
//   io_mbps = measured storage read MB/s × policy safe fraction (UNKNOWN if
//             the host evidence lacks a measurement — UNKNOWN stays UNKNOWN)
#include "dc/fidelity.hpp"
#include "dc/fidelity_compiler.hpp"
#include "dc/hash.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool ReadFile(const std::string& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

dc::fidelity::Metric NumOrUnknown(const dc::json::Value& v, const char* path) {
    const dc::json::Value* p = &v;
    std::string key;
    // walk dotted path
    const dc::json::Value* cur = &v;
    std::string segment;
    for (size_t i = 0; i <= std::string(path).size(); ++i) {
        if (i == std::string(path).size() || path[i] == '.') {
            if (!cur || !cur->is_object()) return dc::fidelity::Metric::Unknown();
            const dc::json::Value* nxt = cur->find(segment);
            if (!nxt) return dc::fidelity::Metric::Unknown();
            cur = nxt;
            segment.clear();
        } else {
            segment += path[i];
        }
    }
    if (!cur->is_number()) return dc::fidelity::Metric::Unknown();
    return dc::fidelity::Metric::Known(cur->as_number());
}

// Budget policy v1 (versioned data in code until it earns a data file —
// §18: values are derived from explicit, documented rules, and the policy
// version travels with every compiled artifact).

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: dc-fidelity-compile <contract.json> <host-capability.json>"
                     " <out.candidates.json> [target_hz] [policy_version]\n");
        return 2;
    }
    std::string contract_text, host_text;
    if (!ReadFile(argv[1], &contract_text)) {
        std::fprintf(stderr, "error: cannot read contract %s\n", argv[1]);
        return 2;
    }
    if (!ReadFile(argv[2], &host_text)) {
        std::fprintf(stderr, "error: cannot read host evidence %s\n", argv[2]);
        return 2;
    }
    const double target_hz = argc > 4 ? std::atof(argv[4]) : 60.0;
    const std::string policy_version = argc > 5 ? argv[5] : "1";

    dc::fidelity::ParseResult c = dc::fidelity::ParseContract(contract_text);
    if (!c.ok) {
        std::fprintf(stderr, "error: contract rejected: %s — %s\n",
                     c.reason_code.c_str(), c.detail.c_str());
        return 1;
    }

    std::string herr;
    auto host = dc::json::Parse(host_text, herr);
    if (!host) {
        std::fprintf(stderr, "error: host evidence invalid: %s\n", herr.c_str());
        return 1;
    }

    dc::fidelity::BudgetPolicy pol;
    pol.version = policy_version;
    dc::fidelity::Budget budget = dc::fidelity::BudgetFromHostEvidence(
        host_text, pol, target_hz);

    dc::fidelity::CompileStats stats;
    // §30/§31: host_profile records WHICH host evidence the compile assumed —
    // the SHA-256 of the evidence bytes, not a human label. A newer evidence
    // file therefore invalidates the artifact's provenance linkage.
    dc::fidelity::CompileResult r = dc::fidelity::CompileCandidates(
        c.contract, budget, dc::hash::Sha256::Hex(host_text), &stats);
    if (!r.ok) {
        std::fprintf(stderr, "error: compile rejected: %s — %s\n",
                     r.reason_code.c_str(), r.detail.c_str());
        return 1;
    }

    // §29: bind the candidate set to the exact canonical contract bytes it
    // was compiled from. A material contract edit changes the canonical hash
    // → the loader rejects the stale artifact.
    r.set.contract_sha256 = dc::hash::Sha256::Hex(
        dc::fidelity::ContractJson(c.contract));

    const std::string out = dc::fidelity::CandidateSetJson(r.set);
    std::ofstream f(argv[3], std::ios::binary);
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
    if (!f) {
        std::fprintf(stderr, "error: cannot write %s\n", argv[3]);
        return 1;
    }

    std::printf("compiled %s: %zu enumerated, %zu within budget, %zu pareto, %zu dominated\n",
                r.set.title_id.c_str(), stats.enumerated, stats.budget_ok,
                stats.pareto, stats.dominated);
    std::printf("budget: gpu<=%.2fms cpu<=%.2fms vram<=%.0fMB io=%s policy=%s\n",
                budget.gpu_ms.known ? budget.gpu_ms.value : -1.0,
                budget.cpu_ms.known ? budget.cpu_ms.value : -1.0,
                budget.vram_mb.known ? budget.vram_mb.value : -1.0,
                budget.io_mbps.known ? "known" : "UNKNOWN", policy_version.c_str());
    return 0;
}
