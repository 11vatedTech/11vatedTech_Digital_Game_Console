// dc/fidelity.cpp — contract parse/serialize + shared vocabulary names.
// Deterministic, allocation-strict, rejection-coded (see fidelity.hpp).
#include "dc/fidelity.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace dc::fidelity {

// ---- ParseResult out-of-line members (unique_ptr<CandidateSet>) ------------
ParseResult::~ParseResult() = default;
ParseResult::ParseResult(ParseResult&&) noexcept = default;
ParseResult& ParseResult::operator=(ParseResult&&) noexcept = default;

const char* TransitionPolicyName(TransitionPolicy p) {
    switch (p) {
        case TransitionPolicy::InstantSafe: return "INSTANT_SAFE";
        case TransitionPolicy::HystereticRuntime: return "HYSTERETIC_RUNTIME";
        case TransitionPolicy::CameraCutOnly: return "CAMERA_CUT_ONLY";
        case TransitionPolicy::SceneBoundary: return "SCENE_BOUNDARY";
        case TransitionPolicy::ReloadRequired: return "RELOAD_REQUIRED";
        case TransitionPolicy::RestartRequired: return "RESTART_REQUIRED";
    }
    return "?";
}

bool ParseTransitionPolicy(const std::string& s, TransitionPolicy* out) {
    static const std::pair<const char*, TransitionPolicy> k[] = {
        {"INSTANT_SAFE", TransitionPolicy::InstantSafe},
        {"HYSTERETIC_RUNTIME", TransitionPolicy::HystereticRuntime},
        {"CAMERA_CUT_ONLY", TransitionPolicy::CameraCutOnly},
        {"SCENE_BOUNDARY", TransitionPolicy::SceneBoundary},
        {"RELOAD_REQUIRED", TransitionPolicy::ReloadRequired},
        {"RESTART_REQUIRED", TransitionPolicy::RestartRequired},
    };
    for (const auto& e : k)
        if (s == e.first) { *out = e.second; return true; }
    return false;
}

const char* CostClassName(CostClass c) {
    switch (c) {
        case CostClass::Declared: return "DECLARED";
        case CostClass::Measured: return "MEASURED";
        case CostClass::Certified: return "CERTIFIED";
    }
    return "?";
}

bool ParseCostClass(const std::string& s, CostClass* out) {
    if (s == "DECLARED") { *out = CostClass::Declared; return true; }
    if (s == "MEASURED") { *out = CostClass::Measured; return true; }
    if (s == "CERTIFIED") { *out = CostClass::Certified; return true; }
    return false;
}

const char* IntentName(Intent i) {   // canonical (lowercase) spelling
    switch (i) {
        case Intent::Automatic: return "automatic";
        case Intent::Responsive: return "responsive";
        case Intent::Balanced: return "balanced";
        case Intent::Cinematic: return "cinematic";
    }
    return "?";
}

bool ParseIntent(const std::string& s, Intent* out) {
    // Case-insensitive: the envelope carries "AUTOMATIC" (human-facing
    // vocabulary), the governor normalizes internally.
    auto eqi = [](const std::string& a, const char* b) {
        return a.size() == std::strlen(b) &&
               std::equal(a.begin(), a.end(), b, [](char x, char y) {
                   return std::tolower((unsigned char)x) ==
                          std::tolower((unsigned char)y);
               });
    };
    if (eqi(s, "automatic")) { *out = Intent::Automatic; return true; }
    if (eqi(s, "responsive")) { *out = Intent::Responsive; return true; }
    if (eqi(s, "balanced")) { *out = Intent::Balanced; return true; }
    if (eqi(s, "cinematic")) { *out = Intent::Cinematic; return true; }
    return false;
}

const char* SelectionReasonName(SelectionReason r) {
    switch (r) {
        case SelectionReason::InitialSelection: return "INITIAL_SELECTION";
        case SelectionReason::GpuPressure: return "GPU_PRESSURE";
        case SelectionReason::CpuPressure: return "CPU_PRESSURE";
        case SelectionReason::VramPressure: return "VRAM_PRESSURE";
        case SelectionReason::RamPressure: return "RAM_PRESSURE";
        case SelectionReason::IoPressure: return "IO_PRESSURE";
        case SelectionReason::ThermalPressure: return "THERMAL_PRESSURE";
        case SelectionReason::LatencyPressure: return "LATENCY_PRESSURE";
        case SelectionReason::Recovery: return "RECOVERY";
        case SelectionReason::PlayerIntentChanged: return "PLAYER_INTENT_CHANGED";
        case SelectionReason::SessionProfileChanged: return "SESSION_PROFILE_CHANGED";
    }
    return "?";
}

