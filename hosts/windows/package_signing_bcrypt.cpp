// package_signing_bcrypt.cpp — ECDSA P-256 package signatures via Windows
// CNG (DK0-M3; ADR-0025). The OS-established crypto provider; core declares
// the contract, this host layer owns the math. No invented cryptography.
#include "dc/package_signing.hpp"

#include "dc/hash.hpp"
#include "dc/json.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace dc::package {

namespace {

using dc::json::Object;
using dc::json::Value;

std::string ToHex(const uint8_t* p, size_t n) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s.push_back(hex[p[i] >> 4]);
        s.push_back(hex[p[i] & 0xf]);
    }
    return s;
}

bool FromHex(const std::string& hex, std::vector<uint8_t>* out) {
    if (hex.size() % 2) return false;
    out->clear();
    out->reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        auto val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = val(hex[i]), lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out->push_back(uint8_t((hi << 4) | lo));
    }
    return true;
}

#pragma pack(push, 1)
struct BCRYPT_ECCKEY_BLOB_W {
    ULONG dwMagic;
    ULONG cbKey;
};
// P-256 private blob: magic(4) cbKey(4) | X[32] Y[32] d[32]
// P-256 public blob:  magic(4) cbKey(4) | X[32] Y[32]
#pragma pack(pop)

constexpr ULONG kMagicPrivPriv = 0x32534345;  // ECDSA_PRIVATE_P256_MAGIC 'ECS2'
constexpr ULONG kMagicPub = 0x31534345;       // ECDSA_PUBLIC_P256_MAGIC  'ECS1'
constexpr ULONG kKeyBytes = 32;

bool OpenAlg(BCRYPT_ALG_HANDLE* out) {
    return BCryptOpenAlgorithmProvider(out, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0;
}

// Build a CNG ECCPUBLIC_BLOB from a 04||X||Y hex string (standard uncompressed
// point form used in envelopes). CNG's blob = 8-byte header + X||Y (no 04).
bool PubKeyHexToCngBlob(const std::string& hex130, std::vector<uint8_t>* out) {
    if (hex130.size() != 130 || hex130.substr(0, 2) != "04") return false;
    std::vector<uint8_t> xy;
    if (!FromHex(hex130.substr(2), &xy) || xy.size() != 2 * kKeyBytes) return false;
    out->assign(8 + xy.size(), 0);
    auto* hdr = reinterpret_cast<BCRYPT_ECCKEY_BLOB_W*>(out->data());
    hdr->dwMagic = kMagicPub;
    hdr->cbKey = kKeyBytes;
    for (size_t i = 0; i < xy.size(); ++i) (*out)[8 + i] = xy[i];
    return true;
}

// Extract 04||X||Y hex from a CNG ECCPUBLIC_BLOB.
std::string CngBlobToPubKeyHex(const uint8_t* blob, size_t n) {
    if (n != 8 + 2 * kKeyBytes) return {};
    return "04" + ToHex(blob + 8, 2 * kKeyBytes);
}

} // namespace

const char* TrustStateName(TrustState t) {
    switch (t) {
        case TrustState::Unsigned: return "UNSIGNED";
        case TrustState::SignedTrusted: return "SIGNED_TRUSTED";
        case TrustState::SignedUntrusted: return "SIGNED_UNTRUSTED";
        case TrustState::SignatureInvalid: return "SIGNATURE_INVALID";
    }
    return "UNKNOWN";
}

