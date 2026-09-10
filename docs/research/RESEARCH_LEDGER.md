# RESEARCH_LEDGER.md — 11vated Digital Console

Living ledger. Every entry: source · date accessed · current version · relevant
capability · limitation · architectural consequence · confidence.

Verification pass: **2026-09-06** (fresh, this session). Entries marked
`[carried]` were verified in DC-CANON-001 §48 research basis and are re-verified
when implementation depends on them.

---

## 1. Windows shell ownership

- **Source:** Microsoft Learn — Shell Launcher Overview
  (learn.microsoft.com/en-us/windows/configuration/shell-launcher/)
- **Accessed:** 2026-09-06 · **Version:** current (v1/v2 docs, updated 2025–2026)
- **Capability:** Shell Launcher v1 replaces `Explorer.exe` with a Win32 shell;
  v2 replaces it with `CustomShellHost.exe` which can host Win32 or UWP apps.
- **Limitation:** Restricted to Enterprise / Education / IoT Enterprise editions;
  not available on Windows Home/Pro. Shell does not run above the lock screen.
- **Architectural consequence:** Confirms canon §6: Windows Host Edition is a
  *console session* host (auto-start, borderless, supervised), not a bare-metal
  appliance. Shell Launcher is a *Lab Shell Edition* validation tool only.
  ConsoleOS remains the true appliance implementation.
- **Confidence:** HIGH (primary vendor doc).

## 2. Xbox Mode / Full Screen Experience (market validation)

- **Source:** Xbox Wire — "Xbox Mode Begins Rolling Out to Players on Windows 11
  PC" (news.xbox.com, 2026-04-30); Microsoft Support — "Windows Gaming: Full
  screen experience" (2025-10-20)
- **Accessed:** 2026-09-06
- **Capability:** Controller-optimized full-screen launcher UI ("Xbox mode")
  rolling out from handhelds to desktop Windows 11 PCs; Windows starts the
  gaming home app as the launcher, games run full screen.
- **Limitation:** Aggregated library UI only — it is not a runtime, package
  format, capability qualification, or certification system.
- **Architectural consequence:** Market has validated controller-first Windows
  sessions, but aggregation alone (Playnite, Xbox Mode) is explicitly NOT the
  invention (blueprint §23). Our differentiators remain host qualification, .11g,
  VFR, certification.
- **Confidence:** HIGH (vendor announcements).

## 3. Input — GameInput

- **Source:** Microsoft GDK docs — GameInput introduction
  (learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/input-overview?view=gdk-2604);
  GameInput PC v2.2 update (developer.microsoft.com, 2025-09-16); NuGet
  `Microsoft.GameInput` 3.x
- **Accessed:** 2026-09-06
- **Capability:** Unified low-latency gamepad/arcade/racing/flight/touch input on
  DMA architecture; force feedback, haptics (incl. audio-driven haptic device
  info in v2.2), motion/sensors, callbacks, low-level device access.
- **Limitation:** Windows/GDK ecosystem; part of the GameInput redistributable
  service. Not a ConsoleOS path.
- **Architectural consequence:** Confirms GameInput as primary Windows input
  backend (canon §12.2) with SDL3 as portable fallback; semantic haptic channels
  are feasible now.
- **Confidence:** HIGH.

## 4. Input — SDL3

- **Source:** libsdl.org / SDL3 API docs (SDL_Gamepad, SDL_Haptic, SDL_Sensor);
  SDL_GameControllerDB (community, SDL2+SDL3 compatible)
- **Accessed:** 2026-09-06
- **Capability:** Stable SDL3 with gamepad mapping database, haptics, sensors;
  portable across Windows/Linux.
- **Limitation:** Vendor-proprietary extensions (adaptive triggers, advanced
  haptics) vary; community DB coverage lags new devices.
- **Architectural consequence:** SDL3 is the portable/fallback abstraction and
  the ConsoleOS input path. Semantic device model (canon §12.3) must degrade
  gracefully per backend.
- **Confidence:** HIGH.

## 5. Display — DXGI flip model + VRR

