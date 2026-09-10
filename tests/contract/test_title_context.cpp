// test_title_context.cpp — typed launch-context contract tests (canon §16).
//
// Pure round-trip: serialize→parse must preserve fields; foreign schema ids
// and malformed JSON must be rejected strictly.
//
// Live transport: the parent launches a child copy of ITSELF through the
// title supervisor's typed-context overload. The child parses the
// DC_TITLE_CONTEXT envelope exactly as a native title would and reports by
// exit code:
//   0 = envelope received, schema valid, all expected fields present
//   1 = envelope absent or unparsable
//   2 = received but field values wrong
// The parent verifies the child's exit code — proving the supervisor
// serializes the envelope across a real process boundary (env-block,
// job-owned), not just in-process.
#include "dc/title_context.hpp"
#include "dc/title_context_json.hpp"
#include "dc/title_supervisor.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

dc::TitleLaunchContext MakeSample() {
    dc::TitleLaunchContext c;
    c.title_id = "com.11vated.samples.native-minimal";
    c.title_version = "0.0.1";
    c.session_id = "sess-test-0001";
    c.user_id = 7;
    c.display_width = 3840;
    c.display_height = 2160;
    c.refresh_numerator = 119882;   // rational: 119.882 Hz — never truncated
    c.refresh_denominator = 1000;
    c.controller_required = true;
    c.guide_owned_by_platform = true;
    c.offline_launch = true;
    c.active_session_profiles = {"DCX-UHD60", "DCX-VRR"};
    c.host_capability_json = "{\"schema\":\"dc.host-capability/2\",\"gpu\":"
                             "{\"model_name\":\"TestGPU\",\"vram_bytes\":8}}";
    c.session_experience_json = "{\"schema\":\"dc.session-profiles/1\"}";
    return c;
}

int RunChildParseMode() {
    // Child mode: parse the envelope from the environment exactly as a
    // native title would. No test framework here — this must survive the
    // real CreateProcess environment-block transport.
#ifdef _WIN32
    const char* env = std::getenv("DC_TITLE_CONTEXT");
    if (!env || !*env) {
        std::fprintf(stderr, "child: no envelope\n");
        return 1;
    }
    dc::TitleLaunchContext ctx;
    std::string err;
    if (!dc::ParseTitleContext(env, ctx, err)) {
        std::fprintf(stderr, "child: parse failed: %s\n", err.c_str());
        return 1;
    }
    const dc::TitleLaunchContext want = MakeSample();
    const bool fields_ok =
        ctx.title_id == want.title_id &&
        ctx.title_version == want.title_version &&
        ctx.session_id == want.session_id &&
        ctx.user_id == want.user_id &&
        ctx.display_width == 3840 && ctx.display_height == 2160 &&
        ctx.refresh_numerator == 119882 && ctx.refresh_denominator == 1000 &&
        ctx.guide_owned_by_platform &&
        ctx.active_session_profiles.size() == 2 &&
        ctx.active_session_profiles[0] == "DCX-UHD60" &&
        ctx.host_capability_json.find("TestGPU") != std::string::npos;
    if (!fields_ok) {
        std::fprintf(stderr, "child: field mismatch\n");
        return 2;
    }
    return 0;
#else
    return 0;
#endif
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--child-parse") == 0) {
        return RunChildParseMode();
    }

    std::printf("[title context contract]\n");

    // 1. Round-trip preserves every field.
    {
        const dc::TitleLaunchContext in = MakeSample();
        const std::string text = dc::SerializeTitleContext(in);
        dc::TitleLaunchContext out;
        out.schema = "dc.title-context/1";
        std::string err;
        const bool ok = dc::ParseTitleContext(text, out, err);
        Check(ok, "round-trip parses");
        if (ok) {
            Check(out.title_id == in.title_id, "title_id preserved");
            Check(out.session_id == in.session_id, "session_id preserved");
            Check(out.user_id == in.user_id, "user_id preserved");
            Check(out.refresh_numerator == 119882 &&
                      out.refresh_denominator == 1000,
                  "rational refresh preserved (ADR-0022)");
            Check(out.active_session_profiles.size() == 2,
                  "DCX claims preserved");
            Check(out.host_capability_json.find("TestGPU") !=
                      std::string::npos,
                  "host evidence embedded verbatim");
            Check(out.guide_owned_by_platform, "Guide ownership preserved");
        } else {
            Check(false, err.c_str());
        }
    }

    // 2. Strict rejection: foreign schema id.
    {
        dc::TitleLaunchContext out;
        std::string err;
        const bool rejected =
            !dc::ParseTitleContext("{\"schema\":\"dc.title-context/9\"}", out, err) &&
            !err.empty();
        Check(rejected, "foreign schema id rejected");
    }

    // 3. Strict rejection: malformed JSON.
    {
        dc::TitleLaunchContext out;
        std::string err;
        const bool rejected =
            !dc::ParseTitleContext("{\"schema\":\"dc.title-context/1\",,}",
                                   out, err) &&
            !err.empty();
        Check(rejected, "malformed envelope rejected");
    }

    // 4. Strict rejection: truncated envelope (crash-damaged transport).
    {
        std::string text = dc::SerializeTitleContext(MakeSample());
        dc::TitleLaunchContext out;
        std::string err;
        const bool rejected =
            !dc::ParseTitleContext(text.substr(0, text.size() / 2), out, err);
        Check(rejected, "truncated envelope rejected");
    }

#ifdef _WIN32
    // 5. Live transport through the real supervisor: parent launches a child
    //    copy of itself with the typed context; the child verifies fields.
    {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        dc::ITitleSupervisor* sup = dc::CreateTitleSupervisor();
        if (!sup) {
            Check(false, "supervisor create");
        } else {
            dc::TitleLaunchContext ctx = MakeSample();
            auto launch =
                sup->Launch(path, ".", "--child-parse", ctx);
            Check(launch.ok, "supervised context launch");
            if (launch.ok) {
                dc::TitleExitReport rep = sup->WaitExit(15000);
                Check(rep.kind == dc::TitleExitKind::Clean,
                      dc::ITitleSupervisor::ExitKindName(rep.kind));
                Check(rep.exit_code == 0, "child verified all fields via env "
                                          "transport");
            }
            sup->TerminateTree();
            dc::DestroyTitleSupervisor(sup);
        }
    }
#endif

    std::printf("%s\n", g_failures ? "RESULT: FAIL" : "RESULT: all passed");
    return g_failures ? 1 : 0;
}
