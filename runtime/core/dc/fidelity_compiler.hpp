// dc/fidelity_compiler.hpp — offline Fidelity Compiler (DK0-M4; ADR-0026).
//
// Turns (contract, budget, policy) into a validated, deterministically
// ordered candidate set (directive §7). Pure and host-agnostic: identical
// inputs → identical artifact bytes (§38). The compiler NEVER sees runtime
// telemetry and the governor NEVER re-runs this search (§19/§30).
//
// Pipeline: validate → enumerate (bounded by kMaxCombinations) → cost
// totals (UNKNOWN-aware) → budget evaluation → Pareto filtering →
// deterministic ordering. Every stage emits inspectable evidence (§9):
// all_valid / within_budget / dominated / pareto candidates.
#pragma once

#include "dc/fidelity.hpp"

namespace dc::fidelity {

// Aggregation policy for UNKNOWN cost axes (directive §17):
//   - an axis is UNKNOWN if ANY selected state's cost on that axis is
//     UNKNOWN → the candidate's total on that axis is UNKNOWN
//   - UNKNOWN is never treated as zero and never proves compliance; the
//     budget check marks the axis "unconstrained-but-unproven" and the
//     candidate carries cost_class DECLARED for it.
struct CompileResult {
    bool ok = false;
    std::string reason_code;
    std::string detail;
    CandidateSet set;
};

struct CompileStats {
    size_t enumerated = 0;       // legal combinations before filtering
    size_t budget_ok = 0;        // within every KNOWN budget axis
    size_t pareto = 0;           // survived dominance filtering
    size_t dominated = 0;        // removed as dominated
};

// Deterministic compile. `budget` may carry UNKNOWN axes; `host_profile`
// records which host capability the compile assumed (evidence linkage).
CompileResult CompileCandidates(const Contract& c, const Budget& budget,
                                const std::string& host_profile,
                                CompileStats* stats = nullptr);

// Budget evaluation for a single candidate against a budget (UNKNOWN-aware;
// directive §17). Returns exceeded axis names in canonical order:
// gpu, cpu, vram, ram, io.
std::vector<std::string> BudgetExceedances(const Candidate& cand,
                                           const Budget& budget);

// Pareto dominance (directive §9). A dominates B iff A is >= in utility,
// <= in every KNOWN cost axis, and strictly better in at least one.
// UNKNOWN axes never dominate (unknown ≠ zero).
bool Dominates(const Candidate& a, const Candidate& b);

// ---- budget policy (directive §18: data-driven headroom, versioned) --------
// Shared by the offline compiler tool AND the runtime so both sides derive
// identical budgets from identical host evidence — the governor must select
// against the same constraint the artifact was compiled under.
struct BudgetPolicy {
    std::string version = "1";
    double gpu_headroom = 0.89;    // 16.67 ms → ~14.8 ms certification budget
    double cpu_headroom = 0.85;
    double vram_safe_frac = 0.80;  // never award beyond 80% of measured VRAM
    double io_safe_frac = 0.70;
};

// Derive a Budget from a dc.host-capability/2 document (§31: REAL evidence —
// measured VRAM/storage; never a synthetic score). Missing measurements stay
// UNKNOWN on the corresponding axis. Never fabricates values.
Budget BudgetFromHostEvidence(const std::string& host_capability_json,
                              const BudgetPolicy& policy, double target_hz);

} // namespace dc::fidelity