- **Source:** Microsoft Learn — "For best performance, use DXGI flip model";
  "DXGI flip model"; "Variable refresh rate displays" (Win32/Direct3D articles)
- **Accessed:** 2026-09-06
- **Capability:** Flip presentation reduces resource load/latency, enables
  present statistics; VRR via swap-chain flags (tearing allowed requirement);
  waitable swap chains reduce latency.
- **Limitation:** VRR requires allow-tearing + compatible path; behavior differs
  on legacy bitblt model.
- **Architectural consequence:** Confirms modern borderless flip presentation
  policy for shell and native titles (canon §11.2, §15). Presentation policy is
  capability-detected, never assumed.
- **Confidence:** HIGH.

## 6. Display — HDR / Advanced Color

- **Source:** Microsoft Learn — "Use DirectX with Advanced Color on high/standard
  dynamic range displays"; `AdvancedColorInfo` (WinRT Windows.Graphics.Display)
- **Accessed:** 2026-09-06
- **Capability:** FP16 advanced-color composition; per-display HDR/WCG state,
  luminance (min/max/maxFullFrame), primaries, white point, SDR white level,
  HDR metadata format support queries.
- **Limitation:** Metadata availability varies per display/driver; AC requires
  explicit activation and state-change handling.
- **Architectural consequence:** System-level HDR calibration flow + display
  profile contract (canon §11.3) is implementable now with truthful fallback.
- **Confidence:** HIGH.

## 7. Storage — DirectStorage

- **Source:** DirectX Developer Blog — "DirectStorage 1.4 release adds support
  for Zstandard" (devblogs.microsoft.com, 2026-03-11); NVIDIA GDeflate blog;
  NuGet `Microsoft.Direct3D.DirectStorage` 1.4.0-preview2 (1.3 stable)
- **Accessed:** 2026-09-06
- **Capability:** Batched high-throughput IO, GPU decompression; GDeflate 64 KiB
  tiles; 1.4 preview adds Zstd + Game Asset Conditioning Library.
- **Limitation:** 1.3 stable ships GDeflate GPU decompression; 1.4 (Zstd path)
  is preview. GPU decompression error reporting has gaps in preview builds.
- **Architectural consequence:** Package codecs (canon §14.3) — Zstd portable +
  GDeflate for Windows GPU decompression + raw — remain correct. 1.4 preview is
  tracked but not a baseline dependency until stable.
- **Confidence:** HIGH. **Update vs canon §48:** DirectStorage 1.4 preview with
  Zstd is new since canon drafting; recorded as an additive finding (no
  architectural conflict).

## 8. Graphics profiles — Vulkan Profiles

- **Source:** Khronos — Vulkan Profiles guide/docs.vulkan.org; KhronosGroup/
  Vulkan-Profiles; Khronos press (2022, ecosystem current 2026)
- **Accessed:** 2026-09-06
- **Capability:** Machine-readable profiles formalizing sets of features/
  extensions/limits; tooling to verify device support against a profile.
- **Limitation:** GPU-API-scoped; no CPU/storage/display/thermal semantics.
- **Architectural consequence:** Validates the profile pattern (canon §10.3) and
  motivates extending capability profiles beyond the GPU to whole-console
  qualification (blueprint §4).
- **Confidence:** HIGH.

## 9. ConsoleOS compositor precedent — gamescope

- **Source:** ValveSoftware/gamescope (GitHub README: direct DRM/KMS flips,
  fewer copies, HDR, adaptive-sync); issue tracker for HDR/VRR state
- **Accessed:** 2026-09-06
- **Capability:** Embedded session compositor operating directly against DRM/KMS
  with direct flips; SteamOS3 session proven on shipping hardware (Steam Deck).
- **Limitation:** NVIDIA DRM/KMS path historically rougher (atomic flip issues);
  VRR/HDR behavior version-dependent; not a stable public API for third parties.
- **Architectural consequence:** Confirms ConsoleOS approach (canon §6.3) is
  precedented; ConsoleOS copies the *concepts* (embedded compositor, direct
  flips) rather than depending on gamescope as a component.
