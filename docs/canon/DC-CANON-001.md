11vated Digital Console

Platform Constitution + System Architecture Specification

Document ID: DC-CANON-001
Version: 0.1.0
Status: Canonical architecture draft
Date: 2026-09-06
Owner: 11vatedTech
Primary implementation language: C++23
Secondary research/tooling language: Python 3.12+
Initial host: Windows 11
Long-term native host: ConsoleOS (purpose-built Linux-based appliance host)

0. Normative language

The words MUST, MUST NOT, SHOULD, SHOULD NOT, and MAY are normative requirements. A feature is not canonical merely because it is visually demonstrated; it is canonical only when its required contracts and verification gates pass.

1. Executive thesis

The 11vated Digital Console is a software-defined game console platform. Its identity is not tied to a manufactured motherboard, enclosure, or fixed GPU generation. A compatible computer becomes a console host by satisfying a versioned hardware/software capability contract and running the console runtime.

The platform combines the strongest properties of traditional consoles and high-end PCs:

deterministic controller-first interaction;

validated game/runtime contracts;

automatic display, audio, input, storage, and performance configuration;

console-grade lifecycle and update behavior;

continuously improvable compute hardware;

scalable native games that can expose more fidelity on future hosts;

compatibility with existing PC software without allowing legacy behavior to define the native platform.

The central proposition is:

Separate the console from the hardware generation, then separate the game’s canonical scalable representation from the fidelity one specific host can render today.

A successful implementation is not “a launcher for PC games.” It is a platform with its own host qualification, native title package, lifecycle, fidelity negotiation, certification, security, update, and developer contracts.

2. Platform constitution

2.1 Immutable principles

C1 — The console is a runtime, not a box

The physical computer is a host. The persistent identity belongs to the Digital Console runtime, profile, library, saves, platform services, and native game contract.

C2 — Native titles are not PC applications in disguise

A native title MUST target the Digital Console SDK and lifecycle contract. It MAY contain Windows and ConsoleOS executables, but it MUST NOT require desktop interaction, arbitrary installers, external launchers, registry configuration, keyboard-only dialogs, or manual graphics setup.

C3 — Zero-configuration is the default player contract

For normal operation, a player MUST be able to use the console with a controller from shell entry through game launch, gameplay, suspend/resume, settings, recovery, and shutdown.

C4 — Variable hardware must become deterministic profiles

The platform MUST not expose arbitrary PC component complexity to native games. It MUST convert raw hardware capability into versioned, machine-readable capability profiles and validated experience envelopes.

C5 — Highest fidelity means highest validated fidelity

The system MUST never select a setting merely because a benchmark predicts it might work. Native games expose validated fidelity domains and VFR selects only configurations proven to satisfy relevant frame-time, memory, IO, latency, and stability constraints.

C6 — The player does not manage graphics menus by default

The platform SHOULD remove conventional Low/Medium/High/Ultra menus from native console titles. Technical configuration is owned by VFR. Accessibility preferences and explicit player experience preferences remain valid inputs.

C7 — Frame generation does not redefine simulation performance

Generated/interpolated display frames MUST be reported separately from simulation/render frames. A 30 Hz simulation with generated 120 Hz presentation MUST NOT be certified as 120 Hz interaction.

C8 — Compatibility is a guest layer

Steam, Epic, GOG, Win32, Proton/Wine, emulation, and standalone PC software are compatibility guests. Their limitations MUST NOT weaken native-title requirements.

C9 — Offline operation is foundational

A local profile, installed native game, save system, settings, achievements cache, recovery path, and core shell MUST work without an internet connection. Cloud and social services are extensions, not boot dependencies.

C10 — No silent degradation

If HDR, VRR, ray tracing, reconstruction, haptics, spatial audio, or another capability is unavailable, the platform MUST expose truthful telemetry and select a valid fallback. It MUST NOT falsely advertise a capability class.

C11 — Security and modding are separate trust domains

The platform MUST support strong package integrity and sandboxing without defining all user modification as hostile. Trusted competitive mode and user-moddable mode are distinct policies.

C12 — Update failure must be survivable

ConsoleOS updates MUST be atomic or rollback-capable. A failed system update MUST NOT turn a healthy console into an unbootable appliance.

C13 — Future hardware is anticipated through semantic contracts, not promises of infinity

Native content MUST use scalable representations, optional fidelity packs, procedural detail, hierarchical assets, and capability semantics. The platform MUST NOT claim that arbitrary future algorithms can automatically reconstruct data that was never authored or represented.

C14 — Performance is measured end-to-end

FPS alone is insufficient. Certification MUST include frame time, pacing, latency, VRAM pressure, CPU/GPU queues, IO pressure, thermal stability, present behavior, and recovery.

C15 — A console UI is not the console

Visual polish is necessary but non-sufficient. Shell fidelity MUST never substitute for lifecycle, runtime, security, performance, storage, display, input, or certification work.

3. Truth boundary: proven, plausible, research-grade

Every roadmap item MUST carry one of these classifications.

3.1 Proven / implementable now

These are technically established and suitable for production implementation on current hardware:

Windows 11 host session with full-screen controller shell;

Direct3D 12 native rendering;

Vulkan native rendering;

controller/gamepad/device abstraction through GameInput and SDL3;

HDR capability detection and HDR-aware presentation;

VRR and modern flip-model presentation;

NVMe-aware streaming and DirectStorage/GDeflate on Windows;

precompiled/precached shader and PSO workflows;

hardware capability enumeration;

local package signing/integrity validation;

sandboxed native process execution with declared permissions;

local profiles, saves, achievements, capture, overlay, settings;

native title lifecycle callbacks;

title-defined suspend/checkpoint/resume;

automatic quality selection from prevalidated profiles;

content-addressed package chunks and differential patching;

Windows compatibility adapters for existing games;

hardware-accelerated remote streaming over a LAN;

Linux embedded compositor architecture using DRM/KMS/Wayland concepts;

A/B or rollback-capable ConsoleOS update architecture.

3.2 Plausible platform extensions

These require significant engineering but do not require scientific breakthroughs:

high-confidence cross-vendor VFR runtime adaptation;

reliable multi-title native Quick Resume with disk-backed checkpoints;

dynamic perceptual resource scheduling between rendering/simulation domains;

TV/AV appliance control where CEC-capable hardware exists;

console-wide reconstruction API with vendor-neutral fallbacks;

standardized scalable world/character fidelity contracts across engines;

remote play clients for TVs, handhelds, phones, and browsers/native apps;

developer certification cloud/local farm later, while keeping local certification possible;

federated/open social and save sync services;

per-display calibration profiles that follow the user locally or through optional sync.

3.3 Research-grade / must not be promised as launch functionality

transparent, reliable checkpoint/restore of arbitrary legacy GPU-heavy PC games across all vendors;

migrating a running arbitrary game between materially different GPU architectures without title cooperation;

automatically increasing missing art fidelity that was never represented by source assets or procedural rules;

universal HDMI-CEC control from commodity PC GPUs;

guaranteed elimination of all anti-cheat incompatibility on ConsoleOS;

“perfect” automatic graphics optimization for unknown titles with no title-authored quality contract;

treating AI-generated frames as equivalent to true high-rate simulation;

vendor-independent access to every proprietary controller feature without vendor cooperation.

Any product communication MUST preserve this distinction.

4. Competitive target: what “surpass manufactured consoles” means

The objective is not to claim universal superiority regardless of host cost or hardware quality. The objective is to remove fixed-generation constraints while restoring console determinism.

The platform SHOULD be designed to surpass fixed consoles in:

peak rendering capability on sufficiently powerful hosts;

ray-tracing capability;

neural/reconstruction capability;

CPU simulation headroom;

VRAM and RAM ceilings;

local storage expansion;

hardware upgradeability;

backward compatibility breadth;

controller/peripheral breadth;

modding openness;

developer access;

local/remote display flexibility;

game longevity across hardware generations;

user ownership of local software and saves;

optional high-end fidelity packs beyond a base console target.

The platform MUST explicitly acknowledge hardware-dependent areas where a custom manufactured console can remain superior:

performance per dollar;

idle/load power efficiency;

thermal/noise consistency;

homogeneous hardware validation burden;

physical simplicity for nontechnical customers.

The Digital Console wins by making software identity persistent while compute becomes replaceable.

5. System context

                         +-----------------------+
                         |  Optional Cloud Layer |
                         | sync/social/store/etc |
                         +-----------+-----------+
                                     |
+----------------+       +-----------v-----------+       +----------------+
| TV / Monitor   |<----->|  Digital Console      |<----->| Controllers    |
| HDMI/DP/Remote |       |  Runtime + Shell      |       | audio/access.  |
+----------------+       +-----------+-----------+       +----------------+
                                     |
                         +-----------v-----------+
                         | Host Abstraction Layer |
                         +-----------+-----------+
                                     |
                   +-----------------+------------------+
                   |                                    |
          +--------v---------+                 +--------v---------+
          | Windows Host     |                 | ConsoleOS Host    |
          | D3D12/Win32      |                 | Vulkan/DRM/KMS    |
          +--------+---------+                 +--------+---------+
                   |                                    |
          +--------v------------------------------------v---------+
          | CPU | GPU | RAM | NVMe | Network | HDMI | USB | TPM |
          +-------------------------------------------------------+

