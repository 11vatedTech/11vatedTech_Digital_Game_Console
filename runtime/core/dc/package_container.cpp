// dc/package_container.cpp — deterministic .11g container I/O + full verify
// pipeline (DK0-M3; ADR-0025).
#include "dc/package_container.hpp"

#include "dc/hash.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace dc::package {

namespace {

inline void PutU16(std::string* s, uint16_t v) {
    s->push_back(char(v & 0xff));
    s->push_back(char((v >> 8) & 0xff));
}
inline void PutU64(std::string* s, uint64_t v) {
    for (int i = 0; i < 8; ++i) s->push_back(char((v >> (8 * i)) & 0xff));
}
inline uint16_t GetU16(const std::string& s, size_t off) {
    return uint16_t(uint8_t(s[off])) | (uint16_t(uint8_t(s[off + 1])) << 8);
}
inline uint64_t GetU64(const std::string& s, size_t off) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(uint8_t(s[off + i])) << (8 * i);
    return v;
}

constexpr char kMagic[4] = {'1', '1', 'G', 'B'};

} // namespace

bool BuildContainer(const std::vector<ContainerMember>& members, std::string* out) {
    if (members.empty() || !out) return false;
    std::vector<ContainerMember> sorted = members;
    std::sort(sorted.begin(), sorted.end(),
              [](const ContainerMember& a, const ContainerMember& b) { return a.path < b.path; });
    for (size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i].path == sorted[i - 1].path) return false;

    // Header: magic | version | header_size | record count
    std::string header;
    header.append(kMagic, 4);
    PutU16(&header, kContainerFormatVersion);
    PutU16(&header, 0);  // reserved
    PutU64(&header, sorted.size());

    size_t records_size = 0;
    for (const auto& m : sorted) records_size += 2 + m.path.size() + 8 + 8;

    const size_t payload_off = header.size() + records_size;
    std::string records;
    std::string payload;
    size_t off = payload_off;
    for (const auto& m : sorted) {
        PutU16(&records, uint16_t(m.path.size()));
        records.append(m.path);
        PutU64(&records, off);
        PutU64(&records, m.bytes.size());
        payload.append(m.bytes);
        off += m.bytes.size();
    }

    *out = header;
    out->append(records);
    out->append(payload);
    return true;
}

ContainerParseResult ParseContainer(const std::string& bytes) {
    ContainerParseResult res;
    if (bytes.size() < 16 || std::memcmp(bytes.data(), kMagic, 4) != 0) {
        res.reason = Reason::ContainerInvalid;
        res.detail = "not an .11g container (bad magic)";
        return res;
    }
    const uint16_t version = GetU16(bytes, 4);
    if (version != kContainerFormatVersion) {
        res.reason = Reason::SchemaVersionUnsupported;
        res.detail = "container format " + std::to_string(version);
        return res;
    }
    const uint64_t count = GetU64(bytes, 8);
    if (count == 0 || count > 100000) {
        res.reason = Reason::ContainerInvalid;
        res.detail = "member count " + std::to_string(count);
        return res;
    }

    size_t off = 16;
    struct Slot { std::string path; uint64_t offset, size; };
    std::vector<Slot> slots;
    slots.reserve(size_t(count));
    for (uint64_t i = 0; i < count; ++i) {
        if (off + 2 > bytes.size()) { res.reason = Reason::ContainerInvalid; res.detail = "records truncated"; return res; }
        const uint16_t plen = GetU16(bytes, off);
        off += 2;
        if (off + plen + 16 > bytes.size()) { res.reason = Reason::ContainerInvalid; res.detail = "records truncated"; return res; }
        Slot s;
        s.path.assign(bytes, off, plen);
        off += plen;
        s.offset = GetU64(bytes, off); off += 8;
        s.size = GetU64(bytes, off); off += 8;
        if (s.path.empty()) { res.reason = Reason::ContainerInvalid; res.detail = "empty member path"; return res; }
        slots.push_back(std::move(s));
    }
    // Records must be sorted (determinism contract) and non-overlapping.
    for (size_t i = 1; i < slots.size(); ++i) {
        if (!(slots[i - 1].path < slots[i].path)) {
            res.reason = Reason::ContainerInvalid;
            res.detail = "member order not canonical: " + slots[i].path;
            return res;
        }
        if (slots[i - 1].offset + slots[i - 1].size != slots[i].offset) {
            res.reason = Reason::ContainerInvalid;
            res.detail = "non-contiguous payload at " + slots[i].path;
            return res;
        }
    }
    for (const auto& s : slots) {
        if (s.offset > bytes.size() || s.size > bytes.size() - s.offset) {
            res.reason = Reason::ContainerInvalid;
            res.detail = "payload out of bounds: " + s.path;
            return res;
        }
        ContainerMember m;
        m.path = s.path;
        m.bytes.assign(bytes, s.offset, s.size);
        res.members.push_back(std::move(m));
    }
    return res;
}