namespace {

ParseResult Fail(std::string code, std::string detail) {
    ParseResult r;
    r.ok = false;
    r.reason_code = std::move(code);
    r.detail = std::move(detail);
    return r;
}

// Optional-number helper: contract numbers may be absent (UNKNOWN), but a
// present number must be a finite non-negative value (directive §37:
// negative cost, NaN/inf are rejections).
Metric GetMetric(const json::Value& v, const char* key, bool* bad, const char** why) {
    Metric m;
    const json::Value* f = v.find(key);
    if (!f) return m;                       // absent → UNKNOWN
    if (!f->is_number() || !std::isfinite(f->as_number()) || f->as_number() < 0.0) {
        *bad = true;
        *why = key;
        return m;
    }
    m = Metric::Known(f->as_number());
    return m;
}

} // namespace

ParseResult ParseContract(const std::string& json_text) {
    std::string err;
    auto root = json::Parse(json_text, err);
    if (!root) return Fail("FIDELITY_PARSE_INVALID", err);
    if (!root->is_object()) return Fail("FIDELITY_SCHEMA_INVALID", "root is not an object");

    const json::Value* schema = root->find("schema");
    if (!schema || !schema->is_string() || schema->as_string() != kSchemaId)
        return Fail("FIDELITY_SCHEMA_INVALID",
                    schema ? "foreign schema '" + schema->as_string() + "'"
                           : "missing schema");
    const json::Value* title = root->find("title_id");
    if (!title || !title->is_string() || title->as_string().empty())
        return Fail("FIDELITY_SCHEMA_INVALID", "missing title_id");
    const json::Value* cver = root->find("contract_version");
    if (!cver || !cver->is_string() || cver->as_string().empty())
        return Fail("FIDELITY_SCHEMA_INVALID", "missing contract_version");

    const json::Value* domains = root->find("domains");
    if (!domains || !domains->is_array() || domains->as_array().empty())
        return Fail("FIDELITY_SCHEMA_INVALID", "domains must be a non-empty array");
    if (domains->as_array().size() > kMaxDomains)
        return Fail("FIDELITY_POLICY_LIMIT",
                    "domains " + std::to_string(domains->as_array().size()) + " > kMaxDomains");

    Contract c;
    c.schema = schema->as_string();
    c.title_id = title->as_string();
    c.contract_version = cver->as_string();

    std::map<std::string, int> domain_names;   // duplicate detection (§37)
    std::map<std::string, int> state_ids;      // unique across ALL domains
    int64_t combination_total = 1;

    for (size_t di = 0; di < domains->as_array().size(); ++di) {
        const json::Value& dv = domains->as_array()[di];
        if (!dv.is_object()) return Fail("FIDELITY_SCHEMA_INVALID", "domain not an object");
        const json::Value* dn = dv.find("domain");
        if (!dn || !dn->is_string() || dn->as_string().empty())
            return Fail("FIDELITY_SCHEMA_INVALID", "domain missing name");
        const json::Value* states = dv.find("states");
        if (!states || !states->is_array() || states->as_array().empty())
            return Fail("FIDELITY_SCHEMA_INVALID", "domain '" + dn->as_string() + "' states empty");
        if (states->as_array().size() > kMaxStatesPerDomain)
            return Fail("FIDELITY_POLICY_LIMIT",
                        "domain '" + dn->as_string() + "' states > kMaxStatesPerDomain");

        Domain d;
        d.name = dn->as_string();
        if (!domain_names.emplace(d.name, 1).second)
            return Fail("FIDELITY_DUPLICATE_DOMAIN", d.name);
        combination_total *= static_cast<int64_t>(states->as_array().size());

        const json::Value* tr = dv.find("transition");
        if (tr && tr->is_string() && !ParseTransitionPolicy(tr->as_string(), &d.transition))
            return Fail("FIDELITY_SCHEMA_INVALID",
                        "domain '" + d.name + "' unknown transition '" + tr->as_string() + "'");
        const json::Value* crit = dv.find("criticality");
        if (crit && crit->is_string()) d.criticality = crit->as_string();
        const json::Value* dyn = dv.find("dynamic");
        if (dyn && dyn->is_bool()) d.dynamic = dyn->as_bool();

        int prev_ordinal = -1;
        for (size_t si = 0; si < states->as_array().size(); ++si) {
            const json::Value& sv = states->as_array()[si];
            if (!sv.is_object()) return Fail("FIDELITY_SCHEMA_INVALID", "state not an object");
            const json::Value* sid = sv.find("id");
            if (!sid || !sid->is_string() || sid->as_string().empty())
                return Fail("FIDELITY_SCHEMA_INVALID", "state missing id");
            State st;
            st.id = sid->as_string();
            if (!state_ids.emplace(st.id, 1).second)
                return Fail("FIDELITY_DUPLICATE_STATE", st.id);

            const json::Value* ord = sv.find("ordinal");
            if (ord && ord->is_number()) {
                if (ord->as_number() != static_cast<double>(static_cast<int>(ord->as_number())))
                    return Fail("FIDELITY_SCHEMA_INVALID", "state '" + st.id + "' ordinal not integer");
                st.ordinal = static_cast<int>(ord->as_number());
                if (st.ordinal <= prev_ordinal)
                    return Fail("FIDELITY_SCHEMA_INVALID",
                                "domain '" + d.name + "' ordinals not strictly increasing");
                prev_ordinal = st.ordinal;
            } else {
                st.ordinal = static_cast<int>(si);
                prev_ordinal = st.ordinal;
            }

            const json::Value* util = sv.find("utility");
            if (!util || !util->is_number() || !std::isfinite(util->as_number()) ||
                util->as_number() < 0.0 || util->as_number() > 1.0)
                return Fail("FIDELITY_INVALID_UTILITY", st.id);
            st.utility = util->as_number();

            bool bad = false;
            const char* why = nullptr;
            st.gpu_ms = GetMetric(sv, "gpu_ms", &bad, &why);
            st.cpu_ms = GetMetric(sv, "cpu_ms", &bad, &why);
            st.vram_mb = GetMetric(sv, "vram_mb", &bad, &why);
            st.io_mbps = GetMetric(sv, "io_mbps", &bad, &why);
            if (bad) return Fail("FIDELITY_INVALID_COST",
                                 "state '" + st.id + "' field '" + why + "'");

            const json::Value* cc = sv.find("cost_class");
            if (cc && cc->is_string() && !ParseCostClass(cc->as_string(), &st.cost_class))
                return Fail("FIDELITY_SCHEMA_INVALID", "state '" + st.id + "' cost_class");
            d.states.push_back(std::move(st));
        }
        c.domains.push_back(std::move(d));
    }

    if (combination_total > static_cast<int64_t>(kMaxCombinations))
        return Fail("FIDELITY_POLICY_LIMIT",
                    "combination count " + std::to_string(combination_total) +
                    " > kMaxCombinations");

    ParseResult ok;
    ok.ok = true;
    ok.contract = std::move(c);
    return ok;
}