6. Host editions

6.1 Windows Host Edition — bootstrap and compatibility host

Purpose:

fastest route to working console behavior on existing gaming PCs;

maximum compatibility with commercial PC games and drivers;

Direct3D 12 and DirectStorage reference host;

first platform for SDK and VFR validation.

Important boundary: normal Windows Home/Pro installations do not provide the same first-party shell-replacement facilities as Enterprise/Education/IoT Shell Launcher deployments. Therefore the consumer Windows edition MUST be described as a console session running on Windows, not a bare-metal console OS.

The Windows host MUST:

auto-start optionally after user sign-in;

enter a borderless/full-screen console session;

suppress normal desktop friction where legally and technically supported;

preserve a deliberate escape/recovery path to the desktop;

own Guide-button routing while the console session is active;

supervise games and recover shell focus after game termination/crash;

detect display/input/audio/storage changes;

keep compatibility game launchers hidden behind adapters where practical;

never require privileged kernel modifications for basic console operation.

6.2 Windows Lab Shell Edition

For Enterprise/Education/IoT lab machines, Shell Launcher MAY be used to replace Explorer and validate true appliance-style shell ownership. This edition is a laboratory target, not the assumption for ordinary Windows Home installations.

6.3 ConsoleOS — long-term appliance host

ConsoleOS exists to own the machine from boot through gameplay.

Baseline architecture:

UEFI boot;

Secure Boot capable;

immutable or read-only system image;

A/B system slots or equivalent rollback-safe image model;

Linux kernel;

systemd service supervision;

Wayland-native console shell;

DRM/KMS direct display control;

gamescope-like embedded compositor semantics;

Vulkan primary native graphics API;

PipeWire audio routing;

BlueZ Bluetooth;

udev device management;

NetworkManager/iwd or equivalent networking;

Mesa/AMD/Intel drivers and supported NVIDIA proprietary/open kernel stack as hardware permits;

Proton/Wine as compatibility guest technology, not native API.

ConsoleOS MUST use the same public Digital Console service contracts as Windows Host Edition.

7. Logical platform layers

L0  Firmware / hardware trust
L1  Host OS + device drivers
L2  Host abstraction
L3  Core console services
L4  Native game runtime + compatibility runtime
L5  VFR / qualification / telemetry
L6  Console shell + overlays
L7  Native game SDK / engine adapters
L8  Optional network/cloud/social/store services

The platform MUST keep L2–L7 semantically stable across Windows and ConsoleOS.

8. Process model

The production runtime SHOULD be multi-process. A shell crash must not corrupt package state; a package updater must not run inside a game process; a game crash must not terminate the console runtime.

Recommended processes:

dc-hostd          host abstraction and privileged host operations
dc-sessiond       console session/state orchestration
dc-deviced        hardware/device topology
dc-displayd       display modes, HDR, VRR, calibration
dc-inputd         controller ownership, mapping, haptics
dc-audiod         endpoint routing and spatial capability
dc-storaged       mount/space/IO capability/storage health
dc-packaged       .11g package install/mount/verify/update
dc-securityd      trust, signatures, entitlement policy, sandbox policy
dc-fidelityd      VFR host qualification + runtime governor
dc-saved          transactional save service
dc-achievementd   local achievements/event journal
dc-captured       screenshot/video capture service
dc-updated        console runtime/OS update service
dc-telemetryd     local performance/health telemetry
dc-compatd        legacy launcher/compatibility adapters
dc-shell          controller-first visual shell
dc-overlay        guide/notification/capture overlay

Not every service must be a separate executable in DevKit-0. The architecture MUST preserve service boundaries so components can be separated later without breaking APIs.

8.1 Privilege model

dc-hostd, dc-packaged, dc-securityd, and dc-updated MAY require elevated/platform privileges.

dc-shell, games, and normal UX services SHOULD run unprivileged.

Native games MUST run with least privilege and declared capabilities.

Compatibility titles run under a separate, less restrictive policy because arbitrary Win32 compatibility cannot satisfy the native sandbox contract by default.

8.2 IPC

Canonical IPC design:

schema: FlatBuffers or another versioned zero/low-copy schema format;

Windows transport: named pipes + shared memory for high-frequency telemetry;

ConsoleOS transport: Unix domain sockets + shared memory;

request/response for service calls;

publish/subscribe event bus for topology/lifecycle events;

monotonic timestamps for all latency-sensitive events;

explicit schema versions and backwards-compatibility rules.

High-frequency frame telemetry MUST NOT be sent as verbose JSON.

9. Global console state machine

POWER_ON
  -> HOST_BOOTING
  -> HOST_DISCOVERY
  -> CONSOLE_INITIALIZING
  -> SHELL_ACTIVE

SHELL_ACTIVE
  -> TITLE_STARTING
  -> TITLE_ACTIVE
  -> TITLE_SUSPENDING
  -> TITLE_SUSPENDED
  -> TITLE_RESUMING
  -> TITLE_ACTIVE
  -> TITLE_STOPPING
  -> SHELL_ACTIVE

Any active state
  -> RECOVERY
  -> SHELL_ACTIVE | REBOOT | SAFE_MODE

SHELL_ACTIVE
  -> SYSTEM_UPDATE_PREPARE
  -> SYSTEM_UPDATE_STAGE
  -> REBOOT_PENDING

POWER_EVENT
  -> SESSION_CHECKPOINT
  -> STANDBY | SHUTDOWN

Every transition MUST have:

entry criteria;

timeout;

rollback/recovery behavior;

persistent journal entry;

user-visible fallback if recovery cannot be automatic.

10. Host qualification architecture

The hardware qualification system is one of the platform’s defining technologies. A Digital Console host is not identified by marketing model names alone.

10.1 Capability vector

Each host MUST produce a signed/local integrity-protected HostCapabilityRecord containing at minimum:

{
  "schema": "dc.host-capability/1",
  "host_id": "local-stable-id",
  "os": {
    "family": "windows",
    "version": "11",
    "build": 26200
  },
  "cpu": {
    "arch": "x86_64",
    "logical_cores": 24,
    "game_thread_score": 0,
    "worker_score": 0,
    "sustained_score": 0
  },
  "gpu": {
    "vendor": "nvidia",
    "api": ["d3d12", "vulkan"],
    "vram_bytes": 0,
    "raster_score": 0,
    "compute_score": 0,
    "rt_score": 0,
    "matrix_score": 0,
    "mesh_shader": true,
    "sampler_feedback": true
  },
  "memory": {
    "system_bytes": 0,
    "bandwidth_score": 0,
    "pressure_safe_bytes": 0
  },
  "storage": [{
    "id": "disk0",
    "class": "nvme",
    "read_seq_mbps": 0,
    "read_random_score": 0,
    "latency_us_p50": 0,
    "latency_us_p99": 0,
    "gpu_decompression": true
  }],
  "display": [],
  "audio": [],
  "input": [],
  "thermal": {
    "sustained_profile_valid": false
  },
  "trust": {
    "secure_boot": false,
    "tpm": false,
    "measured_boot_available": false
  }
}

Scores MUST be derived from platform-owned reproducible microbenchmarks and real game-like workloads, not vendor model-name tables alone.

10.2 Qualification phases

A host qualifies in four phases:

Feature discovery — API features, limits, codecs, device topology.

Microbenchmarking — CPU, GPU, RT, matrix, memory, storage, presentation.

Sustained qualification — verify performance after thermal equilibrium rather than only short boost behavior.

Profile derivation — match the measured vector to versioned Digital Console Profiles.

10.3 Profile model

Do not reduce the whole host to one arbitrary number. Profiles SHOULD be composable and versioned by capability family, for example:

DCP-2026-BASE
DCP-2026-HIGHIO
DCP-2026-RT1
DCP-2026-RT2
DCP-2026-AI1
DCP-2026-4K120
DCP-2026-HDR

A host claims the intersection of profiles it satisfies. A title declares required and optional profiles.

A consumer-facing “Experience Class” MAY summarize this vector, but the SDK MUST retain the full capability data.

10.4 Qualification cache invalidation

Requalification MUST occur when any material variable changes:

GPU;

CPU or firmware power configuration;

major GPU driver update;

system memory size;

storage device used for games;

display change for display-dependent certification;

major runtime version;

thermal/power-policy change that materially affects sustained performance.

11. Display + HDMI subsystem

The display subsystem converts arbitrary TV/monitor topology into a console presentation contract.

11.1 Required responsibilities

dc-displayd MUST support:

hot-plug detection;

display identity/fingerprint;

native mode discovery;

resolution/refresh enumeration;

color depth and format discovery where available;

HDR/WCG capability and active-state discovery;

peak/minimum/average luminance where exposed;

VRR capability and range where exposed;

safe-area/overscan policy;

multi-display ownership policy;

audio-over-HDMI endpoint correlation;

per-display calibration persistence;

display loss and recovery events;

