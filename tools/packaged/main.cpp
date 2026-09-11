// dc-packaged — .11g installer / library manager (DK0-M3; directive §10–§12).
//
//   dc-packaged install <package.11g> <library-root> [--allow-unsigned]
//   dc-packaged active   <game-id> <library-root>
//   dc-packaged rollback <game-id> <generation> <library-root>
//   dc-packaged generations <game-id> <library-root>
//
// Install flow: verify container -> import content-addressed objects ->
// build candidate generation -> materialize + verify -> ATOMIC activation.
// Any failure leaves the previous active generation untouched (§11).
#include "dc/package.hpp"
#include "dc/package_container.hpp"
#include "dc/package_signing.hpp"
#include "dc/package_store.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace dc;
using namespace dc::package;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage:\n"
                    "  dc-packaged install <package.11g> <library-root> [--allow-unsigned]\n"
                    "  dc-packaged active <game-id> <library-root>\n"
                    "  dc-packaged rollback <game-id> <generation> <library-root>\n"
                    "  dc-packaged generations <game-id> <library-root>\n");
        return 2;
    }
    const std::string cmd = argv[1];

    if (cmd == "install" && argc >= 4) {
        const std::string pkg = argv[2];
        const std::string root = argv[3];
        bool allow_unsigned = false;
        for (int i = 4; i < argc; ++i)
            if (std::string(argv[i]) == "--allow-unsigned") allow_unsigned = true;

        // Trust policy gate BEFORE any import (directive §10 order).
        std::string bytes;
        if (!ReadFileBytes(pkg, &bytes)) {
            std::printf("INSTALL FAILED: PACKAGE_CONTAINER_INVALID (cannot read)\n");
            return 1;
        }
        ContainerVerifyResult v = VerifyPackage(bytes, "windows-x64", kRuntimeAbi);
        if (v.reason != Reason::Ok) {
            std::printf("INSTALL FAILED: %s — %s\n", v.reason_code.c_str(), v.detail.c_str());
            return 1;
        }
        SignatureEnvelope env;
        const SignatureEnvelope* env_ptr = nullptr;
        for (const auto& m : ParseContainer(bytes).members) {
            if (m.path == kSignatureMember) {
                EnvelopeParseResult p = ParseSignatureJson(m.bytes);
                if (p.ok) { env = p.envelope; env_ptr = &env; }
                break;
            }
        }
        TrustResult trust = ResolveTrust(v.manifest, env_ptr, {}, allow_unsigned);
        if (trust.state == TrustState::SignatureInvalid) {
            std::printf("INSTALL FAILED: %s — %s\n", ReasonName(Reason::SignatureInvalid),
                        trust.detail.c_str());
            return 1;
        }

        Library lib(root);
        InstallResult r = lib.Install(pkg, "windows-x64", kRuntimeAbi);
        if (r.reason != Reason::Ok) {
            std::printf("INSTALL FAILED: %s — %s\n", r.reason_code.c_str(), r.detail.c_str());
            return 1;
        }
        std::printf("INSTALLED\n");
        std::printf("  game       : %s %s\n", r.installed.game_id.c_str(), r.installed.version.c_str());
        std::printf("  package_id : %s\n", r.installed.package_id.c_str());
        std::printf("  generation : %s\n", r.installed.generation.c_str());
        std::printf("  entrypoint : %s\n", r.installed.entrypoint.c_str());
        std::printf("  trust      : %s (%s)\n", TrustStateName(trust.state), trust.detail.c_str());
        return 0;
    }

    if (cmd == "active" && argc >= 4) {
        Library lib(argv[3]);
        auto a = lib.GetActive(argv[2]);
        if (!a) {
            std::printf("NO ACTIVE PACKAGE for %s\n", argv[2]);
            return 1;
        }
        std::printf("ACTIVE %s\n", a->generation.c_str());
        std::printf("  game       : %s %s\n", a->game_id.c_str(), a->version.c_str());
        std::printf("  package_id : %s\n", a->package_id.c_str());
        std::printf("  entrypoint : %s\n", a->entrypoint.c_str());
        std::printf("  install_dir: %s\n", a->install_dir.c_str());
        return 0;
    }

    if (cmd == "generations" && argc >= 4) {
        Library lib(argv[3]);
        for (const auto& g : lib.Generations(argv[2])) std::printf("%s\n", g.c_str());
        return 0;
    }

    if (cmd == "rollback" && argc >= 5) {
        Library lib(argv[4]);
        std::string err;
        if (!lib.ActivateGeneration(argv[2], argv[3], &err)) {
            std::printf("ROLLBACK FAILED: %s\n", err.c_str());
            return 1;
        }
        std::printf("ROLLED BACK to %s\n", argv[3]);
        return 0;
    }

    std::printf("unknown command\n");
    return 2;
}