bool ReadFileBytes(const std::string& path, std::string* out) {
    FILE* f = nullptr;
#if defined(_WIN32)
    fopen_s(&f, path.c_str(), "rb");
#else
    f = fopen(path.c_str(), "rb");
#endif
    if (!f || !out) { if (f) fclose(f); return false; }
    out->clear();
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out->append(buf, n);
    const bool ok = ferror(f) == 0;
    fclose(f);
    return ok;
}

bool WriteFileBytes(const std::string& path, const std::string& bytes) {
    FILE* f = nullptr;
#if defined(_WIN32)
    fopen_s(&f, path.c_str(), "wb");
#else
    f = fopen(path.c_str(), "wb");
#endif
    if (!f) return false;
    const bool ok = bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    return ok;
}

ContainerVerifyResult VerifyPackage(const std::string& container_bytes,
                                    const std::string& host_platform,
                                    const std::string& console_abi) {
    ContainerVerifyResult res;
    ContainerParseResult c = ParseContainer(container_bytes);
    if (c.reason != Reason::Ok) {
        res.reason = c.reason;
        res.detail = c.detail;
        res.reason_code = ReasonName(c.reason);
        return res;
    }

    const ContainerMember* manifest_member = nullptr;
    for (const auto& m : c.members)
        if (m.path == "manifest.json") { manifest_member = &m; break; }
    if (!manifest_member) {
        res.reason = Reason::ManifestMissing;
        res.detail = "manifest.json absent from container";
        res.reason_code = ReasonName(res.reason);
        return res;
    }
    if (manifest_member->bytes.empty()) {
        res.reason = Reason::ManifestMalformed;
        res.detail = "manifest.json empty";
        res.reason_code = ReasonName(res.reason);
        return res;
    }

    ParseResult p = ParseManifestJson(manifest_member->bytes);
    if (p.reason != Reason::Ok) {
        res.reason = p.reason;
        res.detail = p.detail;
        res.reason_code = ReasonName(p.reason);
        return res;
    }
    res.manifest = std::move(p.manifest);

    if (auto r = VerifyIdentity(res.manifest, &res.detail); r != Reason::Ok) {
        res.reason = r;
        res.reason_code = ReasonName(r);
        return res;
    }
    if (auto r = CheckPlatform(res.manifest, host_platform, &res.detail); r != Reason::Ok) {
        res.reason = r;
        res.reason_code = ReasonName(r);
        return res;
    }
    if (auto r = CheckRuntimeAbi(res.manifest, console_abi, &res.detail); r != Reason::Ok) {
        res.reason = r;
        res.reason_code = ReasonName(r);
        return res;
    }
    if (auto r = CheckEntrypoint(res.manifest, &res.detail); r != Reason::Ok) {
        res.reason = r;
        res.reason_code = ReasonName(r);
        return res;
    }

    // Content verification: objects live as container members "objects/<sha>".
    struct MemberFetcher {
        const std::vector<ContainerMember>* members;
        static bool Fetch(const std::string& object_sha, std::string* out, void* user) {
            auto* self = static_cast<MemberFetcher*>(user);
            for (const auto& m : *self->members) {
                if (m.path == "objects/" + object_sha) { *out = m.bytes; return true; }
            }
            return false;
        }
    } fetcher{&c.members};
    ContentResult cr = VerifyContent(res.manifest, &MemberFetcher::Fetch, &fetcher);
    if (cr.reason != Reason::Ok) {
        res.reason = cr.reason;
        res.detail = cr.detail;
        res.reason_code = ReasonName(cr.reason);
        return res;
    }

    res.identity = PackageIdentity(res.manifest);
    return res;
}

} // namespace dc::package