stable rollback if a mode change produces no usable output.

11.2 Presentation policy

The shell SHOULD stay in one stable, high-quality display mode rather than constantly forcing physical mode changes. Native games SHOULD prefer modern flip-model/borderless presentation on Windows where it provides lower friction and modern feature support.

The runtime MUST not hard-code nominal refresh rates. It MUST use enumerated timing/mode data.

11.3 HDR contract

A display profile MUST include:

HDR supported?
HDR currently active?
advanced color kind
bit depth
native primaries
white point
min luminance
max luminance
max full-frame luminance
SDR white level
HDR metadata formats where exposed

The console SHOULD provide one system-level HDR calibration flow and expose its result to native titles. Titles MAY add artistic calibration but MUST not require users to repeat basic hardware calibration unnecessarily.

11.4 VRR/ALLM/QFT/LIP

The runtime SHOULD exploit gaming-oriented HDMI features when the host GPU, driver, cable, and display path expose them. These features are capabilities, not assumptions.

VRR: supported where the presentation path allows it.

ALLM: use where source/driver control exists.

QFT: benefit is hardware/path dependent.

LIP: future HDMI 2.2-aware implementations MAY use it where platform APIs expose useful control/telemetry.

The runtime MUST work correctly when none are available.

11.5 CEC

CEC is OPTIONAL.

If supported through native hardware or a validated adapter, the platform MAY implement:

wake TV;

standby TV;

input switching;

limited remote-control navigation;

AVR coordination.

CEC failure MUST NOT prevent console use.

11.6 Display hot-unplug behavior

Policy order:

detect output loss;

preserve game process if safe;

attempt another validated display;

if none exists, transition title to suspended/background policy;

journal state;

restore when a validated display returns.

No native game may assume its startup display remains permanently connected.

12. Input subsystem

12.1 Goals

The console input layer converts heterogeneous controllers into a stable semantic game/console API while retaining advanced device features when available.

12.2 Backends

Windows baseline:

Microsoft GameInput as primary low-latency device API;

SDL3 controller database/mapping as portable/fallback abstraction;

vendor SDK adapters only for optional proprietary features.

ConsoleOS baseline:

evdev/hidraw/libinput/SDL3-class device integration;

BlueZ for Bluetooth;

vendor adapters as optional capability providers.

12.3 Semantic device model

ConsoleDevice
  identity
  connection
  battery
  player_assignment
  capabilities

GamepadSemanticState
  sticks
  triggers
  buttons
  dpad

ExtendedState
  gyro
  accelerometer
  touch_surface
  paddles
  adaptive_trigger_channels
  haptic_actuators
  LEDs

A title MUST be able to target baseline semantics while discovering richer optional capabilities.

12.4 Controller ownership

The runtime MUST own:

Guide/system button;

player assignment;

controller reconnect;

active-user association;

controller-driven shell focus;

controller battery notifications;

system remapping/accessibility transforms.

A native game receives already-resolved player/controller identity.

12.5 Haptics

The platform SHOULD expose semantic haptic channels rather than only left/right rumble:

low_frequency
high_frequency
impulse_left
impulse_right
surface_texture
impact
continuous_force

Backends map supported semantics to hardware. Unsupported channels degrade gracefully.

13. Audio subsystem

dc-audiod owns endpoint capability and route negotiation.

13.1 Required support

TV/HDMI stereo;

HDMI multichannel;

headphones;

USB audio;

Bluetooth audio;

spatial audio endpoint discovery;

endpoint hot-switch;

microphone route discovery where permitted;

user volume/mute policy;

game/system mix policy.

13.2 Native spatial audio contract

Native titles SHOULD emit either:

standard channel beds;

spatial objects;

ambisonic fields;

semantic voice/chat buses.

The console selects an endpoint-specific rendering strategy.

13.3 Audio continuity

Changing from TV speakers to headphones MUST NOT require restarting a native title. Audio topology changes are lifecycle events.

14. Storage + IO architecture

The Digital Console treats storage capability as part of host performance, not merely free space.

14.1 Storage classes

At minimum:

DC_STORAGE_LEGACY
DC_STORAGE_SATA_SSD
DC_STORAGE_NVME_BASE
DC_STORAGE_NVME_HIGH
DC_STORAGE_STREAMING_CERTIFIED

Native titles SHOULD declare storage requirements semantically.

14.2 Package/content storage

Installed native games SHOULD use a content-addressed chunk store with:

deduplication;

integrity hashes;

independent chunk verification;

atomic manifest activation;

differential updates;

optional compression variants;

optional high-fidelity content packs;

storage relocation support.

14.3 Compression codecs

Baseline package codecs SHOULD include:

Zstandard for portable CPU decompression/content distribution;

GDeflate variant on Windows where DirectStorage GPU decompression is beneficial;

raw/uncompressed for latency-critical already-compressed resources.

A package chunk declares its codec. Codec support is part of the host capability record.

14.4 DirectStorage path

On Windows, a native title MAY use DirectStorage directly or through an SDK abstraction. The SDK SHOULD enable GPU-decompression-friendly content layout when GDeflate is selected.

GDeflate-oriented streams SHOULD be authored with awareness of 64 KiB decompression tiles and batching behavior.

14.5 IO QoS

The runtime SHOULD expose priority classes:

CRITICAL_STREAM
GAMEPLAY_STREAM
BACKGROUND_PRELOAD
PATCH
CAPTURE_WRITE
CLOUD_SYNC

Game loading MUST not be unpredictably starved by background patching or capture uploads.

15. .11g native package format

.11g is a provisional project codename for the native Digital Console title package. The extension/name MAY change; the semantics are canonical.

15.1 Design goals

A native package MUST be:

self-describing;

signed;

content-addressable;

incrementally updateable;

multi-host-capable;

sandbox-policy-aware;

lifecycle-aware;

fidelity-contract-aware;

inspectable by developer tools;

installable without arbitrary executable installers.

15.2 Logical layout

Game.11g/
  manifest/
    game.json
    runtime.json
    capabilities.json
    fidelity.json
    lifecycle.json
    permissions.json
    display.json
    input.json
    audio.json
    save.json
    achievements.json
    accessibility.json
    network.json
    content.json
    build-provenance.json
  binaries/
    windows-x64/
    consoleos-x64/
  shaders/
    d3d12/
    vulkan/
  content/
    base/
    optional/
    fidelity-packs/
  metadata/
    icon/
    hero/
    localization/
  signatures/

This is a logical model; the physical package SHOULD be chunked and indexed rather than a naïve folder archive.

15.3 Game manifest example

{
  "schema": "dc.game/1",
  "id": "tech.11vated.fidelitylab",
  "version": "0.1.0",
  "name": "Fidelity Lab",
  "publisher": "11vatedTech",
  "entrypoints": {
    "windows-x64": "binaries/windows-x64/FidelityLab.exe",
    "consoleos-x64": "binaries/consoleos-x64/FidelityLab"
  },
  "required_profiles": ["DCP-2026-BASE"],
  "optional_profiles": ["DCP-2026-HDR", "DCP-2026-RT1", "DCP-2026-4K120"],
  "controller_required": true,
  "offline_launch": true
}

15.4 Package activation

Install flow:

resolve manifest
 -> download/import chunks
 -> verify chunk hashes
 -> verify signed metadata
 -> construct candidate package view
 -> run compatibility/preflight checks
 -> atomically activate new manifest
 -> retain rollback metadata

A partially downloaded update MUST NOT mutate the currently playable package view.

15.5 Content identity

Baseline integrity SHOULD use SHA-256 for interoperable cryptographic content identity. Faster secondary local fingerprints MAY be used for dedup/cache acceleration but MUST NOT replace signed cryptographic integrity.

16. Native title runtime contract

16.1 Required lifecycle API

Conceptual C++ contract:

namespace dc {

struct LaunchContext;
struct SuspendContext;
struct ResumeContext;
struct DisplayTopology;
struct UserContext;
struct MemoryPressure;

class IConsoleTitleLifecycle {
public:
    virtual ~IConsoleTitleLifecycle() = default;

    virtual Result OnLaunch(const LaunchContext&) = 0;
    virtual void   OnActivated() = 0;
    virtual void   OnDeactivated() = 0;
    virtual Result OnSuspend(const SuspendContext&) = 0;
    virtual Result OnResume(const ResumeContext&) = 0;
    virtual void   OnDisplayChanged(const DisplayTopology&) = 0;
    virtual void   OnUserChanged(const UserContext&) = 0;
    virtual void   OnMemoryPressure(const MemoryPressure&) = 0;
    virtual void   OnExitRequested() = 0;
};

}

No global singleton is required; runtime interfaces are injected through a versioned ConsoleServices context.

16.2 Services context

struct ConsoleServices {
    IDisplayService&      display;
    IInputService&        input;
    IAudioService&        audio;
    IStorageService&      storage;
    ISaveService&         saves;
    IAchievementService&  achievements;
    IFidelityService&     fidelity;
    IOverlayService&      overlay;
    ITelemetryService&    telemetry;
    IUserService&         users;
};

Implementations MUST avoid mutable process-wide global service state.