bool GenerateSigningKeyPair(std::string* private_blob_hex, std::string* public_key_hex,
                            std::string* error) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!OpenAlg(&alg)) {
        if (error) *error = "BCryptOpenAlgorithmProvider failed";
        return false;
    }
    BCRYPT_KEY_HANDLE key = nullptr;
    NTSTATUS st = BCryptGenerateKeyPair(alg, &key, 256, 0);
    if (st == 0) st = BCryptFinalizeKeyPair(key, 0);
    ULONG cb = 0;
    if (st == 0) st = BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB, nullptr, 0, &cb, 0);
    if (st != 0) {
        BCryptDestroyKey(key);
        BCryptCloseAlgorithmProvider(alg, 0);
        if (error) *error = "key generation failed";
        return false;
    }
    std::vector<uint8_t> priv(cb);
    if (BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB, priv.data(), cb, &cb, 0) != 0) {
        BCryptDestroyKey(key);
        BCryptCloseAlgorithmProvider(alg, 0);
        if (error) *error = "private export failed";
        return false;
    }
    ULONG cb_pub = 0;
    if (BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &cb_pub, 0) != 0) {
        BCryptDestroyKey(key);
        BCryptCloseAlgorithmProvider(alg, 0);
        if (error) *error = "public size query failed";
        return false;
    }
    std::vector<uint8_t> pub(cb_pub);
    if (BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, pub.data(), cb_pub, &cb_pub, 0) != 0) {
        BCryptDestroyKey(key);
        BCryptCloseAlgorithmProvider(alg, 0);
        if (error) *error = "public export failed";
        return false;
    }
    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(alg, 0);

    if (private_blob_hex) *private_blob_hex = ToHex(priv.data(), priv.size());
    // Public keys leave the host layer in standard 04||X||Y envelope form.
    if (public_key_hex) *public_key_hex = CngBlobToPubKeyHex(pub.data(), pub.size());
    return true;
}

SignResult SignManifest(const PackageManifest& m, const std::string& private_key_hex) {
    SignResult res;
    std::vector<uint8_t> priv_blob;
    if (!FromHex(private_key_hex, &priv_blob) || priv_blob.size() < 8 + 3 * kKeyBytes) {
        res.error = "invalid private key blob";
        return res;
    }
    auto* hdr = reinterpret_cast<BCRYPT_ECCKEY_BLOB_W*>(priv_blob.data());
    if (hdr->dwMagic != kMagicPrivPriv || hdr->cbKey != kKeyBytes) {
        res.error = "not a P-256 private blob";
        return res;
    }

    // The signature covers the CANONICAL manifest bytes (identity preimage).
    const std::string canonical = CanonicalManifestJson(m);
    const std::string identity = "sha256:" + dc::hash::Sha256::Hex(canonical);

    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!OpenAlg(&alg)) { res.error = "algorithm open failed"; return res; }
    BCRYPT_KEY_HANDLE key = nullptr;
    NTSTATUS st = BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPRIVATE_BLOB, &key,
                                      priv_blob.data(), (ULONG)priv_blob.size(), 0);
    if (st != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        res.error = "private key import failed";
        return res;
    }
    const std::string digest_bytes = dc::hash::Sha256::RawDigest(canonical);
    const char* digest = digest_bytes.data();
    const ULONG digest_size = (ULONG)digest_bytes.size();
    uint8_t sig[64] = {};
    ULONG done = 0;
    st = BCryptSignHash(key, nullptr, (PUCHAR)digest, digest_size, sig,
                        sizeof(sig), &done, 0);
    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st != 0 || done != 64) {
        res.error = "signing failed";
        return res;
    }

    // Recover the public key from the private blob (X||Y are in both blobs).
    res.ok = true;
    res.envelope.algorithm = "ECDSA_P256_SHA256";
    res.envelope.signature_hex = ToHex(sig, 64);
    res.envelope.signed_identity = identity;
    // Standard uncompressed-point form (04||X||Y), per the envelope contract.
    const uint8_t* xy = priv_blob.data() + 8;
    res.envelope.public_key_hex = "04" + ToHex(xy, 64);
    return res;
}

