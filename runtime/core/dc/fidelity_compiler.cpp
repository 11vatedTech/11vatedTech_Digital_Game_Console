// dc/fidelity_compiler.cpp — deterministic offline compile (DK0-M4 §7–§10).
#include "dc/fidelity_compiler.hpp"

#include <algorithm>
#include <numeric>
#include <set>

namespace dc::fidelity {

namespace {

// Deterministic candidate id: domain:state pairs joined in enumeration
// order — stable across runs and machines (§38: same inputs → same ids).
std::string MakeCandidateId(const std::vector<std::pair<std::string, std::string>>& states) {
    std::string id;
    for (const auto& s : states) {
        if (!id.empty()) id += '+';
        id += s.first + ":" + s.second;
    }
    return id;
}

// Sum a per-state metric axis; UNKNOWN anywhere → UNKNOWN total (§17).
Metric SumAxis(const std::vector<const State*>& chosen,
               Metric (State::*axis)) {
    Metric total;
    for (const State* st : chosen) {
        const Metric& m = (*st).*axis;
        if (!m.known) return Metric::Unknown();
        total.value += m.value;
        total.known = true;
    }
    return total;
}

} // namespace

std::vector<std::string> BudgetExceedances(const Candidate& cand,
                                           const Budget& b) {
    std::vector<std::string> out;
    auto check = [&](const Metric& cost, const Metric& limit, const char* name) {
        // Unknown limit → unconstrained (recorded, not hidden: the compiled
        // artifact carries the full budget so evidence shows which axes were
        // actually enforced). Unknown cost → cannot prove compliance; the
        // candidate is NOT marked exceeded (no false accusation), but its
        // cost_class stays DECLARED so downstream certification knows.
        if (!limit.known || !cost.known) return;
        if (cost.value > limit.value) out.push_back(name);
    };
    check(cand.gpu_ms, b.gpu_ms, "gpu");
    check(cand.cpu_ms, b.cpu_ms, "cpu");
    check(cand.vram_mb, b.vram_mb, "vram");
    // ram/io: candidate totals currently carry only the four authored axes;
    // ram is enforced at the host level (system memory pressure), io when
    // the contract authors io_mbps costs.
    check(cand.io_mbps, b.io_mbps, "io");
    return out;
}

bool Dominates(const Candidate& a, const Candidate& b) {
    if (a.utility < b.utility) return false;
    auto axis_le = [](const Metric& x, const Metric& y, bool* strictly) {
        // UNKNOWN on either side → this axis cannot establish dominance
        // (unknown is not zero — §17) and cannot block it either.
        if (!x.known || !y.known) return true;
        if (x.value > y.value) return false;
        *strictly = *strictly || (x.value < y.value);
        return true;
    };
    bool strictly = false;
    if (!axis_le(a.gpu_ms, b.gpu_ms, &strictly)) return false;
    if (!axis_le(a.cpu_ms, b.cpu_ms, &strictly)) return false;
    if (!axis_le(a.vram_mb, b.vram_mb, &strictly)) return false;
    if (!axis_le(a.io_mbps, b.io_mbps, &strictly)) return false;
    if (a.utility > b.utility) strictly = true;
    return strictly;
}

CompileResult CompileCandidates(const Contract& c, const Budget& budget,
                                const std::string& host_profile,
                                CompileStats* stats) {
    CompileResult r;
    CompileStats local;

    // 0. Re-validate the contract defensively (the compiler is a boundary;
    //    callers may have loaded it from a file).
    if (c.domains.empty() || c.title_id.empty()) {
        r.reason_code = "FIDELITY_SCHEMA_INVALID";
        r.detail = "empty contract";
        return r;
    }

    // 1. Enumerate combinations in domain × state order (deterministic).
    size_t total = 1;
    for (const auto& d : c.domains) total *= d.states.size();
    if (total > kMaxCombinations) {
        r.reason_code = "FIDELITY_POLICY_LIMIT";
        r.detail = "combinations " + std::to_string(total) + " > kMaxCombinations";
        return r;
    }

    std::vector<Candidate> all;
    all.reserve(total);
    std::vector<size_t> idx(c.domains.size(), 0);
    while (true) {
        Candidate cand;
        std::vector<const State*> chosen;
        chosen.reserve(c.domains.size());
        for (size_t i = 0; i < c.domains.size(); ++i) {
            const State& st = c.domains[i].states[idx[i]];
            chosen.push_back(&st);
            cand.states.emplace_back(c.domains[i].name, st.id);
        }
        cand.id = MakeCandidateId(cand.states);
        cand.utility = std::accumulate(chosen.begin(), chosen.end(), 0.0,
                                       [](double acc, const State* st) {
                                           return acc + st->utility;
                                       });
        cand.gpu_ms = SumAxis(chosen, &State::gpu_ms);
        cand.cpu_ms = SumAxis(chosen, &State::cpu_ms);
        cand.vram_mb = SumAxis(chosen, &State::vram_mb);
        cand.io_mbps = SumAxis(chosen, &State::io_mbps);
        cand.exceeded = BudgetExceedances(cand, budget);
        cand.within_budget = cand.exceeded.empty();
        // A candidate whose cost axes are all KNOWN and authored as measured
        // carries the strongest provenance; otherwise DECLARED (§10).
        all.push_back(std::move(cand));
        ++local.enumerated;

        // odometer advance
        size_t i = c.domains.size();
        while (i > 0) {
            --i;
            if (++idx[i] < c.domains[i].states.size()) break;
            idx[i] = 0;
            if (i == 0) goto done;
        }
    }
done:

    // 2. Budget filter (§7 step 7). Candidates exceeding a KNOWN budget are
    //    kept in the artifact as `within_budget=false` — inspectable
    //    evidence — but are ineligible for selection.
    for (const auto& cd : all)
        if (cd.within_budget) ++local.budget_ok;

    // 3. Pareto filtering (§9) — computed among budget-OK candidates only
    //    (an over-budget candidate is not a viable alternative, so it cannot
    //    dominate anything). Dominance is exact per Dominates().
    for (Candidate& cd : all) {
        if (!cd.within_budget) continue;
        cd.pareto = true;
        for (const Candidate& other : all) {
            if (&other == &cd || !other.within_budget) continue;
            if (Dominates(other, cd)) { cd.pareto = false; break; }
        }
        if (cd.pareto) ++local.pareto;
    }
    for (const Candidate& cd : all)
        if (cd.within_budget && !cd.pareto) ++local.dominated;

    // 4. Deterministic ordering (§38): pareto first (utility desc, id asc),
    //    then the rest (utility desc, id asc). Stable across machines.
    std::stable_sort(all.begin(), all.end(),
                     [](const Candidate& x, const Candidate& y) {
                         if (x.pareto != y.pareto) return x.pareto;
                         if (x.utility != y.utility) return x.utility > y.utility;
                         return x.id < y.id;
                     });

    r.ok = true;
    r.set.title_id = c.title_id;
    r.set.contract_version = c.contract_version;
    r.set.compiler_version = kCompilerVersion;
    r.set.policy_version = budget.policy_version;
    r.set.host_profile = host_profile;
    // §23: copy domain transition policies into the artifact so the runtime
    // governor can enforce legality without re-reading the contract.
    r.set.domains.reserve(c.domains.size());
    for (const auto& d : c.domains)
        r.set.domains.push_back({d.name, d.transition, d.dynamic});
    r.set.candidates = std::move(all);
    if (stats) *stats = local;
    return r;
}

Budget BudgetFromHostEvidence(const std::string& host_json,
                              const BudgetPolicy& policy, double target_hz) {
    Budget b;
    b.policy_version = policy.version;
    b.target_hz = target_hz;
    const double physical_ms = 1000.0 / target_hz;
    b.gpu_ms = Metric::Known(physical_ms * policy.gpu_headroom);
    b.cpu_ms = Metric::Known(physical_ms * policy.cpu_headroom);

    std::string err;
    auto host = json::Parse(host_json, err);
    if (!host) return b;  // frame budgets stand; evidence axes stay UNKNOWN

    // Measured VRAM × safe fraction. Missing/unparseable → UNKNOWN.
    if (const json::Value* gpu = host->find("gpu"); gpu && gpu->is_object()) {
        if (const json::Value* v = gpu->find("vram_bytes"); v && v->is_number())
            b.vram_mb = Metric::Known(v->as_number() / (1024.0 * 1024.0) *
                                      policy.vram_safe_frac);
    }
    // Streaming: first storage device's measured read rate × safe fraction.
    if (const json::Value* st = host->find("storage"); st && st->is_array() &&
        !st->as_array().empty() && st->as_array()[0].is_object()) {
        if (const json::Value* rm = st->as_array()[0].find("read_mbps");
            rm && rm->is_number())
            b.io_mbps = Metric::Known(rm->as_number() * policy.io_safe_frac);
    }
    return b;
}

} // namespace dc::fidelity