16.3 Exit behavior

A native title MUST:

honor platform exit request;

flush transactional save state;

stop new network operations;

release exclusive devices;

return control to shell without leaving foreground windows or orphan child processes.

17. VFR — Virtual Fidelity Runtime

VFR is the platform’s system for converting host capability and title-authored scalable domains into a validated experience.

17.1 VFR is a negotiator, not the game renderer

The game engine retains its render graph and simulation architecture. VFR provides:

qualification;

budgets;

quality-domain negotiation;

telemetry;

transition policy;

validated profile selection.

17.2 Fidelity domain schema

Example:

{
  "domain": "global_illumination",
  "states": [
    {"id":"gi0","utility":0.20,"gpu_ms":0.6,"vram_mb":64},
    {"id":"gi1","utility":0.45,"gpu_ms":1.3,"vram_mb":96},
    {"id":"gi2","utility":0.72,"gpu_ms":2.6,"vram_mb":180},
    {"id":"gi3","utility":0.90,"gpu_ms":4.8,"vram_mb":320}
  ],
  "transition": "scene_boundary_or_hysteretic_runtime",
  "criticality": "visual"
}

Other domains include:

internal render resolution;

reconstruction method;

geometry density;

texture residency;

anisotropy;

shadowing;

reflection path;

GI;

volumetrics;

particles;

hair representation;

cloth solver quality;

crowd count;

destruction density;

physics solver frequency;

animation deformation quality;

simulation tick rate;

audio spatial object count.

17.3 Objective function

Let each domain i select a discrete state x_i.

VFR seeks to maximize perceptual/gameplay utility:

maximize  Σ U_i(x_i, scene, gameplay_intent)

subject to:

GPU_ms(x)  <= GPU_budget
CPU_ms(x)  <= CPU_budget
VRAM(x)    <= VRAM_safe
RAM(x)     <= RAM_safe
IO(x)      <= IO_safe
Latency(x) <= latency_target
Thermal(x) <= sustained_envelope

The system SHOULD NOT solve an expensive unconstrained combinatorial problem every frame. Instead it SHOULD use an offline-generated Pareto frontier plus a fast runtime governor.

17.4 Two-stage VFR architecture

Stage A — Fidelity Compiler (offline/dev/build time)

consumes title quality domains;

runs automated benchmark scenes;

measures cost/utility;

detects invalid combinations;

generates Pareto-efficient profile candidates;

records driver/host-specific exceptions;

emits certification evidence.

Stage B — Fidelity Governor (runtime)

selects an initial validated profile;

reserves safety headroom;

monitors frame/latency/memory/IO pressure;

changes only approved dynamic domains;

uses hysteresis and dwell time;

avoids oscillation;

records reason codes for every adaptation.

17.5 Safety headroom

VFR MUST target less than the theoretical frame budget to tolerate variance.

Example for 60 Hz:

physical frame budget       16.67 ms
certification target        e.g. 14.5–15.5 ms average envelope
reserved variance/headroom  remainder

Exact thresholds are title/profile/certification-policy decisions, not one universal constant.

17.6 Transition policy

A fidelity change MUST declare whether it is:

INSTANT_SAFE;

HYSTERETIC_RUNTIME;

CAMERA_CUT_ONLY;

SCENE_BOUNDARY;

RELOAD_REQUIRED;

RESTART_REQUIRED.

The runtime MUST NOT silently modify restart-required settings during gameplay.

17.7 Player-visible policy

Default UX:

Automatic — Recommended

Optional player intents MAY include semantically meaningful choices such as:

Responsive
Balanced
Cinematic

These are inputs to VFR, not manual collections of graphics toggles. The automatic mode remains canonical.

18. Canonical scalable content model

The platform’s future-fidelity promise depends on authoring semantics, not wishful upscaling.

18.1 Canonical representation principle

A native title SHOULD preserve richer source structure than a fixed-target build normally exposes. The build pipeline then produces host-appropriate projections.

AUTHORING REPRESENTATION
        |
        +-- geometry hierarchy
        +-- texture/material hierarchy
        +-- procedural parameters
        +-- animation/deformation hierarchy
        +-- simulation quality hierarchy
        +-- audio hierarchy
        +-- optional future fidelity pack metadata
        |
        v
FIDELITY COMPILER
        |
        +-- base runtime representation
        +-- enhanced representation
        +-- extreme representation
        +-- future-compatible semantic metadata

18.2 Geometry

Preferred strategies:

virtualized/clustered geometry where engine technology permits;

explicit mesh hierarchy where virtualized geometry is unavailable;

displacement/microgeometry where useful;

procedural detail layers;

stable collision/simulation proxies separate from pure render density;

per-domain residency budgets.

18.3 Textures/materials

The content pipeline SHOULD preserve:

high-quality source textures outside shipped runtime packages;

mipmapped runtime texture hierarchies;

optional high-resolution texture packs;

material parameterization rather than baked appearance where possible;

micro-normal/displacement layers;

HDR-capable authored values;

texture streaming metadata.

The platform MUST NOT require users to download enormous source-art archives to claim future compatibility.

18.4 Characters

Scalable character domains MAY include:

mesh density
skin shading complexity
subsurface quality
micro-normal detail
hair cards/hybrid/strands
facial joint/blendshape/deformation layers
muscle/soft-tissue deformation
cloth solver quality
secondary motion
eye shading
shadow quality

18.5 Simulation

Simulation fidelity MUST be treated separately from rendering fidelity. Domains MAY scale:

crowd population;

rigid-body count;

solver iteration count;

cloth/hair simulation frequency;

fluid grid resolution;

destruction persistence;

AI update frequency where gameplay-equivalent behavior is preserved;

ambient ecosystem simulation.

Gameplay rules MUST NOT silently change because VFR changed visual performance class unless the title explicitly defines deterministic equivalence.

19. Rendering contract

19.1 Native graphics APIs

Windows native baseline:

Direct3D 12.

ConsoleOS native baseline:

Vulkan.

A title MAY ship both backends through its engine. The platform SHOULD not invent a proprietary low-level graphics API unless an actual platform limitation later proves one necessary.

19.2 Cross-vendor reconstruction contract

The SDK SHOULD expose a reconstruction interface rather than a vendor-specific requirement:

struct ReconstructionInputs {
    TextureHandle color;
    TextureHandle depth;
    TextureHandle motion_vectors;
    TextureHandle exposure;
    TextureHandle reactive_mask;
    TextureHandle transparency_mask;
    CameraJitter jitter;
};

struct ReconstructionPolicy {
    Resolution input;
    Resolution output;
    bool allow_frame_generation;
    bool prefer_low_latency;
};

Backends MAY include:

title-native temporal reconstruction;

AMD FSR;

Intel XeSS;

NVIDIA DLSS/Streamline integrations where licensing/hardware permits;

future open/vendor implementations.

The Digital Console MUST always retain a non-proprietary fallback path for native certification unless a capability profile explicitly requires a vendor technology.

19.3 Ray tracing

Ray tracing is a capability domain, not a platform requirement for all titles.

The host record SHOULD distinguish:

acceleration-structure support;

ray queries;

RT pipeline support;

measured traversal/shading performance;

memory cost;

denoising/reconstruction capability.

VFR MAY select hybrid raster/RT paths based on measured benefit.

19.4 Frame generation

Frame-generation APIs MUST expose:

simulation_hz
render_hz
generated_present_hz
end_to_end_latency

Certification reports MUST display all four where relevant.

20. Shader and PSO architecture

Shader hitching is a platform quality failure when it is predictable and preventable.

20.1 Native policy

Native titles MUST provide sufficient pipeline metadata and precompilation/precaching coverage that certified traversal does not trigger unbounded synchronous shader/PSO compilation stalls.

20.2 Pipeline cache lifecycle

GAME INSTALL/UPDATE
      |
      v
HOST PROFILE IDENTIFIED
      |
      v
SHADER VARIANT SELECTION
      |
      v
PSO/Pipeline Cache Build or Import
      |
      v
VALIDATION
      |
      v
PLAYABLE

Cache keys MUST include all relevant inputs such as:

game build;

shader library version;

renderer backend;

GPU/driver identity where required;

runtime ABI;

relevant graphics feature profile.

20.3 Invalidating changes

A material driver/runtime/game update MAY invalidate caches. The runtime SHOULD rebuild in background before the user launches where possible.

20.4 Certification evidence

The cert harness SHOULD record:

runtime pipeline creation count;

blocking pipeline creation time;

shader compile events;

frame hitches correlated to compilation;

cache hit ratio.

21. Frame pacing and latency architecture

21.1 Latency chain

The platform SHOULD measure:

controller sample timestamp
 -> game input consumption
 -> simulation start/end
 -> render submission
 -> GPU start/end
 -> present call
 -> presentation/scanout timing where observable

21.2 Required metrics

Performance certification MUST include at least:

simulation FPS
render FPS
display/generated FPS if distinct
average frame time
p95/p99/p99.9 frame time
1% low and 0.1% low
frame pacing variance
CPU main-thread time
CPU worker pressure
GPU frame time
VRAM high-water mark
RAM high-water mark
storage latency/throughput pressure
present mode
VRR active state
thermal throttling events

