// test_package_signing.cpp — ECDSA P-256 package signing tests (DK0-M3 §9).
// Crypto provider: Windows CNG. Covers:
//   - key generation produces valid P-256 blobs
//   - sign -> verify round-trip accepts
//   - any manifest tamper after signing -> verification fails
//   - wrong key -> verification fails
//   - trust policy: Unsigned (allowed/forbidden), SignedTrusted,
//     SignedUntrusted, SignatureInvalid all resolve correctly
#include "dc/hash.hpp"
#include "dc/package.hpp"
#include "dc/package_signing.hpp"

#include <cstdio>
#include <string>

using namespace dc::package;

namespace {
int g_checks = 0;
int g_failures = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("FAIL: %s\n", what); }
}

PackageManifest MakeManifest() {
    PackageManifest m;
    m.game_id = "tech.11vated.fidelitylab";
    m.version = "0.1.0";
    m.platforms = {"windows-x64"};
    m.game.name = "Fidelity Lab";
    m.game.entrypoints["windows-x64"] = "windows-x64/FidelityLab.exe";
    m.content.push_back({"windows-x64/FidelityLab.exe",
                         dc::hash::Sha256::Hex("EXE"), 3, dc::hash::Sha256::Hex("EXE")});
    m.package_id = PackageIdentity(m);
    return m;
}
} // namespace

int main() {
    // 1. Key generation.
    std::string priv, pub, err;
    Check(GenerateSigningKeyPair(&priv, &pub, &err), err.c_str());
    Check(pub.size() == 130 && pub.substr(0, 2) == "04", "public key is 04||X||Y");
    Check(priv.size() == 8 * 2 + 96 * 2, "private blob is header + X||Y||d");

    // 2. Sign -> verify round-trip.
    PackageManifest m = MakeManifest();
    SignResult s = SignManifest(m, priv);
    Check(s.ok, s.error.c_str());
    Check(s.envelope.signed_identity == PackageIdentity(m), "envelope identity matches");
    Check(VerifySignature(m, s.envelope, &err), "verify accepts valid signature");

    // 3. Tamper after signing -> reject.
    {
        PackageManifest t = m;
        t.game.name = "Tampered";
        t.package_id = PackageIdentity(t);
        Check(!VerifySignature(t, s.envelope, &err), "tampered manifest rejected");
    }
    {
        PackageManifest t = m;
        t.version = "0.1.1";
        t.package_id = PackageIdentity(t);
        std::string e2;
        Check(!VerifySignature(t, s.envelope, &e2), "version-tampered manifest rejected");
    }
    // Tampered envelope signature bytes.
    {
        SignatureEnvelope bad = s.envelope;
        bad.signature_hex[10] = bad.signature_hex[10] == '0' ? '1' : '0';
        std::string e2;
        Check(!VerifySignature(m, bad, &e2), "altered signature bytes rejected");
    }

    // 4. Wrong key -> reject.
    {
        std::string priv2, pub2, e2;
        Check(GenerateSigningKeyPair(&priv2, &pub2, &e2), "second keypair");
        SignResult s2 = SignManifest(m, priv2);
        Check(s2.ok, "sign with second key");
        SignatureEnvelope mixed = s.envelope;
        mixed.signature_hex = s2.envelope.signature_hex;
        std::string e3;
        Check(!VerifySignature(m, mixed, &e3), "signature from different key rejected");
    }

    // 5. Trust policy resolution.
    {
        // Unsigned + developer policy -> Unsigned (allowed).
        TrustResult t1 = ResolveTrust(m, nullptr, {}, true);
        Check(t1.state == TrustState::Unsigned, "unsigned accepted under developer policy");
        // Unsigned + strict policy -> fail closed.
        TrustResult t2 = ResolveTrust(m, nullptr, {}, false);
        Check(t2.state == TrustState::SignatureInvalid, "unsigned rejected under strict policy");
        // Signed by untrusted key -> SignedUntrusted.
        TrustResult t3 = ResolveTrust(m, &s.envelope, {}, true);
        Check(t3.state == TrustState::SignedUntrusted, "untrusted key resolves correctly");
        // Signed by trusted key -> SignedTrusted.
        TrustResult t4 = ResolveTrust(m, &s.envelope, {s.envelope.public_key_hex}, false);
        Check(t4.state == TrustState::SignedTrusted, "trusted key resolves correctly");
        // Tampered signature -> SignatureInvalid regardless of policy.
        SignatureEnvelope bad = s.envelope;
        bad.signature_hex[20] = bad.signature_hex[20] == 'a' ? 'b' : 'a';
        TrustResult t5 = ResolveTrust(m, &bad, {s.envelope.public_key_hex}, true);
        Check(t5.state == TrustState::SignatureInvalid, "invalid signature fails closed");
    }

    // 6. Envelope JSON round-trip.
    {
        const std::string json = SignatureJson(s.envelope);
        EnvelopeParseResult p = ParseSignatureJson(json);
        Check(p.ok, p.error.c_str());
        Check(p.envelope.signature_hex == s.envelope.signature_hex &&
                  p.envelope.public_key_hex == s.envelope.public_key_hex &&
                  p.envelope.signed_identity == s.envelope.signed_identity,
              "envelope round-trip preserves fields");
        // Foreign schema rejected.
        EnvelopeParseResult bad = ParseSignatureJson("{\"schema\":\"dc.package-signature/9\"}");
        Check(!bad.ok, "foreign signature schema rejected");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