- **Confidence:** HIGH for direction; MEDIUM for NVIDIA embedded-mode specifics.

## 10. Update trust — TUF

- **Source:** The Update Framework — specification latest
  (theupdateframework.github.io/specification/latest/, rev 2026); tuf.io
- **Accessed:** 2026-09-06
- **Capability:** Formal threat model + roles (root/targets/snapshot/timestamp)
  defending against repository compromise, rollback/freeze, endless-data,
  mix-and-match, malicious mirrors.
- **Limitation:** Repository-metadata security, not transport or payload
  compression; client implementation effort is real.
- **Architectural consequence:** Canon §30.2 confirmed: package trust uses a
  TUF-inspired model; do not invent update crypto.
- **Confidence:** HIGH.

## 11. Atomic OS updates — RAUC / systemd boot assessment

- **Source:** rauc.io docs (integration, A/B slots, boot-attempt counting,
  trial boots); bootlin RAUC-on-RPi5 (2025); embedded-artists RAUC guide
- **Accessed:** 2026-09-06
- **Capability:** Atomic A/B slot updates, signed bundles, boot-attempt counting
  with automatic rollback (default 3 attempts), mark-good flow.
- **Limitation:** Linux-only machinery; slot layout/board integration required.
- **Architectural consequence:** Canon §31.2 confirmed: ConsoleOS reuses RAUC/
  systemd boot-assessment concepts rather than a custom updater.
- **Confidence:** HIGH.

## 12. Quick Resume research boundary — CRIU + GPU checkpointing

- **Source:** NVIDIA blog "Checkpointing CUDA Applications with CRIU" (2024-07)
  + NVIDIA/cuda-checkpoint (GitHub); criu-amdgpu-plugin man page; LWN "A
  parallel path for GPU restore in CRIU" (2025-06-17); CRIUgpu (arXiv
  2502.16631, 2025-02)
- **Accessed:** 2026-09-06
- **Capability:** CPU process trees checkpoint/restore in userspace; vendor GPU
  plugins exist (CUDA paravirtualized checkpointing; AMDGPU KFD/VRAM state) in
  constrained environments.
- **Limitation:** GPU state is hardware/driver/version-dependent; restore
  requires compatible GPU topology; arbitrary Win32/DX12 game GPU state is NOT
  checkpointable — remains research-grade (canon §3.3).
- **Architectural consequence:** Confirms Quick Resume tiers (canon §22.3):
  QR1/QR2 launch-critical via title cooperation; QR3 disk-backed native
  snapshots later; universal legacy-game QR stays research-grade.
- **Confidence:** HIGH.

## 13. HDMI features — 2.1/2.2

- **Source:** HDMI Forum — HDMI 2.2 specification release (2025-01-06 /
  hdmiforum.org); hdmi.org/spec/hdmi2 technology overview
- **Accessed:** 2026-09-06
- **Capability:** HDMI 2.1: 4K120, VRR, ALLM, QFT. HDMI 2.2: 96 Gbps "Ultra96"
  transport, Latency Indication Protocol (LIP), retains VRR/ALLM/QFT.
- **Limitation:** Feature exposure depends on GPU driver/path and display; PC
  GPUs expose none of CEC universally.
- **Architectural consequence:** Confirms canon §11.4–11.5: gaming HDMI features
  are capability-detected opportunities; CEC is optional and never a dependency.
- **Confidence:** HIGH.

## 14. Trust posture — Secure Boot / TPM / measured boot

- **Source:** Microsoft Learn — "How Windows uses the TPM" (measured boot),
  Windows 11 security book (system security), Device Health Attestation,
  OEM Secure Boot
- **Accessed:** 2026-09-06
- **Capability:** UEFI Secure Boot verification; TPM 2.0 PCR measurements of
  boot chain; measured boot audit trail; remote attestation (DHA).
- **Limitation:** Attestation is infrastructural; Home editions lack enterprise
  attestation services; not a gameplay-path dependency.
- **Architectural consequence:** Trusted Competitive Mode posture (canon §30.4)
  is implementable as *evidence*, while offline core (C9) never requires it.
- **Confidence:** HIGH.