Latency-capable titles SHOULD additionally expose:

input-to-simulation age
simulation-to-present age
estimated input-to-photon chain

21.3 Dynamic adaptation

VFR SHOULD respond to persistent pressure, not single-frame noise. Adaptation MUST use:

rolling windows;

hysteresis;

minimum dwell times;

severity thresholds;

emergency fallback for sustained missed deadlines.

A visual setting MUST not flap between two states every few frames.

22. Suspend, resume, and Quick Resume

22.1 Native suspend contract

A native title MUST be able to enter a quiescent state in response to platform suspend.

Suspend responsibilities include:

stop accepting new external work;

journal game state required by contract;

flush transactional saves;

pause/close network resources as required;

serialize platform-defined resume state;

release resources the platform declares non-persistent;

acknowledge suspend before timeout.

22.2 Resume contract

Resume responsibilities:

validate user identity;

recreate invalid device/display/audio resources;

reinitialize network stack/middleware if required;

restore checkpoint;

revalidate VFR profile because host/display state may have changed;

continue from an acceptable gameplay location.

22.3 Quick Resume tiers

QR0  no resume guarantee; clean restart only
QR1  title-authored checkpoint + fast reload
QR2  native process suspension + title checkpoint safety
QR3  disk-backed native snapshot where host/backend supports it

Only QR1/QR2 should be launch-critical. QR3 is a later optimization.

22.4 Legacy titles

Legacy titles MAY receive:

process suspend/resume;

adapter-defined save/resume hints;

game-specific scripting;

experimental checkpoint technology.

The platform MUST label this best-effort and MUST NOT imply native Quick Resume guarantees.

23. Save system

The save service is platform-owned for native titles.

23.1 Requirements

atomic local commits;

multiple generations/recovery points;

schema/version metadata;

per-user ownership;

offline-first operation;

corruption detection;

storage-pressure policy;

optional encryption for sensitive save payloads;

optional sync provider interface;

conflict resolution metadata.

23.2 Transaction API

Conceptual API:

SaveTransaction tx = saves.Begin(slot_id);
tx.Write("world", world_blob);
tx.Write("profile", profile_blob);
Result r = tx.Commit();

A power loss during commit MUST leave either the previous valid generation or the new valid generation, never a partially committed canonical save.

23.3 Optional cloud sync

Cloud sync MUST be provider-abstracted. The core console MUST not require a paid third-party API. Initial builds MAY support local-only saves and later add self-hosted/federated sync.

24. Profiles, achievements, presence, and social

These are services, not prerequisites for game execution.

24.1 Local profile

A local console profile SHOULD contain:

display name;

avatar;

controller preferences;

accessibility preferences;

display calibration references;

save ownership;

local achievement journal;

privacy settings.

24.2 Achievements

Native achievements SHOULD be event-driven and locally journaled.

achievements.Emit("boss.first_defeat", AchievementProgress{1, 1});

The service validates idempotency and persists state. Optional online synchronization may occur later.

24.3 Social network

The platform MUST NOT make a centralized proprietary social graph necessary for the first viable console. A later implementation SHOULD use open/federated service boundaries where practical.

25. Overlay and system UI

25.1 System Guide

The Guide overlay MUST be reachable from the system button regardless of the active native game.

Core Guide functions:

return home;

resume game;

switch game where supported;

capture screenshot/video;

volume/audio route;

controller battery;

notifications;

performance/debug overlay in developer mode;

suspend/close game;

system power.

25.2 Composition strategy

Windows:

prefer OS-supported composition/top-level overlay techniques that avoid code injection;

native titles MAY opt into a tighter presentation bridge for guaranteed overlay behavior;

compatibility titles use best-effort overlay composition.

ConsoleOS:

compositor owns composition and can layer shell/overlay above the game directly.

The platform MUST avoid brittle mandatory DLL injection as the foundational overlay mechanism.

26. Capture system

dc-captured SHOULD support:

screenshots;

rolling replay buffer;

manual video capture;

HDR-aware capture metadata;

microphone/voice policy;

hardware encoding where available;

storage quota;

native game capture-protection flags only for narrowly justified content/security scenarios.

Capture work MUST run at a QoS that does not destabilize certified frame-time behavior.

27. Compatibility runtime

Compatibility is intentionally isolated from native console semantics.

27.1 Adapter interface

Each compatibility adapter SHOULD implement:

DiscoverInstalledTitles()
ResolveMetadata()
CanLaunch()
Launch()
RequestStop()
DetectExit()
FindSaveLocations()
GetControllerHints()
GetKnownDisplayOverrides()
GetKnownCompatibilityIssues()

Adapters MAY exist for:

Steam;

Epic Games Store;

GOG;

itch.io;

EA/Ubisoft/Battle.net where permitted;

standalone executables;

emulators;

Proton/Wine prefixes.

27.2 Compatibility title UX

The shell SHOULD hide external launchers when feasible but MUST not break store authentication or DRM. If a launcher requires user interaction, the shell presents it as a compatibility surface rather than pretending it does not exist.

27.3 Compatibility database

Maintain a local/open compatibility knowledge base containing:

launch command;

focus behavior;

controller support;

display behavior;

save paths;

suspend safety;

launcher requirements;

known anti-cheat restrictions;

per-title workarounds.

This database MUST be versioned and evidence-backed.

28. Remote Console

Remote Console converts another display/device into a console terminal without moving the game process.

28.1 Architecture

GAME
 -> compositor/capture
 -> hardware encoder
 -> low-latency transport
 -> remote decoder/display

REMOTE CONTROLLER
 -> timestamped input channel
 -> console input service
 -> game

28.2 Codec targets

Where available:

AV1;

HEVC;

H.264 compatibility fallback.

28.3 Transport

The production transport SHOULD be replaceable. Candidates include QUIC-based custom transport or a WebRTC-derived path. The protocol MUST support:

congestion adaptation;

low-latency input return;

multi-controller;

audio;

HDR metadata where supported;

packet-loss recovery policy;

local-network discovery;

authenticated pairing.

28.4 Remote fidelity

Remote Console adds a second optimization layer:

local game fidelity
+
encode resolution/bitrate
+
network latency/loss
+
decode/display capability

VFR MAY receive remote-display/network constraints, but gameplay simulation fidelity MUST not collapse merely to satisfy temporary network conditions when video adaptation alone is sufficient.

29. Networking platform

The base console SHOULD provide common native services without forcing one hosted multiplayer backend.

Candidate APIs:

IPv4/IPv6 socket abstraction;

QUIC transport helper;

LAN discovery;

NAT traversal plugin boundary;

session/lobby abstraction later;

voice/chat transport boundary;

secure credential/token storage;

network lifecycle notifications.

Native titles MUST handle loss/reacquisition of network after suspend/resume.

30. Security and trust architecture

30.1 Trust chain

UEFI Secure Boot (when available)
 -> trusted host boot
 -> runtime binaries
 -> package trust metadata
 -> signed .11g manifests
 -> verified chunks
 -> sandboxed title process

Windows Host Edition can use Windows Secure Boot/TPM/measured-boot capabilities where available. ConsoleOS should provide its own measured/verified boot policy.

30.2 Package trust

A native package MUST have:

publisher identity;

signed manifest metadata;

content hashes;

expiry/rotation policy for repository metadata;

rollback/freeze attack protection for online update metadata;

local installation provenance.

The update architecture SHOULD follow The Update Framework (TUF) threat model rather than inventing update trust from scratch.

30.3 Sandbox policy

Native permissions SHOULD include granular capabilities such as:

network.internet.client
network.lan.client
network.lan.server
microphone
camera
user.documents.read
user.documents.write
external_storage
local_multiplayer_discovery

Default is deny unless capability is required.

On Windows, AppContainer/Win32 isolation capabilities SHOULD be evaluated per native title architecture. Some high-performance games may require a carefully designed medium-integrity sandbox/job-object policy rather than strict AppContainer if graphics/middleware compatibility is impacted; the security contract must therefore be tested, not assumed.

30.4 Trusted competitive mode

Competitive titles MAY require a stronger host posture:

Secure Boot;

TPM-backed measured boot evidence where available;

signed runtime;

verified package;

restricted modification domain;

integrity telemetry.

The platform SHOULD prefer user-space/platform integrity and server-side game validation. Kernel anti-cheat MUST NOT be a platform-wide requirement.

30.5 Mod mode

A title MAY declare:

mods: disabled
mods: signed_only
mods: user_local
mods: sandboxed_extension_api

A modded session may carry a separate trust status without punishing offline single-player use.

31. Update architecture

31.1 Runtime and game updates

Updates MUST be staged and activated atomically at the manifest/version level.

31.2 ConsoleOS A/B model

Preferred initial ConsoleOS model:

EFI System Partition
System A (read-only image)
System B (read-only image)
Persistent /var
Game content store
User/save data
Recovery tools

Update flow:

boot A
 -> download signed B image/update
 -> verify
 -> write inactive B
 -> mark B trial
 -> reboot B
 -> run health gates
 -> mark B good

