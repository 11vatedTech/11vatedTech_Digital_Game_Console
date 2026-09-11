// dc/package_signing.hpp — package signing/trust boundary (DK0-M3; ADR-0025,
// directive §9, §22).
//
// Trust model (kept strictly separate from content identity):
//   - content identity : SHA-256 over the canonical manifest (what it IS)
//   - integrity        : identity + per-object hashes (unchanged since seal)
//   - authenticity     : ECDSA P-256 signature over the canonical manifest
//   - execution trust  : policy decision combining the above
//
// Signed is NOT "safe", unsigned is NOT "malicious" (§22). DK0 accepts
// unsigned developer packages only through an explicit development policy;
// the verifier always reports the real trust state.
//
// Host boundary (canon §39.1): this header declares the contract; the crypto
// itself is CNG/BCrypt (the OS-established provider) in
// hosts/windows/package_signing_bcrypt.cpp. Core never implements math.
#pragma once

#include "dc/package.hpp"

#include <string>
#include <vector>

namespace dc::package {

enum class TrustState {
    Unsigned,        // no signature present (valid developer state)
    SignedTrusted,   // signature verifies against a trusted key
    SignedUntrusted, // signature present + verifies, key NOT in trust store
    SignatureInvalid,// signature present but does NOT verify (fail closed)
};

const char* TrustStateName(TrustState t);

// Signing envelope embedded as container member "signature.json":
// { "schema":"dc.package-signature/1", "algorithm":"ECDSA_P256_SHA256",
//   "public_key":"<hex: 04 || X || Y, 130 chars>",
//   "signature":"<hex r||s, 128 chars>",
//   "signed_identity":"sha256:<canonical manifest hash>" }
struct SignatureEnvelope {
    std::string algorithm;
    std::string public_key_hex;   // 04 || X || Y (65 bytes, hex)
    std::string signature_hex;    // r || s (64 bytes, hex)
    std::string signed_identity;  // must equal recomputed PackageIdentity
};

struct SignResult {
    bool ok = false;
    std::string error;
    SignatureEnvelope envelope;
};

// Sign the canonical identity of a manifest with an ECDSA P-256 key.
// private_key_hex: CNG BCRYPT_ECCPRIVATE_BLOB exported as hex (obtained from
// GenerateSigningKeyPair, below). The signature covers SHA-256 of the
// canonical (package_id-excluded) manifest bytes; the envelope records the
// identity string for cross-checking.
SignResult SignManifest(const PackageManifest& m, const std::string& private_key_hex);

// Generate a fresh P-256 key pair (CNG). Outputs:
//   private_blob_hex — BCRYPT_ECCPRIVATE_BLOB hex (keep secret; signs packages)
//   public_key_hex   — 04||X||Y hex (share; trust-store material)
bool GenerateSigningKeyPair(std::string* private_blob_hex, std::string* public_key_hex,
                            std::string* error);

// Parse + structurally validate an envelope from JSON bytes.
struct EnvelopeParseResult {
    bool ok = false;
    std::string error;
    SignatureEnvelope envelope;
};
EnvelopeParseResult ParseSignatureJson(const std::string& bytes);

// Cryptographic verification: ECDSA P-256 over SHA-256(canonical manifest).
// This is the math primitive — key TRUST is a separate policy decision.
bool VerifySignature(const PackageManifest& m, const SignatureEnvelope& env,
                     std::string* error);

// Trust resolution: crypto validity + key policy -> TrustState.
// trusted_keys: hex public keys (04||X||Y) accepted for production trust.
// allow_unsigned_developer: DK0 development policy switch (explicit opt-in).
struct TrustResult {
    TrustState state = TrustState::Unsigned;
    std::string detail;
};
TrustResult ResolveTrust(const PackageManifest& m, const SignatureEnvelope* env,
                         const std::vector<std::string>& trusted_keys,
                         bool allow_unsigned_developer);

// Deterministic container member name for the envelope.
inline constexpr const char* kSignatureMember = "signature.json";

// Serialize an envelope to JSON (deterministic field order).
std::string SignatureJson(const SignatureEnvelope& env);

} // namespace dc::package
