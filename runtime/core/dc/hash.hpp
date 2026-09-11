// dc/hash.hpp — SHA-256 (FIPS 180-4) for content identity (DK0-M3; ADR-0025).
// runtime/core, host-agnostic (canon §39.1): package identity must not depend
// on a platform crypto provider. The implementation is validated in
// test_package_contract against the official NIST vectors AND cross-checked
// against Windows BCrypt on the host — never trusted on its own word alone.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dc::hash {

inline constexpr size_t kSha256Size = 32;

class Sha256 {
public:
    Sha256() { Reset(); }

    void Reset() {
        h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        len_ = 0;
        buf_used_ = 0;
    }

    void Update(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        len_ += n;
        while (n > 0) {
            if (buf_used_ == 0 && n >= 64) {
                ProcessBlock(p);
                p += 64;
                n -= 64;
                continue;
            }
            const size_t take = (64 - buf_used_) < n ? (64 - buf_used_) : n;
            for (size_t i = 0; i < take; ++i) buf_[buf_used_ + i] = p[i];
            buf_used_ += take;
            p += take;
            n -= take;
            if (buf_used_ == 64) {
                ProcessBlock(buf_.data());
                buf_used_ = 0;
            }
        }
    }

    std::array<uint8_t, kSha256Size> Finish() {
        // Padding: 0x80, zeros, 64-bit big-endian bit length.
        const uint64_t bits = static_cast<uint64_t>(len_) * 8;
        uint8_t pad = 0x80;
        Update(&pad, 1);
        pad = 0x00;
        while (buf_used_ != 56) Update(&pad, 1);
        uint8_t len_be[8];
        for (int i = 0; i < 8; ++i) len_be[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        Update(len_be, 8);  // len_ changes but bits were captured first.
        std::array<uint8_t, kSha256Size> out{};
        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = static_cast<uint8_t>(h_[i] >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(h_[i] >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(h_[i] >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(h_[i]);
        }
        return out;
    }

    // One-shot convenience.
    static std::array<uint8_t, kSha256Size> Digest(std::string_view data) {
        Sha256 h;
        h.Update(data.data(), data.size());
        return h.Finish();
    }

    // Raw 32-byte digest as a std::string (for crypto providers that take
    // pre-hashed input, e.g. BCryptSignHash).
    static std::string RawDigest(std::string_view data) {
        const auto d = Digest(data);
        return std::string(reinterpret_cast<const char*>(d.data()), d.size());
    }

    // Lowercase hex digest of a payload.
    static std::string Hex(std::string_view data) { return ToHex(Digest(data)); }

    // Raw-file digest (streams in chunks; works for large content objects).
    static bool DigestFile(const std::string& path, std::array<uint8_t, kSha256Size>* out);

    static std::string ToHex(const std::array<uint8_t, kSha256Size>& d) {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string s;
        s.reserve(kSha256Size * 2);
        for (uint8_t b : d) {
            s.push_back(kHex[b >> 4]);
            s.push_back(kHex[b & 0xf]);
        }
        return s;
    }

private:
    void ProcessBlock(const uint8_t* p);

    std::array<uint32_t, 8> h_{};
    std::array<uint8_t, 64> buf_{};
    size_t buf_used_ = 0;
    uint64_t len_ = 0;  // total bytes fed before padding
};

} // namespace dc::hash
