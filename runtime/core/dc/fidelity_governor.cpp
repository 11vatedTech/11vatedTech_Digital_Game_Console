// dc/fidelity_governor.cpp — deterministic runtime selection (DK0-M4 §19–§25).
#include "dc/fidelity_governor.hpp"

#include <algorithm>

namespace dc::fidelity {

namespace {

// §23 helpers: the candidate set carries each domain's transition policy
// (copied at compile time), so the governor can enforce legality without
// re-reading the contract.
const DomainPolicy* FindPolicy(const CandidateSet& set, const std::string& domain) {
    for (const auto& d : set.domains)
        if (d.name == domain) return &d;
    return nullptr;
}

// A RUNTIME transition may only change runtime-adaptive domains (§23);
// every non-dynamic-legal domain must keep its current state. Launch-time
// selection (SelectInitial) is exempt — that is what RESTART_REQUIRED means.
bool TransitionLegal(const CandidateSet& set,
                     const std::vector<std::pair<std::string, std::string>>& from,
                     const std::vector<std::pair<std::string, std::string>>& to) {
    for (const auto& [domain, state] : to) {
        const DomainPolicy* p = FindPolicy(set, domain);
        if (p && !DynamicLegal(*p)) {
            bool same = false;
            for (const auto& [fd, fs] : from)
                if (fd == domain && fs == state) { same = true; break; }
            if (!same) return false;
        }
    }
    return true;
}

// Fresh budget admission (§17/§19): the artifact's `within_budget` flag was
// evaluated at compile time; the RUNTIME budget is derived from live host
// evidence and may differ (policy update, evidence refresh). Re-validate —
// never trust a stale flag. UNKNOWN axes stay unconstrained-but-unproven.
// The intent scale applies to frame-time axes only (vram/io are absolute).
bool AdmitsRuntime(const Candidate& c, const Budget& b, double scale) {
    Budget s = b;
    if (s.gpu_ms.known) s.gpu_ms = Metric::Known(s.gpu_ms.value * scale);
    if (s.cpu_ms.known) s.cpu_ms = Metric::Known(s.cpu_ms.value * scale);
    return BudgetExceedances(c, s).empty();
}

// Intent scaling (directive §16): policy inputs, not presets.
//   Responsive → tighter effective frame budget (latency first)
//   Cinematic  → utility weighting favors the most expensive visual states
//   Automatic / Balanced → as compiled.
double IntentBudgetScale(Intent i) {
    switch (i) {
        case Intent::Responsive: return 0.85;
        case Intent::Balanced: return 1.0;
        case Intent::Cinematic: return 1.0;
        case Intent::Automatic: return 1.0;
    }
    return 1.0;
}

double IntentUtilityBias(Intent i) {
    // Utility is rank-ordered; the bias only breaks near-ties differently
    // per intent (kept deliberately simple and deterministic for M4).
    switch (i) {
        case Intent::Responsive: return 0.0;   // budget dominates
        case Intent::Cinematic: return 1e-9;   // prefer higher utility on ties
        default: return 0.0;
    }
}

// Pressure/health evaluation for one window against the budget (§22).
// Returns the binding reason if any axis is in pressure, or none.
struct Pressure {
    bool any = false;
    bool healthy = false;   // all KNOWN axes comfortably under budget
    SelectionReason reason = SelectionReason::InitialSelection;
    std::string detail;
};

Pressure Evaluate(const TelemetryWindow& w, const Budget& b,
                  const GovernorPolicy& p, Intent intent) {
    Pressure pr;
    const double scale = IntentBudgetScale(intent);
    auto axis = [&](const Metric& cost, const Metric& limit, double limit_scale,
                    SelectionReason r, const char* name) {
        if (!cost.known || !limit.known) return;   // UNKNOWN never fabricates
        const double allowed = limit.value * limit_scale;
        if (cost.value > allowed * p.pressure_ratio) {
            if (!pr.any) {
                pr.any = true;
                pr.reason = r;
                pr.detail = std::string(name) + " " + std::to_string(cost.value) +
                            " > " + std::to_string(allowed) + " (pressure ratio " +
                            std::to_string(p.pressure_ratio) + ")";
            }
        } else if (cost.value < allowed * p.recovery_ratio) {
            pr.healthy = true;
        }
    };
    axis(w.gpu_ms, b.gpu_ms, scale, SelectionReason::GpuPressure, "gpu");
    axis(w.cpu_ms, b.cpu_ms, scale, SelectionReason::CpuPressure, "cpu");
    axis(w.vram_mb, b.vram_mb, 1.0, SelectionReason::VramPressure, "vram");
    axis(w.io_mbps, b.io_mbps, 1.0, SelectionReason::IoPressure, "io");
    if (w.thermal_throttled && !pr.any) {
        pr.any = true;
        pr.reason = SelectionReason::ThermalPressure;
        pr.detail = "host reports thermal throttling";
    }
    return pr;
}

} // namespace

FidelityGovernor::FidelityGovernor(CandidateSet set, Budget budget,
                                   GovernorPolicy policy, Intent intent)
    : set_(std::move(set)), budget_(std::move(budget)),
      policy_(std::move(policy)), intent_(intent) {}

const Candidate* FidelityGovernor::BestForIntent(Intent i, const Candidate* from) const {
    // Best legal candidate = first (in the compiler's deterministic order)
    // validated, budget-OK candidate that the runtime budget admits. The
    // runtime budget — not the artifact's stale compile-time flag — decides
    // (§19); `from` (when non-null) constrains the transition to dynamic-legal
    // domain changes only (§23).
    const double scale = IntentBudgetScale(i);
    const double bias = IntentUtilityBias(i);
    const Candidate* best = nullptr;
    for (const auto& c : set_.candidates) {
        if (!c.pareto) continue;
        if (!AdmitsRuntime(c, budget_, scale)) continue;
        if (from && !TransitionLegal(set_, from->states, c.states)) continue;
        // Deterministic pick: highest (utility + bias), tie-break by the
        // compiler's candidate order (which is already deterministic).
        if (!best || c.utility + bias > best->utility + bias) best = &c;
    }
    return best;
}

Selection FidelityGovernor::SelectInitial(const std::string& session_profile) {
    const Candidate* best = BestForIntent(intent_, nullptr);   // launch-time: all domains selectable
    current_.candidate_id = best ? best->id : "";
    current_.states = best ? best->states : std::vector<std::pair<std::string, std::string>>{};
    current_.reason = SelectionReason::InitialSelection;
    current_.previous_candidate_id.clear();
    current_.host_profile = set_.host_profile;
    current_.session_profile = session_profile;
    current_.intent = intent_;
    current_.policy_version = policy_.version;

    DecisionEvent ev;
    ev.window_index = 0;
    ev.new_candidate = current_.candidate_id;
    ev.reason = SelectionReason::InitialSelection;
    ev.detail = best ? "initial best validated candidate"
                     : "NO VALID CANDIDATE within budget (title must handle)";
    trace_.push_back(std::move(ev));

    initial_selected_ = true;
    since_change_ = 0;
    return current_;
}

std::vector<DecisionEvent> FidelityGovernor::TransitionTo(const Candidate* next,
                                                          SelectionReason r,
                                                          const std::string& detail) {
    std::vector<DecisionEvent> out;
    if (!next || next->id == current_.candidate_id) return out;
    DecisionEvent ev;
    ev.window_index = window_index_;
    ev.previous_candidate = current_.candidate_id;
    ev.new_candidate = next->id;
    ev.reason = r;
    ev.detail = detail;
    out.push_back(ev);
    trace_.push_back(ev);

    current_.previous_candidate_id = current_.candidate_id;
    current_.candidate_id = next->id;
    current_.states = next->states;
    current_.reason = r;
    current_.intent = intent_;
    since_change_ = 0;
    pressure_windows_ = 0;
    healthy_windows_ = 0;
    return out;
}

bool FidelityGovernor::PinCandidate(const std::string& candidate_id) {
    // §35: the pin is a developer/capture affordance INTO the validated set,
    // never a way out of it. The candidate must be compiled, pareto, and
    // budget-admissible; otherwise the pin is refused.
    for (const auto& c : set_.candidates) {
        if (c.id != candidate_id) continue;
        if (!c.pareto || !AdmitsRuntime(c, budget_, 1.0)) return false;
        if (initial_selected_ && current_.candidate_id != candidate_id) {
            TransitionTo(&c, SelectionReason::InitialSelection,
                         "developer pin (diagnostics)");
        }
        pinned_ = true;
        return true;
    }
    return false;
}

std::vector<DecisionEvent> FidelityGovernor::Observe(const TelemetryWindow& w) {
    std::vector<DecisionEvent> out;
    ++window_index_;
    ++since_change_;
    if (pinned_) return out;   // §35: pinned → no adaptation, telemetry still accepted

    const Pressure pr = Evaluate(w, budget_, policy_, intent_);

    // Hysteresis counters (§22): pressure must PERSIST, recovery must
    // PERSIST — never frame-to-frame oscillation.
    if (pr.any) {
        ++pressure_windows_;
        healthy_windows_ = 0;
    } else if (pr.healthy) {
        ++healthy_windows_;
        pressure_windows_ = 0;
    } else {
        // neither in pressure nor comfortably healthy → hold
        pressure_windows_ = 0;
        healthy_windows_ = 0;
    }

    if (since_change_ < policy_.min_dwell_windows) return out;   // dwell (§22)

    // Step DOWN: sustained pressure → the highest-utility budget-admissible
    // candidate that is STRICTLY LIGHTER than the current one on the binding
    // axis (still from the compiled set only, §19; only dynamic-legal domain
    // changes, §23). Observed-vs-authored absolute comparison is a category
    // error — authored costs are nominal estimates; pressure already proves
    // the current candidate is too heavy, so any lighter candidate is the
    // correct direction.
    if (pressure_windows_ >= policy_.down_windows) {
        const double scale = IntentBudgetScale(intent_);
        const Candidate* cur = FindCandidate(current_.candidate_id);
        // §39 (no oscillation): "lighter" is judged on the BINDING axis —
        // the one that raised pressure. Any-axis lightness lets a candidate
        // that is heavier on the binding axis but lighter elsewhere pass,
        // ping-ponging between Pareto trade-offs under sustained overload.
        enum class Axis { Gpu, Cpu, Vram, Io, Any };
        const Axis binding = pr.reason == SelectionReason::GpuPressure   ? Axis::Gpu
                             : pr.reason == SelectionReason::CpuPressure ? Axis::Cpu
                             : pr.reason == SelectionReason::VramPressure ? Axis::Vram
                             : pr.reason == SelectionReason::IoPressure   ? Axis::Io
                                                                         : Axis::Any;
        const Candidate* best = nullptr;
        for (const auto& c : set_.candidates) {
            if (!c.pareto) continue;
            if (!AdmitsRuntime(c, budget_, scale)) continue;
            if (!TransitionLegal(set_, current_.states, c.states)) continue;
            if (cur) {
                auto lt = [](const Metric& a, const Metric& b) {
                    return a.known && b.known && a.value < b.value;
                };
                bool lighter;
                switch (binding) {
                    case Axis::Gpu:  lighter = lt(c.gpu_ms, cur->gpu_ms); break;
                    case Axis::Cpu:  lighter = lt(c.cpu_ms, cur->cpu_ms); break;
                    case Axis::Vram: lighter = lt(c.vram_mb, cur->vram_mb); break;
                    case Axis::Io:   lighter = lt(c.io_mbps, cur->io_mbps); break;
                    default:         // thermal/latency: any axis
                        lighter = lt(c.gpu_ms, cur->gpu_ms) ||
                                  lt(c.cpu_ms, cur->cpu_ms) ||
                                  lt(c.vram_mb, cur->vram_mb) ||
                                  lt(c.io_mbps, cur->io_mbps);
                        break;
                }
                if (!lighter) continue;
            }
            if (!best || c.utility > best->utility) best = &c;
        }
        if (best && best->id != current_.candidate_id)
            out = TransitionTo(best, pr.reason, pr.detail);
        pressure_windows_ = 0;
        return out;
    }

    // Step UP: sustained health → restore the best intent candidate (still
    // constrained to dynamic-legal domain changes, §23).
    if (healthy_windows_ >= policy_.up_windows) {
        const Candidate* cur = FindCandidate(current_.candidate_id);
        const Candidate* best = BestForIntent(intent_, cur);
        if (best && best->id != current_.candidate_id)
            out = TransitionTo(best, SelectionReason::Recovery,
                               "sustained healthy headroom");
        healthy_windows_ = 0;
    }
    return out;
}

std::vector<DecisionEvent> FidelityGovernor::SetIntent(Intent i) {
    std::vector<DecisionEvent> out;
    if (i == intent_) return out;
    intent_ = i;
    if (!initial_selected_) return out;
    // Intent change is a RUNTIME transition: only dynamic-legal domains may
    // change (§23) — the launch-time-only domains keep their current state.
    const Candidate* cur = FindCandidate(current_.candidate_id);
    const Candidate* best = BestForIntent(intent_, cur);
    if (best && best->id != current_.candidate_id)
        out = TransitionTo(best, SelectionReason::PlayerIntentChanged,
                           std::string("intent → ") + IntentName(i));
    return out;
}

const Candidate* FidelityGovernor::FindCandidate(const std::string& id) const {
    for (const auto& c : set_.candidates)
        if (c.id == id) return &c;
    return nullptr;
}

} // namespace dc::fidelity
