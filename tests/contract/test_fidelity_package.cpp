// test_fidelity_package.cpp — DK0-M4 §28/§29/§30 package-integration tests.
//
// Proves the runtime fidelity boundary against an INSTALLED package view
// (never the source tree):
//   - LoadFromPackageView succeeds on a proper generation view
//   - missing files fail closed with structured reasons
//   - a stale artifact (contract edited after compile) is REJECTED via the
//     canonical contract SHA-256 binding (§29)
//   - a foreign-schema artifact is rejected
//   - FidelityService::Create works end-to-end from a gen view with REAL
//     host evidence, applies the intent, and refuses unknown intents
#include "dc/fidelity_loader.hpp"
#include "dc/fidelity_service.hpp"
#include "dc/hash.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

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

bool ReadFile(const std::string& p, std::string* out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out->assign(std::istreambuf_iterator<char>(f),
                std::istreambuf_iterator<char>());
    return true;
}

void WriteFile(const std::string& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
}

const char* kContract = R"JSON({
  "schema": "dc.fidelity/1",
  "title_id": "t.pkg",
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
        { "id": "sim_0", "ordinal": 0, "utility": 0.1, "gpu_ms": 0.3, "cpu_ms": 1.0 },
        { "id": "sim_1", "ordinal": 1, "utility": 0.5, "gpu_ms": 0.8, "cpu_ms": 2.0 } ] }
  ]
})JSON";

const char* kHost = R"JSON({ "gpu": { "vram_bytes": 4294967296 } })JSON";

