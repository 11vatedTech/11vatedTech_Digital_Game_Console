// dc/fidelity_service.hpp — semantic fidelity service boundary (DK0-M4 §12).
//
// Titles consume semantic fidelity state through this interface; they never
// see the compiler's optimization structures, the candidate-set file, or any
// platform type (directive §12/§13). The implementation wraps the runtime
// FidelityGovernor with explicit ownership (no process-global state, §13):
// the session constructs it from the INSTALLED package view + the typed
// launch context and injects it through ConsoleServices.
//
// Hosting boundary (ADR-0026): in DK0-M4 the service is hosted in the title
// process as the ConsoleServices embryo. The cross-process service pipe is
// future work and does not change this interface.
#pragma once

#include "dc/fidelity.hpp"
#include "dc/fidelity_compiler.hpp"
#include "dc/fidelity_governor.hpp"
#include "dc/fidelity_loader.hpp"

#include <memory>
#include <string>
#include <vector>

namespace dc::fidelity {

struct ServiceConfig {
    std::string gen_view_dir;     // installed package generation view (§28)
    std::string host_capability_json;  // from the launch context (§31)
    std::string intent;           // player intent from the launch context (§16)
    std::string session_profile;  // active DCX claims summary (may be empty)
    BudgetPolicy budget_policy;   // versioned budget/headroom policy (§18)
    GovernorPolicy governor_policy;  // versioned hysteresis/dwell policy (§22)
};

// Constructed with explicit ownership; never a global (§13).
class FidelityService {
public:
    // Factory: loads contract + compiled candidate artifact from the package
    // view, derives the budget from the real host evidence, and applies the
    // intent. Fails closed with a structured reason (§37) — a title MUST
    // handle service-failure explicitly (it may fall back to its own lowest
    // state; it must never silently self-select a preset).
    static std::unique_ptr<FidelityService> Create(const ServiceConfig& cfg,
                                                   std::string* reason,
                                                   std::string* detail);

    // Perform the initial selection (§20). Returns false on a service whose
    // candidate set could not satisfy any candidate (structured in last_error).
    bool SelectInitial();

    // Title feeds one measured telemetry window (§10: measured, never
    // fabricated). Returns decisions made (may be empty).
    std::vector<DecisionEvent> Observe(const TelemetryWindow& w);

    // Player intent change (§16). Returns false for an unknown intent string.
    bool SetIntent(const std::string& intent);

    // Semantic accessors (§12).
    const Selection& CurrentSelection() const { return governor_->current(); }
    const Budget& CurrentBudget() const { return budget_; }
    const std::vector<DecisionEvent>& Trace() const { return governor_->trace(); }
    const Contract& contract() const { return lf_->contract; }
    bool initial_selected() const { return governor_->initial_selected(); }

    // Developer diagnostics only (§35): pin to a validated candidate id.
    // Refused for ids outside the compiled artifact set.
    bool PinCandidate(const std::string& candidate_id) {
        return governor_ ? governor_->PinCandidate(candidate_id) : false;
    }

    // Helper for titles: resolve the selected state id for one domain.
    // Returns "" when the domain is absent from the selection.
    std::string StateFor(const std::string& domain) const;

    const std::string& last_error() const { return last_error_; }

private:
    FidelityService() = default;
    std::unique_ptr<LoadedFidelity> lf_;   // package-view load (§28)
    Budget budget_{};
    std::unique_ptr<FidelityGovernor> governor_;
    std::string session_profile_;
    std::string last_error_;
};

} // namespace dc::fidelity