if B fails boot/health repeatedly
 -> firmware/bootloader selects A

RAUC, systemd boot assessment concepts, or an equivalent audited open implementation can provide the fail-safe mechanics. The project SHOULD prefer reuse of proven atomic-update machinery over a custom updater for the earliest ConsoleOS generation.

31.3 Update channels

stable
preview
developer

Moving to a less stable channel MUST be explicit. Developer channel must not silently become the consumer default.

32. Console shell architecture

The consumer shell is the most visible component but remains a client of platform services rather than the platform itself.

32.1 Rendering stack

Recommended cross-host shell stack:

C++23 application core;

SDL3 for portable window/device integration where appropriate;

Direct3D 12 backend on Windows;

Vulkan backend on ConsoleOS;

Skia-class vector/text rasterization or equivalent GPU-accelerated 2D layer;

FreeType/HarfBuzz/ICU-class typography and localization stack;

custom retained UI/layout/animation layer or carefully selected open UI framework;

Dear ImGui only for developer/debug surfaces, not the consumer shell.

The shell SHOULD target HDR-aware rendering and high refresh rates from the beginning.

32.2 UX invariants

every primary path controller navigable;

no hover-only affordances;

focus always visible;

focus restoration deterministic after overlays/game exit;

readable at television distance;

no dependency on Windows desktop metaphors;

animation never blocks input;

shell remains responsive while background services work;

network failure does not freeze local library;

local installed content is first-class;

power/recovery actions always reachable.

32.3 Boot experience

The shell MAY present a cinematic identity, but startup MUST prioritize readiness. Heavy network/content calls begin after local shell readiness.

33. Developer SDK

The SDK turns the platform from a product into a console ecosystem.

33.1 SDK components

11SDK/
  include/
  lib/
  schemas/
  tools/
  unreal/
  unity/
  godot/
  samples/
  cert/
  docs/

33.2 Native SDK APIs

At minimum:

Host/Capabilities
Lifecycle
Display
Input/Haptics
Audio
Storage/Streaming
Saves
Achievements
Users
Overlay
Capture
Fidelity
Telemetry
Networking
Accessibility
Package/Build metadata

33.3 Engine integrations

Unreal Engine

Plugin SHOULD provide:

subsystem wrappers;

platform lifecycle callbacks;

GameInput/controller mapping;

VFR scalability-domain integration;

PSO collection/certification hooks;

DirectStorage integration helpers;

HDR/display profile access;

packaging commandlet for .11g;

automated certification maps/traversal;

trace markers.

Unity

Plugin SHOULD provide equivalent lifecycle, input, save, achievement, VFR, package, telemetry, and certification hooks.

Godot

Open plugin SHOULD expose native platform services and build/export support.

Custom C++ engines

The C ABI boundary SHOULD remain minimal and versioned so non-C++ engines can bind to it.

33.4 Developer tools

Required tools:

dc-doctor          validate developer environment
dc-hostprof        inspect/benchmark host capability
dc-pack            build/sign/inspect .11g package
dc-run             run title under console runtime
dc-vfr             inspect fidelity domains/Pareto profiles
dc-cert            execute certification suites
dc-trace           capture console/game performance trace
dc-display         display/HDR/VRR diagnostics
dc-input           controller/haptics diagnostics
dc-storage         storage/DirectStorage diagnostics
dc-save            inspect/recover developer saves
dc-compat          compatibility adapter diagnostic tool

34. Certification framework

Certification is what converts heterogeneous hardware into a trustworthy console experience.

34.1 Certification layers

L1 Package Integrity
L2 Runtime Contract
L3 Controller/UX
L4 Lifecycle/Recovery
L5 Display/Audio/Input
L6 Performance/Fidelity
L7 Storage/Streaming
L8 Security/Permissions
L9 Accessibility
L10 Long-duration Reliability

A title is not “Digital Console Certified” until all required levels for its declared profile pass.

34.2 Package integrity tests

signature verifies;

all referenced chunks exist;

hashes match;

manifest schema valid;

required runtime ABI supported;

forbidden undeclared executables absent;

no arbitrary post-install scripts in native package;

rollback metadata valid.

34.3 Controller-only tests

Certification bot/manual run MUST verify:

first launch;

profile selection;

menus;

gameplay start;

pause;

settings;

system Guide;

suspend/resume;

save/load;

exit;

error/recovery flows;

without keyboard/mouse.

34.4 Lifecycle tests

Inject:

focus loss;

controller disconnect/reconnect;

display disconnect/reconnect;

audio route change;

network loss;

suspend;

resume;

user change;

low-memory notification;

storage pressure;

forced game crash;

shell crash;

service restart.

34.5 Performance certification

Each title declares one or more target experience envelopes, e.g.:

60 Hz Native Target
120 Hz Native Target
HDR Target
RT Target
Remote Target

For each envelope, cert captures:

representative traversal corpus;

combat/stress scenarios;

worst-known authored scenes;

cutscenes;

menu transitions;

loading/streaming spikes;

long-duration thermal run.

Pass/fail thresholds MUST be explicit and version-controlled.

34.6 Fidelity evidence

A VFR configuration is certified only if:

selected domains match a generated validated profile;

resource limits remain within safe margins;

transitions respect declared transition safety;

no sustained oscillation occurs;

visual corruption tests pass;

performance trace evidence is attached.

34.7 Shader hitch tests

cold cache;

warm cache;

driver-cache invalidation scenario;

game update scenario;

representative traversal;

pipeline creation event audit.

34.8 Storage tests

minimum allowed storage class;

fragmented/pressure simulation where meaningful;

background update while playing;

capture writes while streaming;

nearly-full disk;

move game between compatible volumes;

interrupted update recovery.

34.9 HDR/display tests

SDR display;

HDR display;

HDR enabled/disabled transition;

60/120 Hz;

VRR on/off;

display hotplug;

invalid mode rollback;

TV overscan/safe area;

luminance calibration.

34.10 Reliability soak

Native certification SHOULD include multi-hour automated soak tests with telemetry anomaly detection and memory/resource leak checks.

35. Accessibility as a platform contract

Accessibility cannot be delegated entirely to games.

The console SHOULD provide:

global text scaling preference;

high-contrast system UI;

reduced-motion preference;

screen reader/navigation semantics for shell;

controller remapping;

hold/toggle preferences;

stick sensitivity/dead-zone transforms;

mono audio option;

captions/subtitle preference metadata;

color accessibility preference metadata;

alternative controller support;

per-user persistence.

Native titles SHOULD be able to query platform preferences and apply them on first launch.

36. Telemetry, evidence, and privacy

36.1 Local-first telemetry

Performance telemetry MUST be usable entirely locally. A console must not require cloud analytics to diagnose itself.

36.2 Trace format

Trace events SHOULD include:

monotonic timestamp
process/service
thread
category
event id
duration/value
host profile hash
game build id
VFR profile id

Use compact binary traces for high frequency data.

36.3 Evidence bundle

Every certification run SHOULD produce a deterministic evidence bundle:

cert-result.json
host-capability.json
game-manifest.json
vfr-profile.json
performance.trace
crash/log extracts
screenshots/capture references
shader-cache report
storage report
hash manifest

36.4 Privacy

User telemetry upload MUST be opt-in or clearly scoped to essential crash/security behavior. Local diagnostics MUST remain available without account creation.

37. Windows DevKit-0

The existing development machine becomes DevKit-0, the first reference host for implementation and proof.

Reference machine:

Intel Core Ultra 9 275HX;

32 GB RAM;

NVIDIA GeForce RTX 5070 Ti Laptop GPU, 12 GB VRAM;

Windows 11 Home;

NVMe SSD;

HDMI-connected TV/monitor for console validation.

DevKit-0 is NOT the product hardware target. It is the first host used to prove that the runtime can transform commodity hardware into a console session.

37.1 DevKit-0 milestones

DK0-M1 — Host Doctor

Build dc-hostprof that emits:

OS/build;

CPU topology;

GPU/D3D12 capabilities;

Vulkan capabilities;

VRAM;

system RAM;

storage type and benchmark;

connected display modes;

HDR data;

VRR feasibility;

audio endpoints;

GameInput devices;

Secure Boot/TPM state;

sustained performance probe.

Deliverable: host-capability.json + human-readable report.

DK0-M2 — Console Session

Build:

service bootstrap;

shell process;

controller navigation;

display topology watcher;

audio/input watchers;

safe desktop escape;

game process supervisor.

Acceptance: keyboard/mouse can be removed after session entry and the shell remains fully operable.

DK0-M3 — Minimal native title

A C++ .11g sample title that implements:

lifecycle;

controller input;

HDR/display query;

save transaction;

achievement event;

Guide overlay interaction;

clean exit.

DK0-M4 — Fidelity Lab

Build the flagship VFR proof environment with multiple independently scalable domains and automatic host-aware selection.

DK0-M5 — Certification runner

Automate controller, lifecycle, performance, shader, storage, display, and recovery gates.

38. Fidelity Lab — flagship technical proof

Fidelity Lab must be a beautiful playable microgame, not a benchmark menu.