// Compile the fixture contract into a candidate artifact using the core
// compiler (same path the tool uses).
std::string CompileArtifact(const std::string& contract_text,
                            const std::string& host_text) {
    ParseResult pr = ParseContract(contract_text);
    if (!pr.ok) return {};
    BudgetPolicy pol;
    Budget b = BudgetFromHostEvidence(host_text, pol, 60.0);
    CompileResult cr = CompileCandidates(pr.contract, b, "test-host");
    if (!cr.ok) return {};
    cr.set.contract_sha256 =
        dc::hash::Sha256::Hex(ContractJson(pr.contract));
    return CandidateSetJson(cr.set);
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "dc_fidelity_pkg_test";
    fs::remove_all(root);
    fs::create_directories(root / "gen" / "manifests");

    const std::string artifact = CompileArtifact(kContract, kHost);
    Check(!artifact.empty(), "fixture compiles");

    // ---------------- proper gen view loads ---------------------------------
    WriteFile((root / "gen/manifests/fidelity.json").string(), kContract);
    WriteFile((root / "gen/manifests/fidelity.candidates.json").string(),
              artifact);
    {
        LoadedFidelity lf = LoadFromPackageView((root / "gen").string());
        Check(lf.ok, "proper gen view loads");
        if (lf.ok) {
            PackageFidelitySummary s = Summarize(lf);
            Check(s.domains == 2, "summary domain count");
            Check(s.pareto_candidates > 0, "summary pareto count");
            Check(s.compiler_version == std::string(kCompilerVersion),
                  "artifact compiler version");
        }
    }

    // ---------------- missing files fail closed -----------------------------
    {
        LoadedFidelity lf = LoadFromPackageView((root / "empty").string());
        Check(!lf.ok, "missing view fails");
        Check(lf.reason_code == "FIDELITY_CONTRACT_MISSING",
              "structured reason for missing contract");
    }
    {
        fs::create_directories(root / "no_cand/manifests");
        WriteFile((root / "no_cand/manifests/fidelity.json").string(),
                  kContract);
        LoadedFidelity lf = LoadFromPackageView((root / "no_cand").string());
        Check(!lf.ok, "missing artifact fails");
        Check(lf.reason_code == "FIDELITY_CANDIDATES_MISSING",
              "structured reason for missing artifact");
    }

    // ---------------- stale artifact rejected (§29) -------------------------
    {
        // Edit the contract materially AFTER compile: the artifact's bound
        // canonical hash no longer matches → rejected.
        std::string edited = kContract;
        edited.replace(edited.find("\"contract_version\": \"1\""),
                       std::string("\"contract_version\": \"1\"").size(),
                       "\"contract_version\": \"2\"");
        fs::create_directories(root / "stale/manifests");
        WriteFile((root / "stale/manifests/fidelity.json").string(), edited);
        WriteFile((root / "stale/manifests/fidelity.candidates.json").string(),
                  artifact);
        LoadedFidelity lf = LoadFromPackageView((root / "stale").string());
        Check(!lf.ok, "stale artifact rejected");
        Check(lf.reason_code == "FIDELITY_VERSION_MISMATCH",
              "stale artifact reason is version mismatch");
    }

    // ---------------- foreign artifact schema rejected ----------------------
    {
        fs::create_directories(root / "foreign/manifests");
        WriteFile((root / "foreign/manifests/fidelity.json").string(),
                  kContract);
        WriteFile((root / "foreign/manifests/fidelity.candidates.json").string(),
                  R"JSON({"schema":"someone-elses.candidates/9","candidates":[]})JSON");
        LoadedFidelity lf = LoadFromPackageView((root / "foreign").string());
        Check(!lf.ok, "foreign artifact rejected");
        Check(lf.reason_code == "FIDELITY_CANDIDATES_INVALID",
              "foreign artifact reason");
    }

    // ---------------- service end-to-end from the gen view ------------------
    {
        ServiceConfig cfg;
        cfg.gen_view_dir = (root / "gen").string();
        cfg.host_capability_json = kHost;           // REAL evidence bytes
        cfg.intent = "CINEMATIC";
        cfg.session_profile = "DCX-UHD60";
        std::string reason, detail;
        auto svc = FidelityService::Create(cfg, &reason, &detail);
        Check(static_cast<bool>(svc), "service constructs from package view");
        if (svc) {
            Check(svc->SelectInitial(), "initial selection succeeds");
            const Selection& sel = svc->CurrentSelection();
            Check(sel.session_profile == "DCX-UHD60", "session profile recorded");
            Check(sel.intent == Intent::Cinematic, "intent recorded");
            // The launch-time selection MAY include sim_1 (RESTART_REQUIRED is
            // legal at launch, §23).
            Check(svc->StateFor("simulation_quality") == "sim_1" ||
                      svc->StateFor("simulation_quality") == "sim_0",
                  "sim state present at launch");
            // Measured observation (§10): feed windows; UNKNOWN stays UNKNOWN.
            TelemetryWindow w;
            w.gpu_ms = Metric::Known(1.0);
            w.cpu_ms = Metric::Known(1.0);
            for (int i = 0; i < 3; ++i) svc->Observe(w);
            Check(svc->Trace().size() >= 1, "decision trace exists");
            // Intent round-trip + unknown-intent rejection (§37).
            Check(svc->SetIntent("responsive"), "known intent accepted");
            Check(!svc->SetIntent("ULTRA"), "unknown intent refused");
        }
    }
    {
        ServiceConfig cfg;
        cfg.gen_view_dir = (root / "gen").string();
        cfg.host_capability_json = kHost;
        cfg.intent = "ULTRA";   // not a §16 intent
        std::string reason, detail;
        auto svc = FidelityService::Create(cfg, &reason, &detail);
        Check(!svc, "unknown intent fails construction");
        Check(reason == "FIDELITY_INTENT_INVALID", "structured intent reason");
    }
    {
        // Budget policy is honored through the service boundary (§18).
        // NOTE: the degenerate axis must be one whose candidate costs are
        // KNOWN (gpu here). A degenerate UNKNOWN-cost axis cannot exclude
        // anything — §17 forbids accusing unknown of exceeding (the axis is
        // unconstrained-but-unproven instead).
        ServiceConfig cfg;
        cfg.gen_view_dir = (root / "gen").string();
        cfg.host_capability_json = kHost;
        cfg.intent = "AUTOMATIC";
        cfg.budget_policy.gpu_headroom = 0.0;   // degenerate: gpu budget 0 ms
        std::string reason, detail;
        auto svc = FidelityService::Create(cfg, &reason, &detail);
        Check(static_cast<bool>(svc), "service with degenerate policy builds");
        if (svc) {
            // Every candidate costs > 0 ms gpu → NO valid candidate.
            Check(!svc->SelectInitial(),
                  "degenerate budget yields no selection (fail closed)");
        }
    }

    fs::remove_all(root);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