// ---- deterministic canonical JSON ------------------------------------------
// json.hpp writes sorted-key compact objects (std::map), so identical
// contracts produce identical bytes (§38 determinism).

namespace {

json::Value MetricValue(const Metric& m) {
    return m.known ? json::Value(m.value) : json::Value();
}

json::Object StateObject(const State& st) {
    json::Object o;
    o["id"] = json::Value(st.id);
    o["ordinal"] = json::Value(static_cast<double>(st.ordinal));
    o["utility"] = json::Value(st.utility);
    if (st.gpu_ms.known) o["gpu_ms"] = MetricValue(st.gpu_ms);
    if (st.cpu_ms.known) o["cpu_ms"] = MetricValue(st.cpu_ms);
    if (st.vram_mb.known) o["vram_mb"] = MetricValue(st.vram_mb);
    if (st.io_mbps.known) o["io_mbps"] = MetricValue(st.io_mbps);
    o["cost_class"] = json::Value(CostClassName(st.cost_class));
    return o;
}

} // namespace

std::string ContractJson(const Contract& c) {
    json::Array domains;
    for (const auto& d : c.domains) {
        json::Array states;
        for (const auto& st : d.states) states.emplace_back(StateObject(st));
        json::Object dv;
        dv["domain"] = json::Value(d.name);
        dv["transition"] = json::Value(TransitionPolicyName(d.transition));
        dv["criticality"] = json::Value(d.criticality);
        dv["dynamic"] = json::Value(d.dynamic);
        dv["states"] = json::Value(std::move(states));
        domains.emplace_back(json::Value(std::move(dv)));
    }
    json::Object root;
    root["schema"] = json::Value(c.schema);
    root["title_id"] = json::Value(c.title_id);
    root["contract_version"] = json::Value(c.contract_version);
    root["domains"] = json::Value(std::move(domains));
    json::Value v(std::move(root));
    std::string out;
    json::WriteCompact(v, out);
    return out;
}