38.1 Required scene set

One coherent world containing:

hero character close-up;

dense exterior environment;

detailed interior;

dynamic time/weather;

water;

vegetation;

crowds or autonomous agents;

destructible/simulated objects;

volumetrics;

cinematic sequence;

latency-sensitive gameplay segment.

38.2 Required scalable domains

At least:

internal render resolution;

reconstruction;

geometry density;

texture residency;

GI quality;

reflection quality;

shadow quality;

volumetrics;

hair representation;

skin/material complexity;

cloth quality;

particles;

crowd density;

destruction density;

physics solver quality;

animation deformation quality.

38.3 Proof criterion

The same packaged game MUST visibly improve when moved to a stronger host profile without presenting a conventional graphics menu, while meeting its selected experience envelope.

That is the minimum credible proof of the Digital Console thesis.

39. Repository topology

Recommended monorepo:

digital-console/
  CMakeLists.txt
  cmake/
  third_party/

  apps/
    shell/
    overlay/
    settings/

  runtime/
    core/
    host/
    session/
    device/
    display/
    input/
    audio/
    storage/
    package/
    security/
    fidelity/
    saves/
    achievements/
    capture/
    update/
    telemetry/
    compatibility/
    remote/

  hosts/
    windows/
    consoleos/

  sdk/
    include/
    c_abi/
    unreal/
    unity/
    godot/

  formats/
    schemas/
    package/
    traces/

  tools/
    doctor/
    hostprof/
    pack/
    run/
    vfr/
    cert/
    trace/
    display/
    input/
    storage/
    compat/

  samples/
    minimal-title/
    fidelity-lab/

  tests/
    unit/
    integration/
    contract/
    fuzz/
    adversarial/
    performance/
    certification/

  docs/
    canon/
    architecture/
    adr/
    sdk/
    certification/
    research/

  packaging/
    windows/
    consoleos/

  infra/
    schemas/
    test-corpus/
    evidence/

39.1 Dependency rules

runtime/core depends on no host implementation.

host-specific code implements interfaces in runtime/host.

shell depends on public runtime client APIs, not private service internals.

SDK public headers do not include Windows-only or Linux-only types.

compatibility runtime cannot be a dependency of native core services.

VFR cannot depend directly on a specific game engine.

no mutable globals for service ownership; use explicit lifetime/DI.

40. Build system and toolchain

40.1 Core

CMake 3.30+ target-based configuration;

C++23;

MSVC on Windows;

Clang/GCC on ConsoleOS;

vcpkg or CPM/conan only if dependency lockfiles and reproducible versions are enforced;

Ninja for CI/dev builds where useful.

40.2 Testing

Recommended:

Catch2 or GoogleTest for unit/contract tests;

libFuzzer/AFL++-class fuzzing on parsers/package formats;

sanitizers on Linux/Clang builds;

Application Verifier/PageHeap-style Windows diagnostics where relevant;

RenderDoc/PIX/Nsight/RGP/GPA as optional vendor/platform diagnostics;

custom deterministic certification harness as canonical evidence layer.

40.3 Schemas

Machine-readable schemas SHOULD be version-controlled and code-generated where practical.

41. Initial C++ interface boundaries

namespace dc {

struct HostCapabilityRecord;
struct DisplayTopology;
struct AudioTopology;
struct InputTopology;
struct FidelityContract;
struct FidelityDecision;
struct TitleLaunchRequest;
struct TitleSession;

class IHostPlatform {
public:
    virtual ~IHostPlatform() = default;
    virtual HostCapabilityRecord QueryCapabilities() = 0;
};

class IDisplayService {
public:
    virtual ~IDisplayService() = default;
    virtual DisplayTopology CurrentTopology() const = 0;
    virtual Result ApplyValidatedMode(DisplayId, ModeId) = 0;
};

class IFidelityService {
public:
    virtual ~IFidelityService() = default;
    virtual FidelityDecision SelectInitial(
        const FidelityContract&,
        const HostCapabilityRecord&) = 0;
    virtual FidelityDecision Update(const RuntimeTelemetry&) = 0;
};

class ITitleSupervisor {
public:
    virtual ~ITitleSupervisor() = default;
    virtual ResultOr<TitleSession> Launch(const TitleLaunchRequest&) = 0;
    virtual Result Suspend(TitleSession&) = 0;
    virtual Result Resume(TitleSession&) = 0;
    virtual Result Stop(TitleSession&) = 0;
};

}

Interfaces are examples, not frozen ABI signatures. The architectural boundaries are the normative part.

42. Verification gates for the platform itself

The runtime has its own certification independent of games.

Gate P0 — Build health

clean clone builds;

dependency lock valid;

no warnings-as-errors regressions in supported compilers;

unit/contract tests green.

Gate P1 — Controller console contract

all primary shell paths controller-only;

Guide always recoverable;

no input deadlocks;

reconnect works.

Gate P2 — Host topology

display/input/audio/storage changes survive;

no stale device references;

topology events deterministic.

Gate P3 — Game supervision

native launch;

compatibility launch;

crash detection;

exit recovery;

shell foreground recovery;

orphan cleanup.

Gate P4 — Package integrity

corrupt manifest rejected;

corrupt chunk rejected;

invalid signature rejected;

interrupted update retains playable old version;

rollback works.

Gate P5 — VFR

deterministic initial profile selection for identical inputs;

runtime changes remain within certified graph;

no oscillation under stable workload;

emergency downgrade works;

reason codes auditable.

Gate P6 — Performance

shell meets latency/frame pacing target;

service CPU overhead bounded;

idle overhead bounded;

telemetry overhead measured;

capture/background update QoS validated.

Gate P7 — Recovery

shell crash restarts;

noncritical service crash restarts;

game crash returns home;

corrupt state enters safe mode;

system update rollback proven on ConsoleOS.

Gate P8 — Security

parser fuzzing baseline;

sandbox escape surface review;

least privilege validation;

update trust tests;

signature/key-rotation tests.

43. Roadmap: canonical implementation order

Phase 0 — Research freeze + architecture scaffold

Deliver:

this constitution;

ADR set;

schema registry;

repo skeleton;

dependency/licensing audit;

threat model;

certification specification.

Exit condition: no implementation starts without defined ownership/contracts for its subsystem.

Phase 1 — DevKit-0 host foundation

Deliver:

dc-hostprof;

service bootstrap;

Windows host abstraction;

display/input/audio/storage topology;

controller-first shell skeleton;

title supervisor.

Exit condition: console session launches a supervised native sample using controller only.

Phase 2 — Native .11g contract

Deliver:

package schemas;

package builder;

signed manifests;

atomic install/update;

native lifecycle SDK;

saves;

achievements;

overlay;

minimal native title.

Exit condition: native title installs, launches, suspends/checkpoints, resumes, saves, unlocks achievement, exits, updates, and rolls back.

Phase 3 — VFR generation 1

Deliver:

benchmark harness;

host profile derivation;

fidelity schema;

Fidelity Compiler;

runtime governor;

telemetry trace format;

Fidelity Lab v1.

Exit condition: stronger host profile produces a measurably richer certified projection automatically.

Phase 4 — Console-grade rendering/storage

Deliver:

HDR system calibration;

VRR handling;

DirectStorage path;

shader/PSO precache pipeline;

latency telemetry;

60/120 target certification;

capture service.

Exit condition: no known avoidable shader compilation stalls in certification corpus; display/storage transitions validated.

Phase 5 — Appliance/reliability

Deliver:

native suspend/resume robustness;

crash recovery;

service watchdogs;

transactional state journals;

offline mode hardening;

long-duration soak suite.

Exit condition: controller-only multi-hour usage without desktop intervention.

Phase 6 — Compatibility

Deliver:

Steam adapter;

standalone Win32 adapter;

additional store adapters;

compatibility knowledge base;

save-path adapters;

best-effort suspend policies.

Exit condition: representative legacy library feels integrated while remaining visibly classified as compatibility content.

Phase 7 — ConsoleOS prototype

Deliver:

bootable image;

immutable system;

A/B updates;

service port;

embedded compositor;

Vulkan native sample;

input/audio/network stack;

Proton compatibility proof.

Exit condition: boot -> shell -> native title -> suspend/exit -> shell with no desktop environment required.

Phase 8 — Remote Console

Deliver:

hardware encoder abstraction;

pairing;

LAN discovery;

low-latency protocol;

client app;

controller return path;

AV1/HEVC negotiation;

HDR path where supported.

Phase 9 — SDK ecosystem

Deliver:

Unreal plugin;

Unity plugin;

Godot plugin;

public native SDK;

developer docs;

certification kit;

sample titles.

Phase 10 — Platform-grade flagship game

Build an original game whose art, simulation, streaming, and lifecycle were authored around canonical scalable fidelity from day one.

44. Anti-goals: what we explicitly do not build

a Steam skin presented as a new console;

a browser UI that cannot survive OS/device lifecycle changes;

a proprietary graphics API without demonstrated necessity;

a cloud-required console;

a platform that requires paid AI/API subscriptions to function;

an “AI optimizer” that changes unknown settings without certification;

