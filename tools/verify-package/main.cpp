// dc-verify-package — structured .11g verification (DK0-M3; directive §8).
//
//   dc-verify-package <package.11g> [--json]
//
// Fails closed with precise reason codes; --json emits the full machine-
// readable verdict including trust state.
#include "dc/hash.hpp"
#include "dc/json.hpp"
#include "dc/package.hpp"
#include "dc/package_container.hpp"
#include "dc/package_signing.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace dc;
using namespace dc::package;

namespace {

void PrintJson(const std::string& reason_code, const std::string& detail,
               const std::string& identity, const std::string& trust,
               const std::string& game_id, const std::string& version) {
    json::Object o;
    o.emplace("reason_code", json::Value(reason_code));
    o.emplace("detail", json::Value(detail));
    o.emplace("valid", json::Value(reason_code == "OK"));
    if (!identity.empty()) o.emplace("package_id", json::Value(identity));
    if (!trust.empty()) o.emplace("trust", json::Value(trust));
    if (!game_id.empty()) o.emplace("game_id", json::Value(game_id));
    if (!version.empty()) o.emplace("version", json::Value(version));
    std::string out;
    json::WriteCompact(json::Value(std::move(o)), out);
    std::printf("%s\n", out.c_str());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: dc-verify-package <package.11g> [--json]\n");
        return 2;
    }
    bool as_json = false;
    for (int i = 2; i < argc; ++i)
        if (std::string(argv[i]) == "--json") as_json = true;

    std::string bytes;
    if (!ReadFileBytes(argv[1], &bytes)) {
        if (as_json) PrintJson(ReasonName(Reason::ContainerInvalid), "cannot read file", "", "", "", "");
        else std::printf("INVALID: %s — cannot read file\n", ReasonName(Reason::ContainerInvalid));
        return 1;
    }

    ContainerVerifyResult v = VerifyPackage(bytes, "windows-x64", kRuntimeAbi);
    if (v.reason != Reason::Ok) {
        if (as_json) PrintJson(v.reason_code, v.detail, "", "", "", "");
        else std::printf("INVALID: %s — %s\n", v.reason_code.c_str(), v.detail.c_str());
        return 1;
    }

    // Trust resolution: envelope present? trusted keys come from the trust
    // store (DK0: none installed -> signed packages report SignedUntrusted).
    SignatureEnvelope env;
    const SignatureEnvelope* env_ptr = nullptr;
    TrustResult trust;
    trust.state = TrustState::Unsigned;
    trust.detail = "no signature.json in container";
    for (const auto& m : ParseContainer(bytes).members) {
        if (m.path == kSignatureMember) {
            EnvelopeParseResult p = ParseSignatureJson(m.bytes);
            if (p.ok) {
                env = p.envelope;
                env_ptr = &env;
                // Development policy: unsigned accepted; signed verified against
                // an empty DK0 trust store (production installs trusted keys).
                trust = ResolveTrust(v.manifest, env_ptr, {}, false);
                // Re-map strict-policy Unsigned rejection to the truthful state.
                if (trust.state == TrustState::SignatureInvalid && env_ptr == nullptr)
                    trust.state = TrustState::Unsigned;
            } else {
                trust.state = TrustState::SignatureInvalid;
                trust.detail = "signature.json malformed: " + p.error;
            }
            break;
        }
    }

    if (as_json) {
        PrintJson("OK", "", v.identity, TrustStateName(trust.state),
                  v.manifest.game_id, v.manifest.version);
    } else {
        std::printf("VALID\n");
        std::printf("  package_id : %s\n", v.identity.c_str());
        std::printf("  game       : %s %s\n", v.manifest.game_id.c_str(), v.manifest.version.c_str());
        std::printf("  name       : %s\n", v.manifest.game.name.c_str());
        std::printf("  platform   : windows-x64 (abi %s)\n", v.manifest.runtime_abi.c_str());
        std::printf("  content    : %zu objects\n", v.manifest.content.size());
        std::printf("  entrypoint : %s\n", v.manifest.game.entrypoints.count("windows-x64")
                                             ? v.manifest.game.entrypoints.at("windows-x64").c_str()
                                             : "(none)");
        std::printf("  profiles   : required");
        for (const auto& p : v.manifest.game.required_profiles) std::printf(" %s", p.c_str());
        std::printf(" | optional");
        for (const auto& p : v.manifest.game.optional_profiles) std::printf(" %s", p.c_str());
        std::printf("\n  trust      : %s (%s)\n", TrustStateName(trust.state), trust.detail.c_str());
    }
    return 0;
}
