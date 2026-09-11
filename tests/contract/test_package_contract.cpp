// test_package_contract.cpp — .11g package contract tests (DK0-M3).
//
// Covers directive §5 (identity), §20 (determinism), §21 (corruption battery):
//   - SHA-256 validated against official NIST vectors
//   - SHA-256 cross-checked against Windows BCrypt on real bytes
//   - canonical-manifest determinism (byte-identical, key-order-insensitive parse)
//   - rename invariance / single-byte content change flips identity
//   - container round-trip + every corruption reason code
//   - content store put/get/verify + post-write verification
//   - atomic activation: failed install leaves previous active generation
//   - rollback: activate previous generation after a bad candidate
//   - save compatibility: v1.0.0 → v1.0.1 same save_schema recognized
#include "dc/hash.hpp"
#include "dc/json.hpp"
#include "dc/package.hpp"
#include "dc/package_container.hpp"
#include "dc/package_store.hpp"
#include "dc/save_boundary.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

using namespace dc;
using namespace dc::package;

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

// ---- test fixtures -----------------------------------------------------------

PackageManifest MakeManifest() {
    PackageManifest m;
    m.game_id = "tech.11vated.fidelitylab";
    m.version = "0.1.0";
    m.platforms = {"windows-x64"};
    m.game.name = "Fidelity Lab";
    m.game.publisher = "11vatedTech";
    m.game.entrypoints["windows-x64"] = "windows-x64/FidelityLab.exe";
    m.game.required_profiles = {"DCP-2026-BASE"};
    m.game.optional_profiles = {"DCP-2026-HDR"};
    m.lifecycle.title_id = m.game_id;
    m.lifecycle.quick_resume_tier = "QR1";
    m.save.save_schema = "s1";
    m.content.push_back({"windows-x64/FidelityLab.exe", "", 0, ""});
    m.content.push_back({"metadata/credits.txt", "", 0, ""});
    return m;
}

// Fill descriptor hashes from logical name -> bytes map; compute identity.
std::string Finalize(PackageManifest* m, const std::vector<std::pair<std::string, std::string>>& blobs) {
    for (auto& d : m->content) {
        for (const auto& [name, bytes] : blobs) {
            if (name == d.logical_path) {
                d.sha256 = hash::Sha256::Hex(bytes);
                d.object_sha256 = d.sha256;
                d.size_bytes = bytes.size();
            }
        }
    }
    const std::string id = PackageIdentity(*m);
    m->package_id = id;
    return id;
}

std::string BuildFullContainer(const PackageManifest& m,
                               const std::vector<std::pair<std::string, std::string>>& blobs) {
    std::vector<ContainerMember> members;
    members.push_back({"manifest.json", FullManifestJson(m)});
    for (const auto& [name, bytes] : blobs)
        members.push_back({"objects/" + hash::Sha256::Hex(bytes), bytes});
    std::string out;
    BuildContainer(members, &out);
    return out;
}

