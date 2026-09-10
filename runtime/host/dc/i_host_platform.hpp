// i_host_platform.hpp — host abstraction boundary (DC-CANON-001 §7 L2; ADR-0002)
// Implementations live in hosts/<os>/. No OS types may appear in this header (canon §39.1).
#pragma once

#include "dc/capability.hpp"
#include "dc/result.hpp"

namespace dc {

class IHostPlatform {
public:
    virtual ~IHostPlatform() = default;

    // Family identifier: "windows" | "consoleos" (matches dc.host-capability/1 os.family).
    virtual const char* HostFamily() const = 0;

    // Full discovery pass: OS/CPU/GPU/memory/storage/display/audio/input/thermal/trust.
    // Score fields remain 0 (unmeasured) until microbenchmark phases are implemented.
    virtual Result QueryCapabilities(HostCapabilityRecord& out) = 0;
};

// Factory declared host-agnostically; defined by the selected host implementation.
IHostPlatform* CreateHostPlatform();

} // namespace dc