## 15. Sandbox — AppContainer / Win32 app isolation

- **Source:** Microsoft Learn — AppContainer isolation; Win32 app isolation
  overview; Windows 11 Security Book — Application Isolation; microsoft/
  win32-app-isolation (GitHub)
- **Accessed:** 2026-09-06
- **Capability:** LowBox token AppContainer sandbox; Win32 app isolation as
  "default isolation standard" built on AppContainer + base system process.
- **Limitation:** Graphics/middleware compatibility under AppContainer is
  unproven for high-performance games (canon §30.3 explicitly requires testing,
  not assumption).
- **Architectural consequence:** Native-title containment begins with
  least-privilege + Job Objects; AppContainer/AppIsolation evaluated per title
  architecture with contract tests.
- **Confidence:** HIGH.

## 16. PSO/shader precache context [carried]

- **Source:** Unreal Engine docs (PSO creation can take 100ms+; PSO precaching)
- **Architectural consequence:** Shader/PSO behavior is a certification gate
  (canon §20, §34.7), not an accepted PC-style hazard.
- **Confidence:** HIGH.

## 17. Engine/platform context [carried]

- **Source:** Unreal Engine rendering docs (Nanite/Lumen/MegaLights scalability
  in frame budgets); Microsoft Xbox Velocity Architecture docs; Playnite
  (aggregation precedent); Sunshine/Moonlight (LAN game streaming incl. HDR,
  HEVC/AV1, multi-controller)
- **Architectural consequence:** Blueprint §24 remote-console direction and
  §7 canonical scalable representation are grounded in shipping technology.
- **Confidence:** HIGH.

---

## Ledger conclusions (2026-09-06)

1. Canon §48 research basis **holds**. No architectural contradictions found.
2. Two additive findings recorded: DirectStorage 1.4 preview (Zstd), Xbox Mode
   desktop rollout. Neither changes canonical architecture; both strengthen it.
3. Research-grade boundaries (universal GPU checkpointing, universal CEC,
   arbitrary-game auto-optimization) remain correctly classified.
4. Phase 0 → Phase 1 build order (canon §50: `dc-hostprof` first) proceeds.

---

## 18. DirectStorage 1.4 + GACL (re-verified, continuation pass)

- **Source:** devblogs.microsoft.com/directx/directstorage-1-4-release-adds-support-for-zstandard · accessed 2026-09-06
- **Technology/version:** DirectStorage 1.4 + Game Asset Conditioning Library (GACL), **public preview** (announced GDC 2026, Mar 2026)
- **Finding:** 1.4 adds Zstandard compression and GACL conditioning. Still
  preview channel; 1.3 + GDeflate remains the production-safe baseline.
- **Limitation:** Preview APIs; not to be made mandatory platform dependencies.
- **Architectural consequence:** DK0-M1E models `zstd_status = preview`;
  stable path stays 1.3 + GDeflate. Profile requirements reference only stable
  capabilities. Confirms prior ledger entry §11 direction.
- **Canon impact:** None (additive). Confidence: HIGH.

## 19. Advanced Shader Delivery (ASD) (new, material)

- **Source:** devblogs.microsoft.com/directx/advanced-shader-delivery-whats-new-at-gdc-2026 · developer.microsoft.com GDC 2026 article · accessed 2026-09-06
- **Technology/version:** Advanced Shader Delivery (PSDB — precompiled shader
  database, delivered via storefronts; Stats API for developers)
- **Finding:** Windows ecosystem now validates storefront-delivered,
  hardware-targeted precompiled shaders as first-class installation content
  (up to ~90% reported load-time reduction in supported scenarios; vendor
  driver support confirmed by AMD 26.6.1, Intel statements, NVIDIA RTX).
- **Limitation:** Microsoft/Xbox storefront-centric today; not an open contract.
- **Architectural consequence:** Elevates our planned `.11g` shader-artifact
  model (`shaders/source-metadata`, `d3d12`, `vulkan`, host-independent,
  resolved-cache keyed by GPU+driver identity) from "future" to
  "ecosystem-validated first-class concept". The Digital Console implements its
  own open/local PSDB-equivalent resolution + invalidation (game update,
  driver change, GPU change) independent of Microsoft infrastructure.
