// dc/fidelity.hpp — title-authored fidelity contract model (DK0-M4; ADR-0026).
//
// Canon §17.2 + DC-BLUEPRINT §5 (VFR): a native title declares scalable
// quality DOMAINS with discrete STATES — never Low/Medium/High/Ultra (C6).
// The Fidelity Compiler (dev-time) turns contract + host capability into a
// validated candidate set; the runtime Fidelity Governor selects ONLY from
// that set (C5: highest fidelity means highest VALIDATED fidelity).
//
// This header is the shared, host-agnostic model used by:
//   - the compiler (tools/fidelity-compile)   — offline, deterministic
//   - the governor (runtime/core)             — runtime, deterministic
//   - the title (samples/)                    — applies the selected state
//
// Truth rules (C5/C7/C10, directive §17/§31):
//   - an unknown metric is UNKNOWN, never zero-cost
//   - unavailable evidence is never proof of compliance
//   - costs carry a provenance class: DECLARED (author), MEASURED (recorded
//     from a real run), CERTIFIED (evidence-policy-validated)
#pragma once

#include "dc/json.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dc::fidelity {

// ---- policy constants (directive §8): compile-time guardrails against
// exponential blowup. Exceeding them is a hard, structured rejection —
// never a silent truncation.
inline constexpr size_t kMaxDomains = 16;
inline constexpr size_t kMaxStatesPerDomain = 16;
inline constexpr size_t kMaxCombinations = 20000;

inline constexpr const char* kSchemaId = "dc.fidelity/1";

// ---- transition policies (directive §23; schema enum is canonical) --------
enum class TransitionPolicy {
    InstantSafe,        // may change any frame
    HystereticRuntime,  // runtime-adaptive with hysteresis + dwell
    CameraCutOnly,      // schema-legal; M4 governor refuses dynamically
    SceneBoundary,      // allowed at scene/load boundaries
    ReloadRequired,     // refuse dynamic transition
    RestartRequired,    // launch-time only
};
const char* TransitionPolicyName(TransitionPolicy p);
bool ParseTransitionPolicy(const std::string& s, TransitionPolicy* out);

// ---- cost provenance (directive §10) ---------------------------------------
enum class CostClass { Declared, Measured, Certified };
const char* CostClassName(CostClass c);
bool ParseCostClass(const std::string& s, CostClass* out);

// ---- experience intent (directive §16): policy input, not a preset ---------
enum class Intent { Automatic, Responsive, Balanced, Cinematic };
const char* IntentName(Intent i);
bool ParseIntent(const std::string& s, Intent* out);

// ---- optional metric value: KNOWN / UNKNOWN (never implicit zero) ----------
struct Metric {
    double value = 0.0;
    bool known = false;
    static Metric Unknown() { return {}; }
    static Metric Known(double v) { return {v, true}; }
};

// A single authored state inside a domain.
struct State {
    std::string id;         // "shadow_1" — unique across the whole contract
    int ordinal = 0;        // authoring order; monotonic per domain
    double utility = 0.0;   // [0,1] author-declared benefit
    Metric gpu_ms;          // per-frame costs at nominal load
    Metric cpu_ms;
    Metric vram_mb;         // residency deltas
    Metric io_mbps;         // streaming pressure
    CostClass cost_class = CostClass::Declared;  // §10 provenance
};

struct Domain {
    std::string name;                     // "shadow_quality"
    std::vector<State> states;            // ordered by ordinal
    TransitionPolicy transition = TransitionPolicy::HystereticRuntime;
    std::string criticality = "visual";   // visual|simulation|gameplay|audio
    bool dynamic = true;                  // false = launch-time selection only
};

// Versioned contract as authored by the title.
struct Contract {
    std::string schema = kSchemaId;
    std::string title_id;
    std::string contract_version;         // author bumps on material change
    std::vector<Domain> domains;
};

