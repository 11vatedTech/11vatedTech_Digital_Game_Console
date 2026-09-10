// qualification_json.cpp — deterministic JSON serialization of benchmark
// evidence (ADR-0018/0021). Reuses the Writer from capability_json.cpp
// (internal linkage there; small local copy here to keep files independent).
#include "dc/qualification.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace dc {

namespace {

void EscapeInto(const std::string& in, std::string& out) {
    for (unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
}

std::string Esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    EscapeInto(s, out);
    out += '"';
    return out;
}

std::string Num(double v) {
    if (std::isfinite(v)) {
        if (v == static_cast<long long>(v) && std::fabs(v) < 1e15) {
            return std::to_string(static_cast<long long>(v));
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6f", v);
        std::string s(buf);
        if (s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.pop_back();
        }
        return s;
    }
    return "0"; // non-finite never serializes (C10; NaN/Inf rejected upstream)
}

const char* StateName(BenchState s) {
    switch (s) {
        case BenchState::Complete: return "complete";
        case BenchState::Aborted: return "aborted";
        case BenchState::Unavailable: return "unavailable";
        default: return "not-run";
    }
}

} // namespace

std::string QualificationToJson(const QualificationSet& q, bool pretty) {
    std::string out = "{";
    auto nl = [&](int depth) {
        if (pretty) {
            out += '\n';
            out.append(static_cast<size_t>(depth) * 2, ' ');
        }
    };
    nl(1);
    out += "\"schema\": \"dc.qualification/1\",";
    nl(1);
    out += "\"mode\": " + Esc(q.mode) + ",";
    nl(1);
    out += "\"measured\": " + std::string(q.measured ? "true" : "false") + ",";
    nl(1);
    out += "\"sustained_valid\": " + std::string(q.sustained_valid ? "true" : "false") + ",";
    nl(1);
    out += "\"timestamp_utc\": " + Esc(q.timestamp_utc) + ",";
    nl(1);
    out += "\"benchmarks\": [";
    bool first = true;
    for (const auto& b : q.benchmarks) {
        if (!first) out += ",";
        first = false;
        nl(2);
        out += "{";
        nl(3);
        out += "\"benchmark_id\": " + Esc(b.benchmark_id) + ",";
        nl(3);
        out += "\"benchmark_version\": " + std::to_string(b.benchmark_version) + ",";
        nl(3);
        out += "\"adapter_id\": " + Esc(b.adapter_id) + ",";
        nl(3);
        out += "\"driver_version\": " + Esc(b.driver_version) + ",";
        nl(3);
        out += "\"iterations\": " + std::to_string(b.iterations) + ",";
        nl(3);
        out += "\"warmup_iterations\": " + std::to_string(b.warmup_iterations) + ",";
        nl(3);
        out += "\"state\": " + Esc(StateName(b.state)) + ",";
        nl(3);
        out += "\"abort_reason\": " + Esc(b.abort_reason) + ",";
        nl(3);
        out += "\"elapsed_gpu_time_ms\": " + Num(b.elapsed_gpu_time_ms) + ",";
        nl(3);
        out += "\"elapsed_cpu_time_ms\": " + Num(b.elapsed_cpu_time_ms) + ",";
        nl(3);
        out += "\"score\": " + Num(b.score) + ",";
        nl(3);
        out += "\"variance\": " + Num(b.variance) + ",";
        nl(3);
        out += "\"score_ratio\": " + Num(b.score_ratio) + ",";
        nl(3);
        out += "\"methodology_id\": " + Esc(b.methodology_id) + ",";
        nl(3);
        out += "\"runtime_version\": " + Esc(b.runtime_version) + ",";
        nl(3);
        out += "\"timestamp_utc\": " + Esc(b.timestamp_utc) + ",";
        nl(3);
        out += "\"samples\": [";
        bool fs = true;
        for (const auto& s : b.samples) {
            if (!fs) out += ",";
            fs = false;
            out += "{\"gpu_time_ms\": " + Num(s.gpu_time_ms) +
                   ", \"cpu_time_ms\": " + Num(s.cpu_time_ms) + "}";
        }
        out += "]";
        nl(2);
        out += "}";
    }
    nl(1);
    out += "]";
    out += "\n}\n";
    return out;
}

} // namespace dc