a single scalar GPU-tier table masquerading as capability qualification;

a graphics benchmark with no real game lifecycle;

an OS fork before the Windows host proves the platform contracts;

custom crypto when audited standard update/signature designs exist;

mandatory kernel anti-cheat as platform architecture;

direct launcher integrations that violate store/DRM terms;

claims of universal Quick Resume for arbitrary legacy games before it is proven.

45. Canonical acceptance test: when does a Digital Console exist?

The first real Digital Console exists when the following is reproducibly true on DevKit-0:

User starts the console session.

TV/display is identified and correctly configured.

Controller becomes primary navigation device.

Host is qualified into machine-readable capability profiles.

Shell operates without keyboard/mouse.

A signed .11g title is installed through the package service.

The runtime verifies and launches it under the native lifecycle contract.

VFR selects a measured, certified fidelity profile.

The title renders with stable target frame behavior.

Guide overlay works.

Save transaction works.

Achievement event works locally.

Display/audio/controller changes are survived.

Title suspends/checkpoints and resumes through the platform contract.

Title exits or crashes and the shell deterministically recovers.

A package update can be interrupted without corrupting the playable version.

No ordinary gameplay step requires the Windows desktop.

Moving the same native title to a stronger qualified host produces a higher validated fidelity projection with no manual graphics configuration.

Passing 1–17 proves a real software-defined console runtime.

Passing 18 proves the central living fidelity / hardware-independent generation thesis.

46. R&D frontier after the core platform is proven

These are legitimate research programs, but remain downstream of the core:

46.1 Perceptual VFR models

Train/derive scene-aware utility models that estimate visible benefit per millisecond rather than relying only on handcrafted weights.

46.2 Neural asset/runtime representations

Investigate neural texture compression, neural materials, learned animation/deformation, radiance representations, and neural reconstruction as optional fidelity domains.

46.3 Cross-device session migration

Title-cooperative checkpoint format that can restore on another host with different GPU resources while preserving gameplay state.

46.4 Shared compiled-content caches

Content-addressed shader/derived-data caches reusable across compatible hosts where driver/runtime identity safely permits it.

46.5 Deterministic remote rendering split

Explore splitting simulation and rendering across local/remote compute without turning the platform into a cloud-required service.

46.6 Display-native rendering

Future displays/light-field/AR surfaces could expose capability profiles beyond conventional flat HDMI targets.

47. Architecture decision records required immediately

Create these ADRs before major implementation:

ADR-0001  Software-defined console platform identity
ADR-0002  Windows Host Edition vs ConsoleOS separation
ADR-0003  C++23 service-oriented runtime architecture
ADR-0004  IPC schema and transport
ADR-0005  .11g package content-addressed format
ADR-0006  Package signing and TUF-inspired update trust
ADR-0007  Host capability vector and profile versioning
ADR-0008  VFR offline compiler + runtime governor
ADR-0009  D3D12 Windows / Vulkan ConsoleOS graphics strategy
ADR-0010  GameInput + SDL3 input strategy
ADR-0011  Transactional saves
ADR-0012  Native suspend/resume contract
ADR-0013  Compatibility guest boundary
ADR-0014  HDR/VRR display policy
ADR-0015  DirectStorage/GDeflate Windows IO path
ADR-0016  ConsoleOS immutable/A-B update architecture
ADR-0017  Local-first identity/cloud-provider abstraction
ADR-0018  Telemetry/evidence format and privacy
ADR-0019  SDK ABI/versioning policy
ADR-0020  Certification policy/versioning

48. Current research basis — September 2026

The architecture above is grounded in current platform capabilities and constraints, including:

Microsoft — Shell Launcher overview. Windows Shell Launcher can replace Explorer but is limited to Enterprise, Education, and IoT Enterprise editions. This supports the decision to treat ordinary Windows Home/Pro as a hosted console session and keep ConsoleOS as the long-term appliance host.

Microsoft — GameInput documentation (updated 2026). GameInput provides a unified, low-latency API across gamepads and other controller classes, with haptics, sensors, force feedback, callbacks, and low-level device access. This supports it as the primary Windows native input backend.

Microsoft — DXGI flip-model and VRR guidance. Modern flip-model presentation offers performance/power/features advantages, and Windows VRR support is exposed through modern swap-chain presentation semantics. This supports a modern borderless/flip presentation strategy rather than assuming legacy exclusive-fullscreen behavior.

Microsoft — AdvancedColorInfo. Windows exposes HDR/WCG/high-precision display state and quantitative luminance/primary metadata. This supports system-level display profiling and calibration.

Microsoft — DirectStorage 1.3 / DirectStorage 1.1 GDeflate. DirectStorage provides GPU decompression and high-throughput asset IO; GDeflate uses parallel 64 KiB tile structure. This supports package variants and an IO layer that treats storage capability as a console performance dimension.

Khronos — Vulkan Profiles. Profiles formalize machine-readable sets of features/extensions/limits and reduce runtime fragmentation. This directly informs the Digital Console host-profile approach, extended beyond graphics into whole-system capability.

Valve — gamescope. Embedded gamescope can use DRM/KMS direct flips with fewer copies/latency and exposes embedded display, HDR, adaptive-sync, and frame-limit behavior. This validates the ConsoleOS compositor direction.

Microsoft GDK — suspend/resume and title stability requirements. Xbox lifecycle guidance demonstrates that networking and title state must explicitly participate in suspend/resume and that title stability after suspend is a certification concern. This supports making lifecycle cooperation mandatory for native titles.

Microsoft — Secure Boot / TPM / Measured Boot. Windows can establish and attest boot trust using UEFI, TPM measurements, and measured boot. This informs Trusted Competitive Mode and host trust posture without making online attestation mandatory for normal offline use.

Microsoft — AppContainer / Win32 app isolation. Windows provides process/resource isolation and newer Win32 sandbox APIs. These are candidates for native-title containment, subject to graphics/middleware compatibility testing.

The Update Framework (TUF). TUF defines a mature update-security threat model and current specification versions. The console should reuse this model for repository metadata/signing rather than inventing update trust.

RAUC / systemd boot assessment concepts. Open Linux update systems provide fail-safe A/B/rollback patterns, signed update bundles, trial boots, and automatic fallback. This informs ConsoleOS update architecture.

HDMI Forum — HDMI 2.1/2.2. Current HDMI specifications provide VRR/ALLM/QFT and higher-bandwidth 2.2 transport up to 96 Gbps plus LIP. These features are useful opportunities but must remain capability-detected rather than assumed.

CRIU/ROCm GPU checkpoint work. GPU-aware checkpoint/restore exists in constrained environments and continues to evolve, proving the concept is real while also demonstrating why universal arbitrary-game Quick Resume remains research-grade.

Unreal Engine 5.8 rendering/PSO work. Current engine work continues to optimize advanced rendering and pipeline caching for real frame budgets, supporting the requirement that shader/PSO behavior be part of console certification rather than accepted as uncontrolled PC-style hitching.

49. Research conclusions that are now canonical

Windows first is correct, Windows forever is not.

ConsoleOS must implement the same contracts, not fork the platform semantics.

A capability vector + versioned profiles is superior to a single hardware tier.

VFR must operate on title-authored scalable domains and validated profiles.

Native title cooperation is required for true suspend/resume and future portability.

Legacy compatibility cannot define native console guarantees.

Storage, display, latency, and thermal behavior are first-class console capabilities.

Future fidelity requires preserved semantic/scalable representations, not magical reconstruction.

Certification is the mechanism that restores deterministic console quality on variable hardware.

The highest-fidelity product is the entire platform contract, not merely higher graphics settings.

50. Immediate next engineering package

The next implementation package SHOULD contain:

/docs/canon/DC-CANON-001.md                 <- this document
/docs/adr/ADR-0001..0020.md
/formats/schemas/host-capability.schema.json
/formats/schemas/game.schema.json
/formats/schemas/fidelity.schema.json
/formats/schemas/lifecycle.schema.json
/runtime/core/                              <- interfaces only
/hosts/windows/                             <- capability discovery
/tools/hostprof/                            <- first executable
/tests/contract/                            <- schema/interface tests

The first executable built should be dc-hostprof, not the shell. It establishes the machine-readable truth upon which the console will make every later hardware/display/fidelity decision.

51. Definition of success

The Digital Console project succeeds when “console generation” is no longer synonymous with one immutable consumer box.

A player owns a persistent console identity and native game library. A qualified host supplies compute. A display supplies presentation capability. The runtime negotiates the two. Native games expose scalable, validated representations. Certification ensures that hardware variety does not become user-facing chaos.

The end-state is:

                  DIGITAL CONSOLE
                         |
                 persistent identity
                         |
        +----------------+----------------+
        |                |                |
      Host A           Host B           Host C
      2026             2029             2033
        |                |                |
        +----------------+----------------+
                         |
                 SAME NATIVE GAME
                         |
                  scalable canon
                         |
       +-----------------+-----------------+
       |                 |                 |
   projection A      projection B      projection C

The console remains the console.

The hardware underneath it becomes replaceable infrastructure.

That is the canonical Digital Console architecture.