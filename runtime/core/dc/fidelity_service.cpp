// dc/fidelity_service.cpp — semantic fidelity service implementation (§12).
#include "dc/fidelity_service.hpp"

#include "dc/fidelity_loader.hpp"

namespace dc::fidelity {

std::unique_ptr<FidelityService> FidelityService::Create(
    const ServiceConfig& cfg, std::string* reason, std::string* detail) {
    auto fail = [&](std::string r, std::string d) {
        if (reason) *reason = std::move(r);
        if (detail) *detail = std::move(d);
        return nullptr;
    };

    // Load from the INSTALLED package view only (§28). Never the source tree.
    auto lf = std::make_unique<LoadedFidelity>(
        LoadFromPackageView(cfg.gen_view_dir));
    if (!lf->ok) {
        // Loader reasons are already FIDELITY_* — never double-prefix.
        const bool prefixed = lf->reason_code.rfind("FIDELITY_", 0) == 0;
        return fail(prefixed ? lf->reason_code
                             : "FIDELITY_" + lf->reason_code,
                    lf->detail);
    }

    // Budget from the REAL host evidence carried in the launch context (§31).
    // M4 sample target: 60 Hz (the contract's envelope target; the full
    // envelope-negotiation path lands with the presentation work).
    Budget budget = BudgetFromHostEvidence(cfg.host_capability_json,
                                           cfg.budget_policy, 60.0);

    Intent intent = Intent::Automatic;
    if (!cfg.intent.empty() && !ParseIntent(cfg.intent, &intent)) {
        return fail("FIDELITY_INTENT_INVALID",
                    "unknown player intent '" + cfg.intent + "'");
    }

    auto svc = std::unique_ptr<FidelityService>(new FidelityService());
    svc->lf_ = std::move(lf);
    svc->budget_ = budget;
    svc->session_profile_ = cfg.session_profile;
    svc->governor_ = std::make_unique<FidelityGovernor>(
        svc->lf_->candidates, budget, cfg.governor_policy, intent);
    return svc;
}

bool FidelityService::SelectInitial() {
    if (!governor_) {
        last_error_ = "service not constructed";
        return false;
    }
    Selection s = governor_->SelectInitial(session_profile_);
    if (s.candidate_id.empty()) {
        last_error_ = "no validated candidate satisfies the current intent";
        return false;
    }
    return true;
}

std::vector<DecisionEvent> FidelityService::Observe(const TelemetryWindow& w) {
    if (!governor_ || !governor_->initial_selected()) return {};
    return governor_->Observe(w);
}

bool FidelityService::SetIntent(const std::string& intent) {
    if (!governor_) return false;
    Intent i = Intent::Automatic;
    if (!ParseIntent(intent, &i)) {
        last_error_ = "unknown player intent '" + intent + "'";
        return false;
    }
    governor_->SetIntent(i);
    return true;
}

std::string FidelityService::StateFor(const std::string& domain) const {
    if (!governor_) return {};
    for (const auto& [d, s] : governor_->current().states) {
        if (d == domain) return s;
    }
    return {};
}

} // namespace dc::fidelity
