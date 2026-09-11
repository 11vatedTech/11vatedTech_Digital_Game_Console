// dc/package_container.hpp — .11g physical container (DK0-M3; ADR-0025).
//
// The logical package model (dc/package.hpp) is storage-agnostic; this layer
// is the DK0 physical representation. It is a deterministic tar-like format:
//
//   header   : magic "11GB" | u16 format_version=1 | u16 header_size
//   records  : repeated { u16 path_len | path | u64 offset | u64 size }
//   payload  : raw bytes of each member at its recorded offset
//
// No timestamps, no platform junk: identical member sets produce byte-identical
// containers. Members are stored in sorted path order. The container is
// deliberately NOT zip/7z semantics — the public contract only ever speaks in
// logical-package terms and this layout can be replaced without touching it.
#pragma once

#include "dc/package.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dc::package {

inline constexpr uint16_t kContainerFormatVersion = 1;

struct ContainerMember {
    std::string path;       // e.g. "manifest.json", "objects/<sha256>"
    std::string bytes;
};

// Deterministic container build: sorted members, no timestamps.
// Returns false on pathological input (empty set, duplicate paths).
bool BuildContainer(const std::vector<ContainerMember>& members, std::string* out);

struct ContainerParseResult {
    Reason reason = Reason::Ok;
    std::string detail;
    std::vector<ContainerMember> members;
};
ContainerParseResult ParseContainer(const std::string& bytes);

// Load a container file from disk (thin IO helper; core has no other fs code).
bool ReadFileBytes(const std::string& path, std::string* out);
bool WriteFileBytes(const std::string& path, const std::string& bytes);

// ---- full package verification over a container (directive §8 flow) --------

struct ContainerVerifyResult {
    Reason reason = Reason::Ok;
    std::string detail;         // human-readable, includes offending path
    std::string reason_code;    // ReasonName(reason) — machine-readable
    PackageManifest manifest;
    std::string identity;       // recomputed "sha256:<hex>"
};
// Verify manifest + identity + all content hashes. Host platform / console
// ABI checks are the caller's policy (tools pass their own values).
ContainerVerifyResult VerifyPackage(const std::string& container_bytes,
                                    const std::string& host_platform,
                                    const std::string& console_abi);

} // namespace dc::package