- **Canon impact:** Strengthens canon §20/§34.7 shader pipeline direction. No
  contradiction. Confidence: HIGH.

## 20. GameInput NuGet (re-verified, continuation pass)

- **Source:** nuget.org/packages/Microsoft.GameInput · learn.microsoft.com
  GDK input-overview (gdk-2604) · accessed 2026-09-06
- **Technology/version:** Microsoft.GameInput 3.5.268 (May 2026 release 3.4+
  added raw HID reports, gyro improvements)
- **Finding:** GameInput remains the current unified input API; NuGet
  distribution is the Windows consumption path; exposes gamepads, keyboards,
  mice, wheels, flight sticks, arcade sticks, raw devices, sensors, haptics,
  force feedback, arrival/removal notifications.
- **Limitation:** Redistributable runtime required on end-user machines.
- **Architectural consequence:** DK0-M1H input backend design (GameInput
  primary, SDL3 portable, XInput fallback) stands. Backend loads gameinput.dll
  dynamically; absence = honest `unavailable`, XInput floor remains.
- **Canon impact:** None (ADR-0010 confirmed). Confidence: HIGH.

## 21. VRR presentation-path qualification (verified technique)

- **Source:** learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays · DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING docs · accessed 2026-09-06
- **Technology/version:** DXGI flip-model + ALLOW_TEARING + IDXGIFactory5::
  CheckFeatureSupport(CreateSwapChainForHwnd allows tearing)
- **Finding:** VRR operation requires: (a) OS VRR enabled, (b) flip-model
  swap chain, (c) tearing-supported factory/swap chain, (d) Present(0,
  DXGI_PRESENT_ALLOW_TEARING) sync-interval 0. Feature discovery alone does not
  prove the path works.
- **Limitation:** None material for our probe design.
- **Architectural consequence:** DK0-M1I dc-displayprobe implements the
  three-state model (capable / compatible / actively-proven) — never collapse
  to one boolean (C10). IDXGIOutput6::GetDesc1 (DXGI_OUTPUT_DESC1,
  dxgi1_6.h) supplies quantitative HDR: min/max/maxFullFrame luminance,
  primaries, white point, SDR white level, ColorSpace.
- **Canon impact:** ADR-0014 confirmed; implemented this slice. Confidence: HIGH.## 23. DXC TraceRay "toolchain gap" (DISPROVEN 2026-09-07 — investigation timeline)

- **Source:** Local toolchain verification on DevKit-0; microsoft/DirectXShaderCompiler
  releases (v1.8.2502 Windows SDK 10.0.26100; v1.9.2607 GitHub release build
  1.9.0.5402) · utils/hct/gen_intrin_main.txt @main · accessed 2026-09-07
- **Original claim (WRONG):** "`TraceRay` is undeclared at the HLSL-name level
  in BOTH DXC builds tested, across lib_6_3/6_5/6_9, all `-HV` versions."
- **DISPROVEN:** `TraceRay` compiles and validates fine in DXC 1.9.5402
  (lib_6_3, full raygeneration/miss/closesthit library, dxv passes). The prior
  session's repros used a **7-argument call**; the actual HLSL signature is
  `TraceRay(AS, RayFlags, InstanceInclusionMask, ContributionToHitGroupIndex,
  MultiplierForGeometryContribution, MissShaderIndex, RayDesc, payload)` —
  **8 parameters with the RayDesc before the payload**
  (gen_intrin_main.txt line 311). Every failed test omitted the RayDesc, and
  DXC reports the missing-declaration diagnostic for arity mismatches on
  undeclared names, which was misread as "the intrinsic doesn't exist".
  Compounding factor: RT stages must be compiled as **library targets**
  (lib_6_3+), not compute entry points — the library-target requirement was
  honored, the signature was not.
- **Corrected truth:** full DXR pipeline authoring IS possible on this
  toolchain today. `gpu.rt_pipeline` is implementable immediately.