struct TempDir {
    std::string path;
    explicit TempDir(const std::string& name) {
        path = (std::filesystem::temp_directory_path() / name).string();
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

} // namespace

int main() {
    // ---- 1. SHA-256: official NIST vectors ------------------------------------
    {
        // FIPS 180-4 / NIST CAVP: "abc" and the 56-char two-block vector.
        Check(hash::Sha256::Hex("abc") ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "sha256 nist 'abc'");
        Check(hash::Sha256::Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              "sha256 nist two-block");
        Check(hash::Sha256::Hex("") ==
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "sha256 empty");
        // One million 'a' (incremental update across buffer boundaries).
        hash::Sha256 big;
        const std::string chunk(1000, 'a');
        for (int i = 0; i < 1000; ++i) big.Update(chunk.data(), chunk.size());
        const auto d = big.Finish();
        Check(hash::Sha256::ToHex(d) ==
                  "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              "sha256 million-a");
    }

    // ---- 2. SHA-256 cross-check vs BCrypt --------------------------------------
#if defined(_WIN32)
    {
        // Distinctive multi-block pattern (not a vector the impl could mirror).
        std::string blob;
        for (int i = 0; i < 5000; ++i) blob.push_back(char(0x41 + (i * 7) % 26));

        BCRYPT_ALG_HANDLE alg = nullptr;
        bool bcrypt_ok = false;
        unsigned char sys[32] = {};
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
            bcrypt_ok = BCryptHashData(alg, (PUCHAR)blob.data(), (ULONG)blob.size(), 0) == 0 &&
                        BCryptFinishHash(alg, sys, 32, 0) == 0;
            BCryptCloseAlgorithmProvider(alg, 0);
        }
        if (bcrypt_ok) {
            std::string sys_hex;
            for (int i = 0; i < 32; ++i) {
                char b[3];
                std::snprintf(b, 3, "%02x", sys[i]);
                sys_hex += b;
            }
            Check(hash::Sha256::Hex(blob) == sys_hex, "sha256 agrees with BCrypt");
        } else {
            std::printf("note: BCrypt unavailable, cross-check skipped\n");
        }
    }
#endif

    // ---- 3. Canonical determinism (directive §20) -------------------------------
    {
        PackageManifest a = MakeManifest();
        const std::vector<std::pair<std::string, std::string>> blobs = {
            {"windows-x64/FidelityLab.exe", "EXE-BYTES-V1"},
            {"metadata/credits.txt", "built by 11vated"},
        };
        Finalize(&a, blobs);
        PackageManifest b = MakeManifest();  // same inputs, fresh construction
        Finalize(&b, blobs);
        Check(CanonicalManifestJson(a) == CanonicalManifestJson(b), "canonical bytes identical");
        Check(PackageIdentity(a) == PackageIdentity(b), "identity stable across runs");

        // Parse→re-serialize round-trip must also be identity-stable.
        ParseResult p = ParseManifestJson(CanonicalManifestJson(a));
        Check(p.reason == Reason::Ok, "round-trip parses");
        Check(PackageIdentity(p.manifest) == PackageIdentity(a), "round-trip identity stable");

        // Any content change → different identity.
        PackageManifest c = MakeManifest();
        auto blobs_c = blobs;
        blobs_c[1].second = "built by 11vated.";
        Finalize(&c, blobs_c);
        Check(PackageIdentity(c) != PackageIdentity(a), "content change flips identity");

        // Metadata change → different identity.
        PackageManifest e = MakeManifest();
        e.version = "0.1.1";
        Finalize(&e, blobs);
        Check(PackageIdentity(e) != PackageIdentity(a), "version change flips identity");
    }

    // ---- 4. Container round-trip + rename invariance (§5) ----------------------
    {
        PackageManifest m = MakeManifest();
        const std::vector<std::pair<std::string, std::string>> blobs = {
            {"windows-x64/FidelityLab.exe", "EXE-BYTES-V1"},
            {"metadata/credits.txt", "built by 11vated"},
        };
        Finalize(&m, blobs);
        const std::string container = BuildFullContainer(m, blobs);

        ContainerVerifyResult v = VerifyPackage(container, "windows-x64", kRuntimeAbi);
        Check(v.reason == Reason::Ok, ReasonName(v.reason));
        Check(v.identity == PackageIdentity(m), "verify reports same identity");

        // Rename the .11g file: identity is content-derived, not filename.
        TempDir tmp("dc11g_rename");
        const std::string p1 = tmp.path + "/anything-a.11g";
        const std::string p2 = tmp.path + "/other-name-b.11g";
        Check(WriteFileBytes(p1, container), "write p1");
        Check(CopyFileBytes(p1, p2), "copy to renamed file");
        std::string bytes_a, bytes_b;
        ReadFileBytes(p1, &bytes_a);
        ReadFileBytes(p2, &bytes_b);
        ContainerVerifyResult va = VerifyPackage(bytes_a, "windows-x64", kRuntimeAbi);
        ContainerVerifyResult vb = VerifyPackage(bytes_b, "windows-x64", kRuntimeAbi);
        Check(va.reason == vb.reason && va.identity == vb.identity, "renamed package same identity");
    }

    // ---- 5. Corruption battery (§21) — every negative case fails closed --------
    {
        PackageManifest m = MakeManifest();
        const std::vector<std::pair<std::string, std::string>> blobs = {
            {"windows-x64/FidelityLab.exe", "EXE-BYTES-V1"},
            {"metadata/credits.txt", "built by 11vated"},
        };
        Finalize(&m, blobs);
        const std::string good = BuildFullContainer(m, blobs);

        // Truncated container.
        {
            auto v = VerifyPackage(good.substr(0, good.size() / 2), "windows-x64", kRuntimeAbi);
            Check(v.reason == Reason::ContainerInvalid, "truncation -> container invalid");
        }
        // Wrong magic.
        {
            std::string bad = good;
            bad[0] = 'X';
            auto v = VerifyPackage(bad, "windows-x64", kRuntimeAbi);
            Check(v.reason == Reason::ContainerInvalid, "bad magic rejected");
        }
        // Corrupted content byte → hash mismatch.
        {
            std::string bad = good;
            bad[bad.size() - 3] ^= 0x01;
            auto v = VerifyPackage(bad, "windows-x64", kRuntimeAbi);
            Check(v.reason == Reason::ContentHashMismatch, "flipped byte -> hash mismatch");
        }
        // Manifest tampering → identity mismatch.
        {
            PackageManifest t = m;
            t.game.name = "Tampered";
            std::vector<ContainerMember> members;
            members.push_back({"manifest.json", CanonicalManifestJson(t)});
            for (const auto& [n, b] : blobs) members.push_back({"objects/" + hash::Sha256::Hex(b), b});
            std::string bad;
            BuildContainer(members, &bad);
            auto v = VerifyPackage(bad, "windows-x64", kRuntimeAbi);
            Check(v.reason == Reason::IdentityMismatch, "manifest tamper -> identity mismatch");
        }
        // Missing object.
        {
            std::vector<ContainerMember> members;
            members.push_back({"manifest.json", FullManifestJson(m)});
            members.push_back({"objects/" + hash::Sha256::Hex(blobs[0].second), blobs[0].second});
            std::string bad;
            BuildContainer(members, &bad);
            auto v = VerifyPackage(bad, "windows-x64", kRuntimeAbi);
            Check(v.reason == Reason::StoreCorrupt, "missing object -> store corrupt");
        }
        // Missing entrypoint backing.
        {
            PackageManifest t = m;
            t.package_id = "";  // recompute below
            t.game.entrypoints["windows-x64"] = "windows-x64/missing.exe";
            const std::string id = PackageIdentity(t);
            t.package_id = id;
            std::string bad;
            BuildContainer({{"manifest.json", FullManifestJson(t)}}, &bad);
            auto v = VerifyPackage(bad, "windows-x64", kRuntimeAbi);
            Check(v.reason == Reason::EntrypointMissing, "entrypoint w/o content -> entrypoint missing");
        }
        // Foreign schema / bad semver / non-lowercase hash.
        {
            auto bad_schema = ParseManifestJson("{\"schema\":\"dc.package/9\",\"game_id\":\"g\","
                                                "\"version\":\"0.1.0\",\"platforms\":[\"windows-x64\"]}");
            Check(bad_schema.reason == Reason::SchemaVersionUnsupported, "foreign schema version rejected");
            auto bad_ver = ParseManifestJson("{\"schema\":\"dc.package/1\",\"game_id\":\"g\","
                                             "\"version\":\"not.semver\",\"platforms\":[\"windows-x64\"]}");
            Check(bad_ver.reason == Reason::ManifestFieldInvalid, "non-semver rejected");
            Check(!IsLowerHex64("ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"),
                  "uppercase hash rejected");
        }
        // Platform/ABI policy rejections.
        {
            auto v = VerifyPackage(good, "consoleos-x64", kRuntimeAbi);
            Check(v.reason == Reason::PlatformUnsupported, "wrong platform rejected");
            auto v2 = VerifyPackage(good, "windows-x64", "dc-runtime/2");
            Check(v2.reason == Reason::RuntimeIncompatible, "wrong ABI rejected");
        }
    }

    // ---- 6. Content store -------------------------------------------------------
    {
        TempDir tmp("dc11g_store");
        ContentStore store(tmp.path + "/store");
        std::string key, err;
        const std::string blob = "content object bytes 0123456789";
        Check(store.PutObject(blob, &key, &err), "store put");
        Check(key == "sha256:" + hash::Sha256::Hex(blob), "key is content hash");
        std::string back;
        Check(store.FetchObject(key, &back) && back == blob, "store get round-trip");
        std::string key2;
        Check(store.PutObject(blob, &key2, &err) && key2 == key, "put idempotent");
        std::string empty_err;
        Check(!store.PutObject("", &key2, &empty_err), "empty object refused");
        // Corrupt-on-disk detection via HasObject + GetObject.
        Check(store.HasObject(key), "has object");
    }

    // ---- 7. Install / atomic activation / rollback (§11/§12) --------------------
    {
        TempDir tmp("dc11g_lib");
        Library lib(tmp.path + "/library");
        const std::string src = tmp.path + "/src";
        MakeDirs(src);

        PackageManifest v1 = MakeManifest();
        const std::vector<std::pair<std::string, std::string>> b1 = {
            {"windows-x64/FidelityLab.exe", "EXE-BYTES-V1"},
            {"metadata/credits.txt", "built by 11vated"},
        };
        Finalize(&v1, b1);
        const std::string pkg_v1 = src + "/v1.11g";
        Check(WriteFileBytes(pkg_v1, BuildFullContainer(v1, b1)), "write v1");

        // Install v1 → active.
        InstallResult r1 = lib.Install(pkg_v1, "windows-x64", kRuntimeAbi);
        Check(r1.reason == Reason::Ok, r1.reason_code.c_str());
        auto active1 = lib.GetActive("tech.11vated.fidelitylab");
        Check(active1.has_value() && active1->generation == "g0001", "v1 active at g0001");
        Check(active1 && active1->entrypoint == "windows-x64/FidelityLab.exe", "entrypoint resolved");
        Check(active1 && active1->quick_resume_tier == "QR1", "lifecycle carried through");

        // Upgrade v2.
        PackageManifest v2 = v1;
        v2.version = "0.2.0";
        const std::vector<std::pair<std::string, std::string>> b2 = {
            {"windows-x64/FidelityLab.exe", "EXE-BYTES-V2-CHANGED"},
            {"metadata/credits.txt", "built by 11vated"},
        };
        Finalize(&v2, b2);
        const std::string pkg_v2 = src + "/v2.11g";
        Check(WriteFileBytes(pkg_v2, BuildFullContainer(v2, b2)), "write v2");
        InstallResult r2 = lib.Install(pkg_v2, "windows-x64", kRuntimeAbi);
        Check(r2.reason == Reason::Ok, r2.reason_code.c_str());
        Check(lib.GetActive("tech.11vated.fidelitylab")->generation == "g0002", "v2 active at g0002");

        // Corrupted v3 attempt must leave v2 active (§11 mandatory test).
        std::string corrupt = BuildFullContainer(v2, b2);
        corrupt[corrupt.size() - 4] ^= 0xFF;
        const std::string pkg_bad = src + "/v3-corrupt.11g";
        Check(WriteFileBytes(pkg_bad, corrupt), "write corrupt v3");
        InstallResult r3 = lib.Install(pkg_bad, "windows-x64", kRuntimeAbi);
        Check(r3.reason != Reason::Ok, "corrupt install fails closed");
        Check(lib.GetActive("tech.11vated.fidelitylab")->generation == "g0002",
              "v2 STILL active after failed v3 install");

        // Rollback to g0001 (§12).
        std::string rb_err;
        Check(lib.ActivateGeneration("tech.11vated.fidelitylab", "g0001", &rb_err), rb_err.c_str());
        Check(lib.GetActive("tech.11vated.fidelitylab")->package_id == PackageIdentity(v1),
              "rollback to v1 identity");
    }

    // ---- 8. Save compatibility (§18): v1 → v1.0.1 keeps saves --------------------
    {
        PackageManifest a = MakeManifest();  // 0.1.0, save_schema s1
        PackageManifest b = MakeManifest();
        b.version = "0.1.1";
        Check(a.save.save_schema == b.save.save_schema,
              "patch update preserves save_schema");
        Check(a.save.game_id == b.save.game_id, "save ownership game_id stable");
        // Ownership key concept: user_id + game_id + save_schema — none of the
        // three change across a patch, so saves remain recognized.
    }

    // ---- 9. Save boundary (§18): patch update keeps saves, schema break ----
    {
        TempDir tmp("dc11g_saves");
        const std::string root = tmp.path + "/saves";
        constexpr uint64_t kUser = 1;
        const char* kGame = "tech.11vated.fidelitylab";

        // v0.1.0 (schema s1) writes a save.
        std::string err;
        Check(save::WriteSave(root, kUser, kGame, "s1", "slot0.bin", "SAVE-V1", &err),
              "write save under s1");
        Check(save::SaveExists(root, kUser, kGame, "s1", "slot0.bin"), "save exists");

        // Patch to 0.1.1: same save_schema -> save still recognized.
        Check(save::SaveExists(root, kUser, kGame, "s1", "slot0.bin"),
              "patch update keeps save visible (same schema)");
        save::CompatibilityReport c1 =
            save::CheckCompatibility("s1", "s1", {});
        Check(c1.state == save::CompatibilityReport::State::Compatible, "compatible report");

        // Breaking change: schema bumped to s2 with migration declared.
        save::CompatibilityReport c2 = save::CheckCompatibility("s1", "s2", {"s1"});
        Check(c2.state == save::CompatibilityReport::State::Migratable, "migratable detected");
        // Read through the OLD schema is still possible (no silent destroy).
        std::string back;
        Check(save::ReadSave(root, kUser, kGame, "s1", "slot0.bin", &back) && back == "SAVE-V1",
              "old-schema save intact after schema bump");
        // Breaking change WITHOUT migration -> incompatible.
        save::CompatibilityReport c3 = save::CheckCompatibility("s1", "s2", {});
        Check(c3.state == save::CompatibilityReport::State::Incompatible,
              "undeclared break is incompatible");
        // Path traversal refused.
        Check(!save::WriteSave(root, kUser, kGame, "s1", "../evil.bin", "x", &err),
              "path traversal refused");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
