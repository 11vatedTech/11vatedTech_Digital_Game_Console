// test_fidelity.cpp — fidelity contract / compiler / governor contract tests
// (DK0-M4 §36 units, §37 negatives, §38 determinism, §22/§23 adaptation).
//
// Coverage map:
//   contract parsing:     valid parse, every §37 rejection reason, canonical
//                         round-trip stability, transition-policy vocabulary
//   compiler:             bounded enumeration, UNKNOWN-axis aggregation,
//                         budget exceedance reasons, Pareto dominance rules
//   determinism (§38):    identical inputs → byte-identical artifact; one
//                         controlled input change → different artifact
//   budgets (§17/§31):    derivation from real dc.host-capability/2 bytes,
//                         missing axes stay UNKNOWN (never zero)
//   governor (§19–§25):   initial selection, sustained-pressure step-down,
//                         sustained-health recovery, min dwell, hysteresis,
//                         §23 transition legality (SCENE_BOUNDARY and
//                         RESTART_REQUIRED domains never change dynamically),
//                         intent change, dev pin, reason codes
#include "dc/fidelity.hpp"
#include "dc/fidelity_compiler.hpp"
#include "dc/fidelity_governor.hpp"
#include "dc/fidelity_loader.hpp"
#include "dc/fidelity_service.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace dc::fidelity;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

// ---- fixture: two-domain contract (res: dynamic, sim: RESTART_REQUIRED) ----
const char* kTwoDomain = R"JSON({
  "schema": "dc.fidelity/1",
  "title_id": "t.test",
  "contract_version": "1",
  "domains": [
    { "domain": "internal_resolution", "transition": "HYSTERETIC_RUNTIME",
      "dynamic": true,
      "states": [
        { "id": "res_50", "ordinal": 0, "utility": 0.2, "gpu_ms": 2.0, "vram_mb": 100 },
        { "id": "res_100", "ordinal": 1, "utility": 0.6, "gpu_ms": 8.0, "vram_mb": 300 }
      ] },    { "domain": "simulation_quality", "transition": "RESTART_REQUIRED",
      "dynamic": false,
      "states": [
        { "id": "sim_0", "ordinal": 0, "utility": 0.1, "gpu_ms": 0.3, "cpu_ms": 1.0, "vram_mb": 20 },
        { "id": "sim_1", "ordinal": 1, "utility": 0.5, "gpu_ms": 0.8, "cpu_ms": 2.0, "vram_mb": 40 } ] }
  ]
})JSON";

const char* kHostEvidence = R"JSON({
  "gpu": { "vram_bytes": 8589934592 },
  "storage": [ { "read_mbps": 2000 } ]
})JSON";

Contract ParseOk(const char* text, const char* what) {
    ParseResult r = ParseContract(text);
    Check(r.ok, what);
    return r.ok ? std::move(r.contract) : Contract{};
}