namespace {

json::Object CandidateObject(const Candidate& cd) {
    json::Object o;
    o["id"] = json::Value(cd.id);
    json::Array st;
    for (const auto& s : cd.states)
        st.emplace_back(json::Value(json::Object{
            {"domain", json::Value(s.first)}, {"state", json::Value(s.second)}}));
    o["states"] = json::Value(std::move(st));
    o["utility"] = json::Value(cd.utility);
    if (cd.gpu_ms.known) o["gpu_ms"] = json::Value(cd.gpu_ms.value);
    if (cd.cpu_ms.known) o["cpu_ms"] = json::Value(cd.cpu_ms.value);
    if (cd.vram_mb.known) o["vram_mb"] = json::Value(cd.vram_mb.value);
    if (cd.io_mbps.known) o["io_mbps"] = json::Value(cd.io_mbps.value);
    o["pareto"] = json::Value(cd.pareto);
    o["within_budget"] = json::Value(cd.within_budget);
    if (!cd.exceeded.empty()) {
        json::Array ex;
        for (const auto& e : cd.exceeded) ex.emplace_back(json::Value(e));
        o["exceeded"] = json::Value(std::move(ex));
    }
    return o;
}

} // namespace

std::string CandidateSetJson(const CandidateSet& s) {
    json::Object root;
    root["schema"] = json::Value(s.schema);
    root["title_id"] = json::Value(s.title_id);
    root["contract_version"] = json::Value(s.contract_version);
    root["compiler_version"] = json::Value(s.compiler_version);
    root["policy_version"] = json::Value(s.policy_version);
    root["host_profile"] = json::Value(s.host_profile);
    root["contract_sha256"] = json::Value(s.contract_sha256);
    json::Array doms;
    for (const auto& d : s.domains) {
        json::Object o;
        o["name"] = json::Value(d.name);
        o["transition"] = json::Value(std::string(TransitionPolicyName(d.transition)));
        o["dynamic"] = json::Value(d.dynamic);
        doms.emplace_back(json::Value(std::move(o)));
    }
    root["domains"] = json::Value(std::move(doms));
    json::Array cands;
    for (const auto& cd : s.candidates) cands.emplace_back(CandidateObject(cd));
    root["candidates"] = json::Value(std::move(cands));
    json::Value v(std::move(root));
    std::string out;
    json::WriteCompact(v, out);
    return out;
}

// Runtime-adaptive legality (§23). Only launch-time-legal domains may be
// freely selected; dynamic adaptation is restricted to INSTANT_SAFE and
// HYSTERETIC_RUNTIME domains that are marked dynamic.
bool DynamicLegal(const DomainPolicy& d) {
    return d.dynamic && (d.transition == TransitionPolicy::InstantSafe ||
                         d.transition == TransitionPolicy::HystereticRuntime);
}