- **Architectural consequence:** `.11g` native titles can use full DXR
  pipelines with project-vendored DXC. The ray-query inline benchmark remains
  valuable as a separate domain (`gpu.rt_inline`), not a substitute.
- **Canon impact:** Supersedes the §23 toolchain limitation; ADR-0021 scope
  note updated. Confidence: HIGH (reproducible both ways).

## 23a. DevKit-0 preview-driver presentation/RT instability (2026-09-07 — investigation timeline)

- **Source:** DevKit-0 runtime evidence 2026-09-07 (driver 32.0.16.1078,
  NVIDIA RTX 5070 Ti Laptop, Windows build 26200)
- **Finding (as observed then):** (a) `gpu.rt` ray-query dispatches removed
  the device asynchronously (INVALID_CALL 0x887A0001) after successful AS
  builds/warmups — content-independent; (b) raster-shaped submissions wedged
  the per-process adapter context for the NEXT bench (ordering fix: raster
  last); (c) ALL DXGI swapchain creation rejected desc-independently (HWND
  and composition, both adapters) while pure D3D12 work succeeded.
- **Status:** classification uncertain (driver state vs environmental);
  hypothesis "pending reboot" MEDIUM confidence. To be re-tested once per
  session start with full diagnostics; do not hammer a broken driver.
- **RESOLVED 2026-09-10 (post-reboot controlled re-test):** claim (c) was
  OUR bug — dc-displayprobe passed `ID3D12Device*` where
  `CreateSwapChainForHwnd` requires the **command queue**; every rejection
  was `DXGI_ERROR_INVALID_CALL` (0x887A0001, commonly misread as ACCESS_LOST
  0x887A0026). With the queue fix, flip-discard:3:tearing is created and
  presents successfully (tearing + vsync) on the dGPU: **VRR actively proven,
  presentation never driver-blocked.** Claims (a)/(b) remain real: RT
  ray-query dispatch still device-removes asynchronously (0x887A0005) after
  clean reboot, same driver — `gpu.rt_inline` stays BLOCKED_DRIVER. Ledger
  rule confirmed: HRESULT codes must be resolved by name before diagnosis.
  Confidence: HIGH (both facts reproduced in fresh processes).

## 25. CreateStateObject DXIL-library rejection (2026-09-07 — BLOCKED_ENVIRONMENT)

- **Source:** DevKit-0 runtime evidence 2026-09-07 (Windows build 26200,
  d3d12.dll/D3D12Core.dll 10.0.26100.8972)
- **Finding:** `ID3D12Device5::CreateStateObject` returns `E_INVALIDARG`
  (0x80070057) for ANY state object containing a **DXIL library subobject**,
  while the SAME call with only a global-root-signature subobject succeeds
  (both COLLECTION and RAYTRACING_PIPELINE types). Isolated via standalone
  bisect tools: (a) valid signed lib_6_3 libraries from DXC 1.9.5402 AND
  1.8.2502 both rejected; (b) garbage bytes rejected identically (arg-level
  rejection before content validation); (c) **WARP adapter also rejects** —
  this is the OS D3D12 runtime, not the NVIDIA preview driver; (d) RT tier
  1.2 correctly reported via CheckFeatureSupport; ray-query inline dispatches
  (no state object) reach the GPU (then hit the separate §23a removal defect).
- **Limitation:** Windows 26200 Dev-channel OS runtime. Not fixable in our
  code; likely fixed by a Windows update or release-channel change.
- **Architectural consequence:** `gpu.rt_pipeline` implementation is complete
  (state object, hit group, shader table, DispatchRays, timestamps) and is
  classified **BLOCKED_ENVIRONMENT** at the CreateStateObject boundary.
  `gpu.rt_inline` remains the measurable RT domain when §23a clears.
- **Canon impact:** No contradiction. Confidence: HIGH (reproducible,
  adapter-independent, content-independent).

## 25. Vulkan loader discovery — DevKit-0 verified (2026-09-07)

- **Source:** Local runtime verification; KhronosGroup/Vulkan-Headers vulkan_core.h
  (raw.githubusercontent.com, accessed 2026-09-07); Vulkan loader registry layout
  (KhronosGroup/Vulkan-Loader docs)
