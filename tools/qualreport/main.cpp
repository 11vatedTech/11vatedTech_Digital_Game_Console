// dc-qualify-report — conservative qualification aggregator (ADR-0023).
// Consumes immutable run directories under evidence/qualification/runs/ and
// computes conservative per-domain statistics. Deterministic: identical run
// sets produce byte-identical reports. No LLM judgment, no cherry-picking.
//
// Policy (dc.policy/1, v1 — development stage):
//   DEVELOPMENT PROFILE : >=1 valid run with the domain COMPLETE.
//   CERTIFIED PROFILE   : >=3 valid certification-mode runs, all COMPLETE for
//                         the domain, conservative aggregate (median) at or
//                         above threshold. N=3 is the smallest set that
//                         distinguishes a persistent capability from a lucky
//                         run while remaining practical for pre-silicon-style
//                         dev-kit governance; revisit with data (policy is
//                         versioned, not baked into derivation).
// Aborted runs are never scored (C10); they are counted for transparency.
#include "dc/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

bool ReadFileText(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    out = std::move(s);
    return true;
}

struct RunSummary {
    std::string run_id;
    std::string mode;
    std::string started_at;
    bool sustained_valid = false;
    // benchmark_id -> {state, score}
    std::map<std::string, std::pair<std::string, double>> benches;
};

// Minimal JSON helpers on top of dc::json (values are objects/arrays only).
const dc::json::Value* Find(const dc::json::Object& o, const std::string& k) {
    auto it = o.find(k);
    return it == o.end() ? nullptr : &it->second;
}
std::string Str(const dc::json::Value& v, const std::string& def = "") {
    return v.is_string() ? v.as_string() : def;
}
double Num(const dc::json::Value& v, double def = 0.0) {
    return v.is_number() ? v.as_number() : def;
}

bool ParseRun(const fs::path& dir, RunSummary& run) {
    std::string manifest;
    if (!ReadFileText(dir / "manifest.json", manifest)) return false;
    std::string jerr;
    auto m = dc::json::Parse(manifest, jerr);
    if (!m) return false;
    run.run_id = dir.filename().string();
    if (const auto* v = Find(m->as_object(), "mode")) run.mode = Str(*v);
    if (const auto* v = Find(m->as_object(), "started_at_utc")) run.started_at = Str(*v);
    if (const auto* v = Find(m->as_object(), "sustained_valid")) run.sustained_valid = v->is_bool() && v->as_bool();

    std::string benches;
    if (!ReadFileText(dir / "benchmarks.json", benches)) return false;
    auto b = dc::json::Parse(benches, jerr);
    if (!b) return false;
    // benchmarks.json is {"benchmarks": [...]} (QualificationSet) or a bare
    // array (older shape). Accept both.
    const dc::json::Array* arr_ptr = nullptr;
    if (b->is_object()) {
        if (const auto* inner = Find(b->as_object(), "benchmarks")) {
            if (inner->is_array()) arr_ptr = &inner->as_array();
        }
    } else if (b->is_array()) {
        arr_ptr = &b->as_array();
    }
    if (!arr_ptr) return false;
    const auto& arr = *arr_ptr;
    for (const auto& item : arr) {
        const auto& o = item.as_object();
        std::string id = Find(o, "benchmark_id") ? Str(*Find(o, "benchmark_id")) : "";
        std::string state = Find(o, "state") ? Str(*Find(o, "state")) : "";
        double score = Find(o, "score") ? Num(*Find(o, "score")) : 0.0;
        if (!id.empty()) run.benches[id] = {state, score};
    }
    return !run.benches.empty();
}

struct DomainStats {
    double median = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double p10 = 0.0;
    double p90 = 0.0;
    double cv = 0.0;            // coefficient of variation (0 if <2 valid)
    int valid = 0;              // complete runs
    int aborted = 0;
    int unavailable = 0;
};

double MedianOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return (n % 2 == 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

DomainStats Aggregate(const std::vector<RunSummary>& runs, const std::string& bench_id) {
    DomainStats st;
    std::vector<double> scores;
    for (const auto& r : runs) {
        auto it = r.benches.find(bench_id);
        if (it == r.benches.end()) continue;
        const auto& [state, score] = it->second;
        if (state == "complete" && score > 0.0) {
            scores.push_back(score);
            ++st.valid;
        } else if (state == "aborted") {
            ++st.aborted;
        } else {
            ++st.unavailable;
        }
    }
    if (scores.empty()) return st;
    st.median = MedianOf(scores);
    st.minimum = *std::min_element(scores.begin(), scores.end());
    st.maximum = *std::max_element(scores.begin(), scores.end());
    auto pct = [&](double p) {
        std::vector<double> v = scores;
        std::sort(v.begin(), v.end());
        size_t n = v.size();
        double idx = p * static_cast<double>(n - 1);
        size_t lo = static_cast<size_t>(std::floor(idx));
        size_t hi = static_cast<size_t>(std::ceil(idx));
        return (lo == hi) ? v[lo] : v[lo] + (v[hi] - v[lo]) * (idx - static_cast<double>(lo));
    };
    st.p10 = pct(0.10);
    st.p90 = pct(0.90);
    if (scores.size() >= 2) {
        double mean = 0.0;
        for (double s : scores) mean += s;
        mean /= static_cast<double>(scores.size());
        double var = 0.0;
        for (double s : scores) var += (s - mean) * (s - mean);
        var /= static_cast<double>(scores.size() - 1);
        st.cv = (mean > 0.0) ? std::sqrt(var) / mean : 0.0;
    }
    return st;
}

const char* DomainUnits(const std::string& id) {
    if (id.rfind("cpu.", 0) == 0) return "ops/s";
    if (id.rfind("gpu.raster", 0) == 0) return "px/ms";
    if (id == "gpu.compute") return "MACs/ms";
    if (id == "gpu.copy") return "GB/s";
    if (id.rfind("gpu.rt", 0) == 0) return "rays/ms";
    if (id == "memory.bandwidth") return "GB/s";
    if (id == "memory.pressure") return "bytes";
    if (id.rfind("storage.", 0) == 0) return "MB/s";
    return "";
}

} // namespace

int main(int argc, char** argv) {
    fs::path runs_dir = "evidence/qualification/runs";
    if (argc > 1) runs_dir = argv[1];

    if (!fs::exists(runs_dir)) {
        std::fprintf(stderr, "no runs directory at %s\n", runs_dir.string().c_str());
        return 1;
    }

    std::vector<RunSummary> runs;
    for (const auto& e : fs::directory_iterator(runs_dir)) {
        if (!e.is_directory()) continue;
        RunSummary r;
        if (ParseRun(e.path(), r)) runs.push_back(r);
    }
    if (runs.empty()) {
        std::fprintf(stderr, "no parseable runs\n");
        return 1;
    }
    // Deterministic order by run_id.
    std::sort(runs.begin(), runs.end(), [](const RunSummary& a, const RunSummary& b) {
        return a.run_id < b.run_id;
    });

    // Collect the union of benchmark ids.
    std::vector<std::string> ids;
    for (const auto& r : runs) {
        for (const auto& [id, _] : r.benches) {
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());

    // JSON report.
    std::string out = "{\n  \"schema\": \"dc.qualification-aggregate/1\",\n";
    out += "  \"policy\": \"dc.policy/1\",\n";
    out += "  \"runs_considered\": " + std::to_string(runs.size()) + ",\n";
    out += "  \"runs\": [\n";
    for (size_t i = 0; i < runs.size(); ++i) {
        out += "    {\"run_id\": \"" + runs[i].run_id + "\", \"mode\": \"" + runs[i].mode +
               "\", \"started_at\": \"" + runs[i].started_at + "\", \"sustained_valid\": " +
               (runs[i].sustained_valid ? "true" : "false") + "}";
        out += (i + 1 < runs.size()) ? ",\n" : "\n";
    }
    out += "  ],\n  \"domains\": {\n";
    for (size_t d = 0; d < ids.size(); ++d) {
        const auto& id = ids[d];
        DomainStats st = Aggregate(runs, id);
        out += "    \"" + id + "\": {";
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "\"unit\": \"%s\", \"median\": %.4f, \"min\": %.4f, \"max\": %.4f, "
                      "\"p10\": %.4f, \"p90\": %.4f, \"cv\": %.4f, \"valid\": %d, "
                      "\"aborted\": %d, \"unavailable\": %d}",
                      DomainUnits(id), st.median, st.minimum, st.maximum,
                      st.p10, st.p90, st.cv, st.valid, st.aborted, st.unavailable);
        out += buf;
        out += (d + 1 < ids.size()) ? ",\n" : "\n";
    }
    out += "  }\n}\n";

    const char* out_path = (argc > 2) ? argv[2] : "evidence/qualification/aggregate.json";
    FILE* f = std::fopen(out_path, "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", out_path);
        return 1;
    }
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);

    // Human summary to stdout.
    std::printf("Qualification aggregate over %zu run(s):\n", runs.size());
    for (const auto& id : ids) {
        DomainStats st = Aggregate(runs, id);
        if (st.valid == 0 && st.aborted == 0 && st.unavailable == 0) continue;
        std::printf("  %-28s median=%12.2f  min=%12.2f  max=%12.2f  cv=%5.2f  valid=%d aborted=%d unavail=%d\n",
                    id.c_str(), st.median, st.minimum, st.maximum, st.cv, st.valid, st.aborted, st.unavailable);
    }
    std::printf("wrote %s\n", out_path);
    return 0;
}