ParseResult ParseCandidateSet(const std::string& json_text) {
    std::string err;
    auto root = json::Parse(json_text, err);
    if (!root) return Fail("FIDELITY_PARSE_INVALID", err);
    if (!root->is_object()) return Fail("FIDELITY_SCHEMA_INVALID", "root not object");
    const json::Value* schema = root->find("schema");
    if (!schema || !schema->is_string() ||
        schema->as_string() != "dc.fidelity-candidates/1")
        return Fail("FIDELITY_SCHEMA_INVALID", "foreign candidate-set schema");

    CandidateSet s;
    auto get_str = [&](const char* k, std::string* dst) -> bool {
        const json::Value* v = root->find(k);
        if (!v || !v->is_string()) return false;
        *dst = v->as_string();
        return true;
    };
    const json::Value* doms = root->find("domains");
    if (!doms || !doms->is_array() || doms->as_array().empty())
        return Fail("FIDELITY_SCHEMA_INVALID", "candidate-set missing domain policies");
    for (const auto& dv : doms->as_array()) {
        const json::Value* nm = dv.find("name");
        if (!nm || !nm->is_string())
            return Fail("FIDELITY_SCHEMA_INVALID", "domain policy name");
        DomainPolicy dp;
        dp.name = nm->as_string();
        const json::Value* tr = dv.find("transition");
        if (tr && tr->is_string()) {
            if (!ParseTransitionPolicy(tr->as_string(), &dp.transition))
                return Fail("FIDELITY_SCHEMA_INVALID",
                            "domain policy unknown transition '" + tr->as_string() + "'");
        }
        const json::Value* dyn = dv.find("dynamic");
        if (dyn && dyn->is_bool()) dp.dynamic = dyn->as_bool();
        s.domains.push_back(std::move(dp));
    }

    if (!get_str("title_id", &s.title_id) ||
        !get_str("contract_version", &s.contract_version) ||
        !get_str("compiler_version", &s.compiler_version) ||
        !get_str("policy_version", &s.policy_version) ||
        !get_str("host_profile", &s.host_profile) ||
        !get_str("contract_sha256", &s.contract_sha256))
        return Fail("FIDELITY_SCHEMA_INVALID", "candidate-set missing identity fields");

    const json::Value* cands = root->find("candidates");
    if (!cands || !cands->is_array() || cands->as_array().empty())
        return Fail("FIDELITY_SCHEMA_INVALID", "candidates empty");

    for (const auto& cv : cands->as_array()) {
        if (!cv.is_object()) return Fail("FIDELITY_SCHEMA_INVALID", "candidate not object");
        Candidate cd;
        const json::Value* id = cv.find("id");
        if (!id || !id->is_string()) return Fail("FIDELITY_SCHEMA_INVALID", "candidate id");
        cd.id = id->as_string();
        const json::Value* st = cv.find("states");
        if (!st || !st->is_array()) return Fail("FIDELITY_SCHEMA_INVALID", "candidate states");
        for (const auto& sv : st->as_array()) {
            const json::Value* dm = sv.find("domain");
            const json::Value* sd = sv.find("state");
            if (!dm || !dm->is_string() || !sd || !sd->is_string())
                return Fail("FIDELITY_SCHEMA_INVALID", "candidate state pair");
            cd.states.emplace_back(dm->as_string(), sd->as_string());
        }
        const json::Value* util = cv.find("utility");
        if (!util || !util->is_number()) return Fail("FIDELITY_SCHEMA_INVALID", "candidate utility");
        cd.utility = util->as_number();
        auto metric = [&](const char* k, Metric* m) {
            const json::Value* v = cv.find(k);
            if (v && v->is_number()) *m = Metric::Known(v->as_number());
        };
        metric("gpu_ms", &cd.gpu_ms);
        metric("cpu_ms", &cd.cpu_ms);
        metric("vram_mb", &cd.vram_mb);
        metric("io_mbps", &cd.io_mbps);
        const json::Value* par = cv.find("pareto");
        if (par && par->is_bool()) cd.pareto = par->as_bool();
        const json::Value* wb = cv.find("within_budget");
        if (wb && wb->is_bool()) cd.within_budget = wb->as_bool();
        const json::Value* ex = cv.find("exceeded");
        if (ex && ex->is_array())
            for (const auto& e : ex->as_array())
                if (e.is_string()) cd.exceeded.push_back(e.as_string());
        s.candidates.push_back(std::move(cd));
    }

    ParseResult ok;
    ok.ok = true;
    ok.candidate_set = std::make_unique<CandidateSet>(std::move(s));
    return ok;
}

} // namespace dc::fidelity