// Structured parse: every rejection carries a machine-readable reason code
// (directive §37). Parse never "best-effort" repairs a contract.
struct CandidateSet;
struct ParseResult {
    bool ok = false;
    std::string reason_code;   // e.g. FIDELITY_SCHEMA_INVALID
    std::string detail;
    Contract contract;
    std::unique_ptr<CandidateSet> candidate_set;  // set when parsing artifacts
    ~ParseResult();
    ParseResult() = default;
    ParseResult(ParseResult&&) noexcept;
    ParseResult& operator=(ParseResult&&) noexcept;
};

ParseResult ParseContract(const std::string& json_text);
// Contract → canonical deterministic JSON (sorted keys; round-trip stable).
std::string ContractJson(const Contract& c);

// ---- budget model (directive §17/§18) --------------------------------------
struct Budget {
    // Every budget axis is optional evidence. UNKNOWN never constrains and
    // never implies compliance — the governor records which axes bound it.
    Metric gpu_ms;          // per-frame GPU time allowance
    Metric cpu_ms;          // per-frame CPU (game thread) allowance
    Metric vram_mb;         // safe VRAM headroom
    Metric ram_mb;          // safe system-RAM headroom
    Metric io_mbps;         // safe streaming rate
    std::string policy_version;   // data-driven headroom policy (§18)
    double target_hz = 60.0;      // experience target (intent-scaled)
};

// ---- candidate set (compiler output; directive §30) ------------------------
struct Candidate {
    std::string id;                       // deterministic composite id
    std::vector<std::pair<std::string, std::string>> states;  // (domain, state) sorted
    double utility = 0.0;                 // sum of state utilities
    Metric gpu_ms, cpu_ms, vram_mb, io_mbps;   // totals; UNKNOWN-aware
    bool pareto = false;
    bool within_budget = false;
    std::vector<std::string> exceeded;    // budget axes exceeded (empty if ok)
};

// Domain policy copied from the contract into the artifact (§23): the
// governor must know which domains it is ALLOWED to change dynamically
// without re-reading the contract. Runtime-adaptive = dynamic &&
// (INSTANT_SAFE | HYSTERETIC_RUNTIME); everything else is launch-time only.
struct DomainPolicy {
    std::string name;
    TransitionPolicy transition = TransitionPolicy::HystereticRuntime;
    bool dynamic = true;
};

// Compiled, versioned candidate set — the ONLY thing the governor may select
// from (§19: the governor never invents a combination the compiler
// did not validate).
struct CandidateSet {
    std::string schema = "dc.fidelity-candidates/1";
    std::string title_id;
    std::string contract_version;
    std::string compiler_version;         // kCompilerVersion below
    std::string policy_version;           // budget policy the compile used
    std::string host_profile;             // host capability summary the
                                          // compile assumed (evidence id)
    std::string contract_sha256;          // SHA-256 over the CANONICAL contract
                                          // JSON this set was compiled from
                                          // (§29: stale-artifact invalidation)
    std::vector<DomainPolicy> domains;    // §23 transition legality per domain
    std::vector<Candidate> candidates;    // deterministic order (§38)
};

// Runtime-adaptive legality (§23). The governor refuses to change any
// domain where this returns false — dynamically OR on intent change.
bool DynamicLegal(const DomainPolicy& d);

inline constexpr const char* kCompilerVersion = "1";

ParseResult ParseCandidateSet(const std::string& json_text);
std::string CandidateSetJson(const CandidateSet& s);

// ---- runtime selection (governor output; directive §24/§25) ----------------
enum class SelectionReason {
    InitialSelection,
    GpuPressure, CpuPressure, VramPressure, RamPressure, IoPressure,
    ThermalPressure, LatencyPressure,
    Recovery,
    PlayerIntentChanged,
    SessionProfileChanged,
};
const char* SelectionReasonName(SelectionReason r);

struct Selection {
    std::string candidate_id;
    std::vector<std::pair<std::string, std::string>> states;
    SelectionReason reason = SelectionReason::InitialSelection;
    std::string previous_candidate_id;    // empty for the initial selection
    std::string host_profile;
    std::string session_profile;
    Intent intent = Intent::Automatic;
    std::string policy_version;
};

} // namespace dc::fidelity