- **Finding:** vulkan-1.dll (loader 1.4.341) is present on DevKit-0 but the Khronos
  registry keys (`HKLM\SOFTWARE\Khronos\Vulkan\Drivers`) are empty — the loader
  discovers NVIDIA ICDs through other standard paths. Dynamic loading through
  vkGetInstanceProcAddr works without any SDK installation. RTX 5070 Ti enumerates
  with VK_KHR_ray_query, VK_EXT_mesh_shader, 6 queue families, 2 memory heaps.
- **Limitation:** Hand-declared Vulkan structs in probe_vulkan.cpp are a known
  fragility — an undersized VkPhysicalDeviceProperties stub caused a stack-cookie
  abort (0xC0000409) until padded generously. Long-term: consume the real headers
  at build time (SDK optional at build, not runtime).
- **Architectural consequence:** The host-capability model is now proven
  backend-independent (D3D12 + Vulkan both discovered on the same host). Vulkan
  Profiles compatibility assessment deferred to ConsoleOS renderer work.
- **Canon impact:** ADR-0009 unchanged. Confirms §11 (Vulkan discovery) complete.
- **Confidence:** High (measured on DevKit-0).

## 26. GameInput runtime — DevKit-0 verified (2026-09-07)

- **Source:** Microsoft.GameInput NuGet 3.5.270 (GAMEINPUT_API_VERSION 3, MIT license);
  learn.microsoft.com GameInput for PC and console with NuGet (view gdk-2604,
  accessed 2026-09-07); winget install Microsoft.GameInput (redist 4.3.0.218 installed
  on DevKit-0 with user approval)
- **Finding:** In-box runtime was 2309.0.9168.26100; the redistributable updates
  GameInputRedist.dll to 4.3.0.218. Both export GameInputCreate, but **statically
  linking GameInput.lib and calling the imported GameInputCreate is the only safe
  path**: dynamically LoadLibrary-ing GameInputRedist.dll and calling the exported
  GameInputCreate crashed (segfault) inside RegisterDeviceCallback with blocking
  enumeration on DevKit-0. The static import pins the correct header/runtime pairing.
- **Limitation:** v3 IGameInputDevice does not expose battery state (v0-only API).
  Devices surface through RegisterDeviceCallback(GameInputBlockingEnumeration),
  not through reading history (no readings exist before first input).
- **Architectural consequence:** GameInput is now the primary Windows input backend
  (XInput floor retained for machines without the redist). System-button ownership
  (Guide/Share) via RegisterSystemButtonCallback is available for DK0-M2 Guide
  routing; SetFocusPolicy exists on IGameInput for foreground/background policy.
- **Canon impact:** ADR-0010 (GameInput primary) confirmed implementable; no change.
- **Confidence:** High (measured on DevKit-0, including the crash repro).

## 27. SDL3 fallback backend — DevKit-0 verified (2026-09-07)

- **Source:** libsdl-org/SDL release-3.4.16 VC development package
  (github.com/libsdl-org/SDL/releases, accessed 2026-09-07); SDL3 API docs
  (wiki.libsdl.org, gamepad enumeration via SDL_GetGamepads + properties API)
- **Finding:** SDL3 3.4.16 dynamically loads and SDL_INIT_GAMEPAD initializes
  cleanly on DevKit-0. Gamepad enumeration uses SDL_GetGamepads (instance-ID
  array, SDL_free by caller); rumble capability via
  SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN on the gamepad properties.
- **Limitation:** SDL3.dll is not OS-distributed; it must ship with the
  platform build. Dedup policy (directive §18): SDL3 enumerates only when
  GameInput found nothing, so no duplicate device records are possible in the
  current flow.
- **Architectural consequence:** The host input stack now has three honest
  tiers: GameInput (primary) → SDL3 (portable fallback) → XInput (legacy
  floor). Backend independence of the semantic input model is proven.
- **Canon impact:** ADR-0010 unchanged; SDL3 role confirmed as fallback only.
- **Confidence:** High (SDL_Init verified on DevKit-0).