EnvelopeParseResult ParseSignatureJson(const std::string& bytes) {
    EnvelopeParseResult res;
    std::string err;
    auto v = dc::json::Parse(bytes, err);
    if (!v || !v->is_object()) { res.error = "signature.json malformed"; return res; }
    const auto* alg = v->find("algorithm");
    const auto* pk = v->find("public_key");
    const auto* sig = v->find("signature");
    const auto* idn = v->find("signed_identity");
    const auto* sch = v->find("schema");
    if (!sch || !sch->is_string() || sch->as_string() != "dc.package-signature/1") {
        res.error = "signature schema invalid";
        return res;
    }
    if (!alg || !alg->is_string() || alg->as_string() != "ECDSA_P256_SHA256") {
        res.error = "algorithm unsupported";
        return res;
    }
    if (!pk || !pk->is_string() || pk->as_string().size() != 130) {
        res.error = "public_key malformed";
        return res;
    }
    if (!sig || !sig->is_string() || sig->as_string().size() != 128) {
        res.error = "signature malformed";
        return res;
    }
    if (!idn || !idn->is_string()) {
        res.error = "signed_identity missing";
        return res;
    }
    res.ok = true;
    res.envelope.algorithm = alg->as_string();
    res.envelope.public_key_hex = pk->as_string();
    res.envelope.signature_hex = sig->as_string();
    res.envelope.signed_identity = idn->as_string();
    return res;
}

bool VerifySignature(const PackageManifest& m, const SignatureEnvelope& env, std::string* error) {
    // Envelope keys are 04||X||Y; convert to the CNG blob for import.
    std::vector<uint8_t> pub_blob;
    if (!PubKeyHexToCngBlob(env.public_key_hex, &pub_blob)) {
        if (error) *error = "public key malformed (want 04||X||Y hex)";
        return false;
    }
    std::vector<uint8_t> sig;
    if (!FromHex(env.signature_hex, &sig) || sig.size() != 64) {
        if (error) *error = "signature malformed";
        return false;
    }
    // Cross-check the envelope's declared identity against the recomputed one.
    const std::string identity = PackageIdentity(m);
    if (env.signed_identity != identity) {
        if (error) *error = "signed_identity != recomputed identity";
        return false;
    }

    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!OpenAlg(&alg)) {
        if (error) *error = "algorithm open failed";
        return false;
    }
    BCRYPT_KEY_HANDLE key = nullptr;
    NTSTATUS st = BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
                                      pub_blob.data(), (ULONG)pub_blob.size(), 0);
    if (st != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        if (error) *error = "public key import failed";
        return false;
    }
    const std::string digest_bytes = dc::hash::Sha256::RawDigest(CanonicalManifestJson(m));
    NTSTATUS ok = BCryptVerifySignature(key, nullptr, (PUCHAR)digest_bytes.data(),
                                        (ULONG)digest_bytes.size(),
                                        sig.data(), (ULONG)sig.size(), 0);
    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (ok != 0) {
        if (error) *error = "signature does not verify";
        return false;
    }
    return true;
}

TrustResult ResolveTrust(const PackageManifest& m, const SignatureEnvelope* env,
                         const std::vector<std::string>& trusted_keys,
                         bool allow_unsigned_developer) {
    TrustResult res;
    if (!env) {
        // Unsigned: acceptable ONLY under explicit developer policy.
        if (allow_unsigned_developer) {
            res.state = TrustState::Unsigned;
            res.detail = "unsigned developer package accepted by explicit policy";
        } else {
            res.state = TrustState::SignatureInvalid;  // fail closed: policy forbids
            res.detail = "unsigned package rejected by policy";
        }
        return res;
    }
    std::string err;
    if (!VerifySignature(m, *env, &err)) {
        res.state = TrustState::SignatureInvalid;
        res.detail = err;
        return res;
    }
    for (const auto& k : trusted_keys) {
        if (k == env->public_key_hex) {
            res.state = TrustState::SignedTrusted;
            res.detail = "signature verified against trusted key";
            return res;
        }
    }
    res.state = TrustState::SignedUntrusted;
    res.detail = "signature verifies but key is not trusted";
    return res;
}

std::string SignatureJson(const SignatureEnvelope& env) {
    std::string out;
    Object o;
    o.emplace("algorithm", Value(env.algorithm));
    o.emplace("public_key", Value(env.public_key_hex));
    // NOTE: Value(const char*) would bind to the bool ctor — wrap literals.
    o.emplace("schema", Value(std::string("dc.package-signature/1")));
    o.emplace("signature", Value(env.signature_hex));
    o.emplace("signed_identity", Value(env.signed_identity));
    dc::json::WriteCompact(Value(std::move(o)), out);
    return out;
}

} // namespace dc::package