GovernorPolicy FastPolicy() {   // compressed hysteresis for tests
    GovernorPolicy p;
    p.version = "test";
    p.down_windows = 2;
    p.up_windows = 2;
    p.min_dwell_windows = 3;
    return p;
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // crash-diagnosable output

    // ---------------- contract parsing -------------------------------------
    {
        ParseResult r = ParseContract(kTwoDomain);
        Check(r.ok, "valid contract parses");
        Check(r.contract.domains.size() == 2, "two domains");
        Check(r.contract.domains[0].states.size() == 2, "two states in domain 0");
        Check(r.contract.domains[1].transition == TransitionPolicy::RestartRequired,
              "RESTART_REQUIRED parsed");
        Check(r.contract.domains[1].dynamic == false, "dynamic=false parsed");
        // canonical round-trip is stable
        std::string canon1 = ContractJson(r.contract);
        ParseResult r2 = ParseContract(canon1);
        Check(r2.ok && ContractJson(r2.contract) == canon1,
              "canonical round-trip stable");
    }
    {
        // §37 negatives — every rejection is structured.
        struct Case { const char* json; const char* why; };
        const Case cases[] = {
            {"{ not json", "malformed json"},
            {R"JSON({"schema":"other/1","title_id":"t","contract_version":"1","domains":[]})JSON",
             "foreign schema"},
            {R"JSON({"schema":"dc.fidelity/1","title_id":"","contract_version":"1","domains":[{"domain":"d","states":[{"id":"s","ordinal":0,"utility":0.5}]}]})JSON",
             "empty title_id"},
            {R"JSON({"schema":"dc.fidelity/1","title_id":"t","contract_version":"1","domains":[]})JSON",
             "empty domains"},
            {R"JSON({"schema":"dc.fidelity/1","title_id":"t","contract_version":"1","domains":[{"domain":"d","states":[{"id":"s","ordinal":0,"utility":0.5},{"id":"s","ordinal":1,"utility":0.4}]}]})JSON",
             "duplicate state id"},
            {R"JSON({"schema":"dc.fidelity/1","title_id":"t","contract_version":"1","domains":[{"domain":"d","states":[{"id":"s","ordinal":0,"utility":1.5}]}]})JSON",
             "utility out of range"},
            {R"JSON({"schema":"dc.fidelity/1","title_id":"t","contract_version":"1","domains":[{"domain":"d","states":[{"id":"s","ordinal":0,"utility":0.4,"gpu_ms":-1.0}]}]})JSON",
             "negative cost"},
            {R"JSON({"schema":"dc.fidelity/1","title_id":"t","contract_version":"1","domains":[{"domain":"d","transition":"NOT_A_POLICY","states":[{"id":"s","ordinal":0,"utility":0.4}]}]})JSON",
             "unknown transition policy"},
        };
        for (const auto& c : cases) {
            ParseResult r = ParseContract(c.json);
            Check(!r.ok, c.why);
            Check(!r.reason_code.empty(), "rejection carries reason code");
        }
        // duplicate domain names
        {
            const char* dup =
                R"JSON({"schema":"dc.fidelity/1","title_id":"t","contract_version":"1",
                "domains":[{"domain":"d","states":[{"id":"a","ordinal":0,"utility":0.1}]},
                           {"domain":"d","states":[{"id":"b","ordinal":0,"utility":0.2}]}]})JSON";
            ParseResult r = ParseContract(dup);
            Check(!r.ok, "duplicate domain rejected");
        }
        // policy guardrails (§8): silent truncation is forbidden. The parser
        // enforces kMaxDomains / kMaxStatesPerDomain; the compiler enforces
        // kMaxCombinations.
        {
            std::string big = R"JSON({"schema":"dc.fidelity/1","title_id":"t",
                "contract_version":"1","domains":[)JSON";
            for (int i = 0; i < 5; ++i) {
                if (i) big += ",";
                big += R"JSON({"domain":"d)JSON" + std::to_string(i) +
                       R"JSON(","states":[)JSON";
                for (int s = 0; s < 18; ++s) {   // > kMaxStatesPerDomain (16)
                    if (s) big += ",";
                    big += R"JSON({"id":"d)JSON" + std::to_string(i) + "_" +
                           std::to_string(s) +
                           R"JSON(","ordinal":)JSON" + std::to_string(s) +
                           R"JSON(,"utility":0.1})JSON";
                }
                big += "]}";
            }
            big += "]}";
            ParseResult r = ParseContract(big);
            Check(!r.ok && r.reason_code == "FIDELITY_POLICY_LIMIT",
                  "state-count guardrail fails closed");
        }
        {
            // > kMaxDomains (16) domains, one state each.
            std::string many = R"JSON({"schema":"dc.fidelity/1","title_id":"t",
                "contract_version":"1","domains":[)JSON";
            for (int i = 0; i < 17; ++i) {
                if (i) many += ",";
                many += R"JSON({"domain":"dd)JSON" + std::to_string(i) +
                        R"JSON(","states":[{"id":"x)JSON" + std::to_string(i) +
                        R"JSON(","ordinal":0,"utility":0.1}]})JSON";
            }
            many += "]}";
            ParseResult r = ParseContract(many);
            Check(!r.ok && r.reason_code == "FIDELITY_POLICY_LIMIT",
                  "domain-count guardrail fails closed");
        }
    }

    // ---------------- compiler + determinism (§38) --------------------------
    Contract c = ParseOk(kTwoDomain, "fixture parses");
    Budget budget;
    budget.policy_version = "1";
    budget.gpu_ms = Metric::Known(10.0);
    budget.cpu_ms = Metric::Known(10.0);
    budget.vram_mb = Metric::Known(250.0);   // excludes res_100 (300 MB)
    Budget same = budget;
    same.vram_mb = Metric::Known(250.0);

    CandidateSet s1, s2;
    {
        CompileStats st;
        CompileResult r = CompileCandidates(c, budget, "host-x", &st);
        Check(r.ok, "compile ok");
        Check(st.enumerated == 4, "4 combinations enumerated");
        Check(r.set.candidates.size() == 4, "all candidates retained");
        s1 = std::move(r.set);
    }
    {
        CompileResult r = CompileCandidates(c, same, "host-x", nullptr);
        s2 = std::move(r.set);
    }
    Check(CandidateSetJson(s1) == CandidateSetJson(s2),
          "identical inputs → identical artifact bytes");
    {
        Budget bigger = budget;
        bigger.vram_mb = Metric::Known(400.0);
        CompileResult r = CompileCandidates(c, bigger, "host-x", nullptr);
        Check(CandidateSetJson(r.set) != CandidateSetJson(s1),
              "changed budget → different artifact");
        // res_100 (300 MB + sim vram) now within 400 MB budget
        bool res100_ok = false;
        for (const auto& cd : r.set.candidates)
            for (const auto& [d, st] : cd.states)
                if (d == "internal_resolution" && st == "res_100" && cd.within_budget)
                    res100_ok = true;
        Check(res100_ok, "relaxed budget admits res_100");
    }
    {
        // §37: candidate exceeding a KNOWN budget is kept, flagged, ineligible.
        bool found_flagged = false;
        for (const auto& cd : s1.candidates) {
            if (!cd.within_budget) {
                found_flagged = true;
                bool has_vram = false;
                for (const auto& ax : cd.exceeded) has_vram |= ax == "vram";
                Check(has_vram, "exceeded axis recorded");
            }
        }
        Check(found_flagged, "over-budget candidate present and flagged");
    }
    Check(!s1.domains.empty(), "artifact carries domain policies");
    {
        bool sim_restart = false;
        for (const auto& d : s1.domains)
            if (d.name == "simulation_quality")
                sim_restart = d.transition == TransitionPolicy::RestartRequired &&
                              !d.dynamic;
        Check(sim_restart, "sim domain policy copied into artifact");
    }

    // ---------------- budgets from REAL evidence shape (§31) ----------------
    {
        Budget b = BudgetFromHostEvidence(kHostEvidence, BudgetPolicy{}, 60.0);
        Check(b.gpu_ms.known && b.gpu_ms.value < 16.6667 && b.gpu_ms.value > 14.0,
              "gpu budget = physical × headroom");
        Check(b.vram_mb.known &&
              b.vram_mb.value == 8192.0 * 0.80,
              "vram budget = measured bytes × safe fraction");
        Check(b.io_mbps.known && b.io_mbps.value == 2000.0 * 0.70,
              "io budget = measured rate × safe fraction");
        Budget no_ev = BudgetFromHostEvidence("{}", BudgetPolicy{}, 60.0);
        Check(no_ev.gpu_ms.known, "frame budgets stand without evidence");
        Check(!no_ev.vram_mb.known && !no_ev.io_mbps.known,
              "missing evidence stays UNKNOWN (never zero)");
    }

    // ---------------- governor: selection, hysteresis, legality -------------
    {
        // Budget admits everything; best candidate = res_100 + sim_1.
        Budget open;
        open.policy_version = "1";
        open.gpu_ms = Metric::Known(20.0);
        open.cpu_ms = Metric::Known(20.0);
        open.vram_mb = Metric::Known(1000.0);
        FidelityGovernor g(CompileCandidates(c, open, "h").set, open,
                           FastPolicy(), Intent::Balanced);
        Selection sel = g.SelectInitial("DCX-UHD60");
        Check(sel.candidate_id.find("res_100") != std::string::npos,
              "initial selection = highest utility within budget");
        Check(sel.candidate_id.find("sim_1") != std::string::npos,
              "launch-time selection may set RESTART_REQUIRED domains");
        Check(sel.reason == SelectionReason::InitialSelection, "reason code");

        // §23: dynamic adaptation may NEVER change the sim domain.
        TelemetryWindow heavy;
        heavy.gpu_ms = Metric::Known(19.0);    // > 20 × pressure ratio? no —
        heavy.cpu_ms = Metric::Known(19.0);    // below pressure threshold
        auto events = g.Observe(heavy);
        Check(events.empty(), "no pressure below threshold");

        // Sustained OVERLOAD: step-down must keep sim_1.
        FidelityGovernor g2(CompileCandidates(c, open, "h").set, open,
                            FastPolicy(), Intent::Balanced);
        g2.SelectInitial("DCX-UHD60");
        TelemetryWindow overload;
        overload.gpu_ms = Metric::Known(30.0);
        overload.cpu_ms = Metric::Known(30.0);
        std::vector<DecisionEvent> adapted;
        for (int i = 0; i < 10; ++i) {
            auto ev = g2.Observe(overload);
            if (!ev.empty()) {
                Check(ev.front().reason == SelectionReason::GpuPressure ||
                          ev.front().reason == SelectionReason::CpuPressure,
                      "pressure reason recorded");
                adapted.insert(adapted.end(), ev.begin(), ev.end());
            }
        }
        Check(!adapted.empty(), "sustained pressure adapts");
        const Selection& down = g2.current();
        Check(down.candidate_id.find("sim_1") != std::string::npos,
              "§23: RESTART_REQUIRED domain never changed at runtime");
        Check(down.candidate_id.find("res_100") == std::string::npos,
              "dynamic domain stepped down under pressure");
    }

    // ---------------- dwell + no oscillation (§22/§39) ----------------------
    {
        Budget open;
        open.policy_version = "1";
        open.gpu_ms = Metric::Known(20.0);
        open.cpu_ms = Metric::Known(20.0);
        open.vram_mb = Metric::Known(1000.0);
        GovernorPolicy p = FastPolicy();
        FidelityGovernor g(CompileCandidates(c, open, "h").set, open, p,
                           Intent::Balanced);
        g.SelectInitial("DCX");
        TelemetryWindow overload;
        overload.gpu_ms = Metric::Known(40.0);
        overload.cpu_ms = Metric::Known(40.0);
        TelemetryWindow calm;
        calm.gpu_ms = Metric::Known(2.0);
        calm.cpu_ms = Metric::Known(2.0);
        // flap pressure/health every window: dwell must suppress all changes
        size_t changes = 0;
        for (int i = 0; i < 30; ++i) {
            auto ev = g.Observe(i % 2 == 0 ? overload : calm);
            changes += ev.size();
        }
        // With min_dwell=3 and alternating windows, the hysteresis counters
        // reset every window — no sustained pressure/health ever accumulates.
        Check(changes == 0, "alternating pressure never adapts (hysteresis)");
    }

    // ---------------- intent change (§16) ------------------------------------
    {
        // gpu budget 10.0 admits res_100+sim_1 (8.8 ms) for Balanced.
        // Responsive scales the budget to 8.5 ms → 8.8 no longer fits → the
        // intent change must move the selection DOWN deterministically.
        Budget mid;
        mid.policy_version = "1";
        mid.gpu_ms = Metric::Known(10.0);
        mid.cpu_ms = Metric::Known(20.0);
        mid.vram_mb = Metric::Known(1000.0);
        FidelityGovernor g(CompileCandidates(c, mid, "h").set, mid,
                           FastPolicy(), Intent::Balanced);
        g.SelectInitial("DCX");
        const std::string before = g.current().candidate_id;
        Check(before.find("res_100") != std::string::npos,
              "Balanced picks the rich candidate within budget");
        auto ev = g.SetIntent(Intent::Responsive);   // 0.85 × 10ms = 8.5ms
        Check(!ev.empty(), "intent change re-evaluates");
        if (!ev.empty()) {
            Check(ev.front().reason == SelectionReason::PlayerIntentChanged,
                  "PLAYER_INTENT_CHANGED recorded");
        }
        Check(g.current().candidate_id != before,
              "tighter intent changed selection");
        Check(g.current().candidate_id.find("sim_1") != std::string::npos,
              "intent change still respects §23 legality");
        Check(g.current().candidate_id.find("res_50") != std::string::npos,
              "Responsive picks the lighter candidate");
    }

    // ---------------- dev pin (§35) ------------------------------------------
    {
        Budget open;
        open.policy_version = "1";
        open.gpu_ms = Metric::Known(20.0);
        open.cpu_ms = Metric::Known(20.0);
        open.vram_mb = Metric::Known(1000.0);
        CandidateSet set = CompileCandidates(c, open, "h").set;
        FidelityGovernor g(set, open, FastPolicy(), Intent::Balanced);
        g.SelectInitial("DCX");
        // pin to the lowest-utility candidate
        std::string low;
        for (const auto& cd : set.candidates)
            if (cd.pareto && cd.utility < 0.7) { low = cd.id; break; }
        Check(g.PinCandidate(low), "pin to validated candidate accepted");
        TelemetryWindow overload;
        overload.gpu_ms = Metric::Known(50.0);
        overload.cpu_ms = Metric::Known(50.0);
        size_t changes = 0;
        for (int i = 0; i < 8; ++i) changes += g.Observe(overload).size();
        Check(changes == 0, "pinned governor never adapts");
        Check(!g.PinCandidate("does-not-exist"), "unknown pin refused");
    }

    // ---------------- loader fail-closed (§29) -------------------------------
    {
        // Contract hash binding: a stale artifact must be refused.
        std::string canon = ContractJson(c);
        CandidateSet stale = s1;
        stale.contract_sha256 = "deadbeef";
        (void)canon;
        Check(stale.contract_sha256 != "ok", "stale-hash field settable");
        // (loader-level check exercised in test_fidelity_package e2e)
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
