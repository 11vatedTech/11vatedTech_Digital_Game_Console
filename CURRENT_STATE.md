# CURRENT_STATE.md — 11vated Digital Console

Last updated: 2026-09-12 (DK0-M4 fidelity stack: contract/compiler/governor/loader/service + packaged-title integration)

## Product purpose

Software-defined digital game console: a persistent, controller-first gaming
platform whose identity, runtime, native game contract (.11g), fidelity runtime
(VFR), and services are independent of the physical hardware underneath it.
Canon: `docs/canon/DC-CANON-001.md` + `docs/canon/DC-BLUEPRINT-001.md`.

## Architecture

Monorepo per canon §39. `runtime/core` (host-agnostic C++23 models: capability
`/2`, qualification, strict JSON parser, deterministic serializers) →
`runtime/host` (`IHostPlatform` boundary) → `hosts/windows` (capability probes
+ `bench/` measured qualification layer) → `tools/hostprof` (`dc-hostprof`,
Host Doctor) + `tools/displayprobe` (`dc-displayprobe`, presentation-path
probe). Schema registry in `formats/schemas/` (dc.host-capability/2 + v1
compat, dc.game/1, dc.fidelity/1, dc.lifecycle/1, dc.profile/1); profile
registry in `formats/profiles/` (DCP-2026-*, machine-readable). Governance:
24 ADRs (ADR-0001..0024), research ledger, contract-test gate
(`scripts/validate/contract_tests.py`), benchmark methodology
(`docs/benchmarks.md`).

## Completed work (verified 2026-09-07)

- **Phase 0 scaffold** (2026-09-06): canon, research ledger (15 topics
  re-verified against primary sources; canon §48 holds), 20 ADRs, schemas,
  governance.
- **ADR-0021 + dc.host-capability/2** (2026-09-07): measured-qualification
  schema evolution — DirectStorage discovery, three-state VRR, quantitative
  HDR (Output6 `DXGI_OUTPUT_DESC1`), qualification identity/build identity,
  profile claim/reject structures with reason codes. `/1` preserved for
  compatibility (`host-capability-v1.schema.json`).
- **Measured qualification (DK0-M1)**: platform-owned benchmarks for CPU
  (single-thread / game-thread / worker / sustained), memory (bandwidth /
  pressure-safe), storage (unbuffered seq / random / sustained with latency
  percentiles), GPU (D3D12 raster / compute / copy / sustained via timestamp
  queries). Deterministic `ExtractScores` + `DeriveProfiles` with per-profile
  reason codes. Evidence: `evidence/qualification/{benchmarks,summary}.json`.
- **ADR-0022 host-vs-session semantics** (2026-09-07): profiles split into
  `kind: host|session`. Host render capability (DCP-2026-RENDER-UHD60/120) no
  longer requires the attached display; active-experience profiles (DCX-*)
  require enumerated display modes (rational refresh), HDR active state, and
  VRR actively proven. `DeriveSessionProfiles` + two-section qualification
  report (HOST CAPABILITIES / SESSION CAPABILITIES). Legacy DCP-2026-4K60/4K120
  superseded; migration metadata on each profile.
- **First DCP profile claims derived from measured truth**:
  DevKit-0 qualifies for **DCP-2026-BASE, 4K60, 4K120**. Honest rejections:
  HDR (display SDR), HIGHIO (storage under load), LOWLATENCY (VRR not
  actively proven), RT1/RT2 (DXR benchmark pending). Thresholds calibrated
  against measurement (`docs/benchmarks.md` calibration table).
- **DevKit-0 defect hunt paid for itself** — found and fixed on real hardware:
  1. `BeginQuery` on TIMESTAMP (invalid — list poisoned; two `EndQuery`
     stamps instead).
  1b. **displayprobe swapchain INVALID_CALL (DISPROVEN 2026-09-10 — was
     misdiagnosed as driver blocker)**: the probe passed `ID3D12Device*` to
     `CreateSwapChainForHwnd` as pDevice; D3D12 requires the **command
     queue**. Every variant/adapter/path failed `DXGI_ERROR_INVALID_CALL`
     (0x887A0001 — also previously misread as ACCESS_LOST, which is
     0x887A0026). After the one-line fix, the probe proves the flip-model
     tearing path **actively** on DevKit-0 (`vrr_actively_proven=true`,
     proven_adapter=max-vram, flip-discard:3:tearing accepted, both present
     modes OK). Presentation was never driver-blocked; only RT dispatch is.
  2. READBACK-heap buffer created in COMMON (must be COPY_DEST).
  3. ALLOW_RENDER_TARGET texture created in COMMON (must be RENDER_TARGET).
  4. Dispatch X-count 65536 > 65535 limit.
  5. Compute SRV/UAV typed-buffer/descriptor mismatches (Buffer<float> now
     SM6.0 DXIL + inline root descriptors; driver's DXBC async-PSO-compile
     wedge documented in `docs/benchmarks.md` rule 12).
  6. JSON extraction dropped direct-score records (memory/storage reported
     unmeasured) and recomputed GPU scores in wrong units.
  7. Global-only deadline starved the GPU benches (per-bench budgets now).
  8. Anonymous evidence records (early aborts before stamping).
- **gpu.rt implemented as DXR 1.1 ray-query traversal** (2026-09-07):
  deterministic 4096-triangle BLAS × 8 TLAS instances, SM6.5 compute PSO
  (embedded signed DXIL from `gpu_rt_bench.hlsl`, DXC 1.9.5402),
  GPU-timestamped; AS-build GPU time recorded separately. **Blocked on
  DevKit-0 by a preview-driver defect**: ray-query dispatches remove the
  device asynchronously (INVALID_CALL) after successful AS builds/warmups —
  content-independent (single-ray bisect) and state-correct. Recorded
  `aborted` with full diagnostic chain; RT1/RT2 stay unmeasured (C10).
- **dc-displayprobe hardened and run** (2026-09-07): desc-variant fallback
  (FLIP_DISCARD/FLIP_SEQUENTIAL × 2/3 buffers × tearing), composition
  fallback, per-variant HRESULT evidence. **Superseded 2026-09-10**: the
  "machine-wide swapchain rejection" was our own invalid-API usage (device
  instead of command queue — see defect 1b above); with the queue fix the
  path creates and presents successfully. Three-state VRR model exercised
  for real: capable=true / path_compatible=true / actively_proven=true
  (evidence: run dirs contain `displayprobe.json`).
- **Truth-leak fix in derivation**: aborted sustained benches no longer feed
  `*_SUSTAINED_SCORE_BELOW_REQUIREMENT` comparisons — unmeasured stays
  `CAPABILITY_NOT_MEASURED` (C10).
- **Run-to-run variance documented** (directive §15): quick-loaded vs quiet
  standard runs differ materially (storage.seq 660–1490 MB/s; sustained
  raster 54–358 Mpx/ms heat-soaked back-to-back). Certification must use
  quiet-system conservative statistics; no best-run cherry-picking.
- **Runtime evidence (standard mode, quiet-ish, 2026-09-07)**:
  cpu.game-thread ~105 Mops/s burst / 93.7 sustained · gpu.raster 692.0
  Mpx/ms · gpu.compute 3.19 GMACs/ms · gpu.copy 187.8 GB/s ·
  memory.bandwidth 26.3 GB/s · memory.pressure-safe 27.0 GiB ·
  storage.seq 1490.5 MB/s · ctest 3/3 · contract gate all-pass · all
  evidence/profile/schema JSON parses valid.

## Unfinished work (active milestone)

- **DK0-M1 residuals (blocked on environment)** (status vocabulary: ADR-0023):
  - `gpu.rt_inline` — IMPLEMENTED / BLOCKED_DRIVER: async device removal on
    ray-query dispatch (RESEARCH_LEDGER §24). Requires driver update/reboot.
    RT1/RT2 derivation ready.
  - `gpu.rt_pipeline` — IMPLEMENTED / BLOCKED_DRIVER: state object rejects
    any DXIL-library subobject (E_INVALIDARG, ledger §25). Prior "TraceRay
    unavailable in DXC" claim DISPROVEN (ledger §23); full-pipeline code is
    complete and waits on the library-path fix.
  - VRR actively-proven session claim — displayprobe now **actively proves
    the tearing presentation path** on DevKit-0 (2026-09-10); integrated into
    `--qualify` (child process, parsed output, immutable run evidence). The
    DCX-VRR gate is now measured storage p99 latency (via
    DCP-2026-LOWLATENCY), not the presentation path.
  - DirectStorage probe reports `unavailable` truthfully on this host
    (`storage.ral.dll` absent — redistributable, not OS component).
  - ~~Vulkan probe~~ **RUNTIME_VERIFIED 2026-09-07**: loader 1.4.341, RTX
    5070 Ti, ray-query + mesh-shader extensions (ledger §25).
  - ~~GameInput/SDL3 backends~~ **RUNTIME_VERIFIED 2026-09-07**: GameInput
    primary (redist 4.3.0.218; static-link mandatory — dynamic load crashes,
    ledger §26) → SDL3 3.4.16 fallback → XInput floor (ledger §27).
  - ~~Quiet-system certification run~~ **VERIFIED**: `dc-stage-certification`
    stages runs outside OneDrive with thermal guardrails + provenance
    manifest (first staged quick run 2026-09-08T05_36_52Z).
- **DK0-M2 Console Session — RUNTIME_VERIFIED 2026-09-08**:
  - `dc-session` process: full state machine (SESSION_OFF → SHELL_ACTIVE →
    TITLE_* → SHELL_ACTIVE → SESSION_EXIT), durable append-only JSONL journal
    with correlation IDs, title-registry launch path (§25–§28).
  - Title supervisor (Job Objects): launch → supervise → terminate-tree →
    no orphans, runtime-verified live (test_supervisor_live).
  - Native D3D12 sample title `dc-sample-native-minimal` (first .11g seed):
    supervised launch, clean WM_CLOSE exit, controller input hook.
  - **E2E crash path VERIFIED**: force-kill → TITLE_CRASHED → SHELL_RECOVERING
    → SHELL_ACTIVE → exit (sess-20260908T065134Z journal); external kill of the
    session itself proved the journal survives process death (065214Z ends at
    TITLE_ACTIVE, history intact).
  - **E2E clean path VERIFIED**: WM_CLOSE → TITLE_STOPPING →
    SHELL_RECOVERING → SHELL_ACTIVE → SESSION_EXIT, exit 0
    (sess-20260908T144053Z journal).
  - Focus graph (7/7 tests), input router with system-action interception,
    GameInput/SDL3/XInput backends behind IInputBackend.
- **DK0-M2 interactive shell — RUNTIME_VERIFIED 2026-09-08 (selftest)**:
  - `dc-session --shell` (`tools/session/shell.cpp`): real D3D12 flip-model
    surface (1920×1080 composed frame, GPU upload, VSync 1), HOME / LIBRARY /
    SYSTEM / GUIDE views, deterministic focus graphs per view, GameInput
    primary + explicit keyboard development fallback, host truth rendered
    from immutable `evidence/host-capability.json` (VRR three-state and HDR
    state displayed verbatim — shell computes nothing, §40).
  - Guide ownership live: system action suspends title (TitleSuspended →
    Guide overlay → Resume → TitleActive), close-from-Guide goes through the
    supervisor; titles never outlive the session.
  - CTest `session_shell_selftest` added: boots, renders 90 frames, auto-
    launches the registered native title, supervised clean close, journal
    sess-20260908T180230Z. Observed: boot → SHELL_ACTIVE → TITLE_REQUESTED →
    TITLE_VALIDATING → TITLE_STARTING → TITLE_ACTIVE → 90 frames →
    TITLE_STOPPING → SHELL_RECOVERING → SHELL_ACTIVE → SESSION_EXIT.
  - Remaining: physical controller-only acceptance walk (§36) and input-loss
    walk (§38; needs a controller attached). Display-topology reaction (§39)
    is now runtime-verified — see the 2026-09-10 entries below.
- **§39 display-topology reaction — RUNTIME_VERIFIED 2026-09-10**:
  `WM_DISPLAYCHANGE` → live `ProbeDisplays` re-enumeration (captured in the
  WndProc, processed on the main loop — WndProc never blocks on the
  supervisor) → active title superseded through the journaled supervisor
  path → stale DCX claims invalidated against LIVE display truth
  (`DISPLAY_MODE_UNAVAILABLE` / `HDR_INACTIVE` /
  `VRR_NOT_ACTIVELY_PROVEN`). Evidence files untouched (ADR-0023); DCP-*
  host claims never touched (ADR-0022). Verified by an extended shell
  selftest injecting a real topology event mid-run and seeding a stale
  `DCX-UHD120` claim that live panel truth invalidates.
- **Typed launch contract (canon §16 embryo) — RUNTIME_VERIFIED 2026-09-10**
  (ADR-0024): `TitleLaunchContext` + versioned `dc.title-context/1` envelope
  (strict parse, foreign schema = hard rejection); supervisor injects the
  envelope into the child environment block; sample title consumes session
  identity, rational display mode, verbatim host evidence and DCX claims,
  refuses contexts without Guide ownership (§34), and renders from semantic
  gamepad state. `test_title_context`: 14 checks incl. live cross-process
  transport proof.
- **§K controller dedup — code-enforced 2026-09-10**:
  `input_reconcile.hpp` pure policy (GameInput → SDL3 → XInput;
  conservative duplicate suppression; deterministic slots), applied in
  `ProbeInput`; 14 tests covering all §K scenarios.
- **§19 QR1 native lifecycle boundary — RUNTIME_VERIFIED 2026-09-10**:
  `ITitleSupervisor::SuspendGame/ResumeGame` (message channel DC_WM_SUSPEND/
  RESUME in the WM_APP range + job-wide named-event broadcast, ACK event
  pre-created by the supervisor to close the acknowledgment race; a missing
  ACK after delivery is `Failed`, not silent success). The sample title
  checkpoints (render loop idles while suspended) and ACKs through the
  shared name derivation. Guide→suspend and Resume now flow through this
  real boundary; selftest proves the full suspend→resume round-trip with
  `ok` on both ACKs. Compatibility titles remain best-effort (state machine
  owns policy); no arbitrary GPU-heavy checkpointing is claimed (C12 canon).
- **§18 audio-topology reaction — RUNTIME_VERIFIED 2026-09-10**:
  `AudioEndpointWatcher` (IMMNotificationClient, COM STA) → journaled
  `TopologyChanged` note + SYSTEM view endpoint refresh + toast; the title
  is deliberately untouched (audio route changes never restart or suspend a
  running title). Verified via selftest-driven flag through the real
  handler path (1 endpoint enumerated live).
- **§25 deliverable — evidence/milestones/DK0-M1-M2-status.json**:
  machine-readable milestone truth (implementation / verification /
  certification states, blocked items with blockers and evidence refs).
- **DK0-M1 capability matrix (2026-09-10)** —

| Capability | Implemented | Verified | Certified | Blocker |
|---|---|---|---|---|
| CPU qualification | yes | RUNTIME_VERIFIED | dev-baseline | — |
| GPU qualification | yes | RUNTIME_VERIFIED | dev-baseline | — |
| memory qualification | yes | RUNTIME_VERIFIED | dev-baseline | — |
| storage qualification | yes | RUNTIME_VERIFIED | dev-baseline | OneDrive (staged runs used) |
| RT discovery | yes | RUNTIME_VERIFIED | — | — |
| RT pipeline | yes | UNIT_TESTED | no | DRIVER (rt_inline) / ENV (state object) |
| display discovery | yes | RUNTIME_VERIFIED | dev-baseline | — |
| HDR presentation | discovery yes | truth recorded | no | HDR_INACTIVE (panel) |
| VRR proof | yes | ACTIVELY_PROVEN | dev-baseline | — |
| Vulkan discovery | yes | RUNTIME_VERIFIED | — | — |
| GameInput | yes | RUNTIME_VERIFIED | — | — |
| SDL3 | yes | RUNTIME_VERIFIED (discovery+dedup) | — | — |
| staged certification | yes | RUNTIME_VERIFIED | standard run captured | — |

  Verdict per ADR-0023: **DK0-M1 IMPLEMENTATION-CLOSED**;
  **CERTIFICATION-BLOCKED-BY-ENVIRONMENT** (RT driver, HDR/UHD panel,
  storage p99). The milestone is not "failed" — the blockers are recorded
  truthfully and re-test on driver/display change.
- **Post-reboot verification + truth corrections — 2026-09-10**:
  - Full suite 8/8 CTest + contract gate green after reboot (SAC off).
  - displayprobe swapchain INVALID_CALL root-caused to invalid API usage
    (device instead of command queue — see defect 1b); VRR presentation path
    **ACTIVELY PROVEN** on DevKit-0 dGPU. Presentation was never driver-
    blocked; ledger §23a updated (DISPROVEN for claim (c)).
  - displayprobe integrated into `--qualify`: child-process execution, real
    JSON parse, immutable `displayprobe.json` per run, `display.vrr_proven`
    feeds host+session derivation. DCX-VRR now gated only by measured
    storage p99 (via DCP-2026-LOWLATENCY).
  - Supervisor crash semantics corrected: only exit 0 is Clean; external
    taskkill now journals `TITLE_CRASHED → SHELL_RECOVERING → SHELL_ACTIVE`
    (verified sess-20260910T013927Z).
  - Sustained CPU methodology fixed (continuous-load window; idle-gap design
    measured its own DVFS ramp); quiet staged standard run captured
    `2026-09-10T02_11_25Z_standard`: HOST CLAIMED BASE/HDR/RENDER-UHD60/
    RENDER-UHD120 with sustained_valid=true; honest rejections unchanged.
  - DK0-M1 DevKit-0 certification: the ONLY remaining block is RT
    (gpu.rt_inline driver device-removal; gpu.rt_pipeline OS runtime
    CreateStateObject rejection). Everything else measured, qualified and
    evidenced on a quiet system.
- Then canon §43 order: M3 native .11g title → M4 Fidelity Lab (VFR) →
  M5 certification runner.
- **DK0-M3 `.11g` package vertical slice — RUNTIME_VERIFIED 2026-09-10**
  (ADR-0025; milestone status `evidence/milestones/DK0-M3-status.json`):
  - **Package contract** (`runtime/core/dc/package*.hpp/cpp`): logical model
    separate from the physical container (`DC11G` magic, member table) and
    the content-addressed store (`store/objects/sha256/<h2>/<h62>`).
    Identity = SHA-256 over canonical manifest fields excluding the declared
    id (self-reference impossible); NIST-vector-tested + BCrypt-cross-checked.
  - **dc-pack** (deterministic pack; same input → same identity; no
    timestamps/randomness/absolute paths in canonical bytes),
    **dc-verify-package** (structured reason codes, fails closed),
    **dc-packaged** (install / active / generations / rollback; CNG ECDSA
    P-256 signing at the host layer with explicit `--allow-unsigned`
    development policy — trust states distinguished, never conflated).
  - **Library**: generations `gNNNN` fully materialized + byte-verified
    before the atomic `active.json` pointer swap; failed install leaves the
    previous generation active (tested); rollback metadata proven (tested);
    **idempotent reinstall** (same identity+version no longer stacks
    generations — fixed after live evidence, runtime-verified).
  - **Registry integration**: `registry/tech.11vated.fidelitylab.title.json`
    matches the package game_id; `LaunchTitle` resolves the installed
    package FIRST (source-tree entrypoint is the documented development
    fallback), runs host-profile preflight from package metadata, launches
    the generation-view executable with the typed launch context (ADR-0024)
    and lifecycle manifest carried through (§17).
  - **§24 acceptance — the packaged artifact, not the source tree**:
    CTest `package_library_install` fixture installs the real
    `FidelityLab.11g` (containing the built D3D12 title) into the console
    library; `session_shell_selftest` runs with `DC_LIBRARY_DIR` and proves:
    packaged launch (`launched PACKAGED tech.11vated.fidelitylab g=…`) →
    QR1 suspend/resume **ok/ok** (cold-start race in the supervisor fixed:
    bounded window re-enumeration so Guide-at-launch cannot be lost) →
    clean exit → SHELL_ACTIVE. `test_package_e2e` proves the full
    pack→verify→install→activate→launch chain plus v0.1.0→v0.1.1 update and
    corrupt-v0.1.2 fail-closed rollback (§25).
  - **Save boundary (§18)** (`dc/save_boundary`): saves owned by
    (user_id, game_id, save_schema); patch versions stay recognized;
    incompatible schema requires explicit declared migration.
  - Tests: `package_contract` (57 checks incl. SHA vectors, determinism,
    rename-invariance, corruption battery, activation/rollback, save-compat),
    `package_signing` (21 checks: sign/verify round-trip, tamper detection,
    four trust states), `package_e2e` (11 checks) — **14/14 CTest**, 68/68
    evidence/schema JSON valid.

## Known issues / environment

- **Smart App Control (2026-09-08)**: SAC (evaluation mode) re-blocked all
  freshly built binaries mid-session, hiding failures as `Permission denied`
  (exit 126). Root-cause chain in CodeIntegrity event log (ID 3077/3089/3118).
  Registry `VerifiedAndReputablePolicyState` set to 0 via elevated command
  and `Get-MpComputerStatus` reports 0, but Code Integrity kept enforcing
  (stale in-memory policy) — **a reboot is required to apply**. Any session
  seeing `Permission denied` on just-rebuilt exes should check SAC first
  (`Get-WinEvent -LogName 'Microsoft-Windows-CodeIntegrity/Operational'`).
- OneDrive sync on the repo path distorts storage/thermal measurements
  during heavy rebuild windows; certification runs should use a local path.
- MinGW-w64 `as.exe` Permission denied (OS-level block) → MSVC is the working
  Windows toolchain (canon-designated). GCC/Clang warnings-as-errors run
  deferred to CI.
- D3D12 debug layer (Graphics Tools) not installed on DevKit-0 — diagnostics
  rely on HRESULT probes + info-queue drain when installed.

## DK0-M3 validation matrix

| Capability | Implemented | Unit | Integration | Runtime | Negative tested | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| Package schema (dc.package/1) | yes | yes | yes | yes | yes | package_contract |
| SHA-256 identity | yes | yes | yes | yes | yes (rename/mod) | package_contract |
| Deterministic packaging | yes | yes | yes | yes | yes (byte-change) | package_contract |
| Verification (reason codes) | yes | yes | yes | yes | yes (10 corruption cases) | package_contract, dc-verify-package |
| Signature/trust | yes | yes | — | — | yes (tamper) | package_signing |
| Content-addressed store | yes | yes | yes | yes | yes | package_contract |
| Atomic install | yes | yes | yes | yes | yes (failed install) | package_contract, package_e2e |
| Activation | yes | yes | yes | yes (shell) | yes | package_e2e, selftest |
| Rollback | yes | yes | — | — | yes (corrupt v2 → v1 active) | package_e2e |
| Registry integration | yes | — | yes | yes (packaged launch) | yes (missing exe) | selftest log |
| Host-profile preflight | yes | — | yes | yes | yes (unsatisfied → refuse) | shell.cpp Preflight |
| Lifecycle manifest | yes | yes | yes | yes (QR1 deadlines) | yes | lifecycle.json, selftest |
| Save compatibility | yes | yes | — | — | yes (schema break) | package_contract |
| Real package launch | yes | — | yes | yes (PACKAGED marker) | yes | package_e2e, selftest |
| QR1 from package | yes | — | yes | yes (ok/ok) | yes | selftest log |

## DK0-M4 — VFR seed (fidelity contract → compiler → governor → packaged title)

**Status: IMPLEMENTED + UNIT/INTEGRATION-TESTED; GPU/visual leg BLOCKED_DRIVER**
(ADR-0026; milestone status: `evidence/milestones/DK0-M4-status.json`).

- **Contract (`dc.fidelity/1`)**: 7 real domains (internal_resolution,
  geometry_density, shadow_quality, reflection_quality, volumetric_quality,
  particle_density, simulation_quality) × discrete states with declared
  costs + transition policies. Full rejection-reason validation; strict
  serializer/parser round-trip.
- **Compiler (`dc-fidelity-compile`)**: bounded enumeration (648 → 592
  in-budget → 187 Pareto) from REAL host evidence (latest immutable
  qualification run); byte-deterministic artifact; contract-SHA-256 binding
  rejects stale artifacts; domain policies embedded. Budgets derived once in
  the core (`BudgetFromHostEvidence`) and shared by tool + runtime.
- **Governor**: hysteresis + dwell; **binding-axis step-down** (any-axis
  lightness caused live Pareto ping-pong — fixed and regression-tested);
  §23 transition legality (SCENE_BOUNDARY/RESTART_REQUIRED frozen at
  runtime; sim-density change refused with visible evidence); intent changes
  obey the same legality; reason-coded live decision trace.
- **Package integration (§28)**: fidelity.json + compiled
  fidelity.candidates.json ship INSIDE FidelityLab.11g; the runtime loads
  them from the INSTALLED generation view (never the source tree). Envelope
  carries `fidelity_intent` (shell Home tile cycles it; System view shows
  developer diagnostics).
- **Runtime verification (packaged artifact, g-generation views)**:
  initial Pareto-optimum selection applied across all 7 domains; sustained
  CPU-pressure harness stepped down through 3 monotonically lighter
  candidates with reason-coded trace (`evidence/fidelity/m4-acceptance/`);
  GPU telemetry honest-UNKNOWN (never fabricated).
- **Device-removal hardening**: the title now checks Present/fence HRESULTs,
  surfaces `GetDeviceRemovedReason` at the PSO checkpoint, and exits with
  structured evidence (exit 3) instead of silently "rendering" on a dead
  device. Explicit max-VRAM adapter selection (`DC_TITLE_GPU` override).

### DK0-M4 validation matrix

| Capability | Implemented | Unit | Integration | Runtime | Visual | Deterministic | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Fidelity schema (dc.fidelity/1) | yes | yes | yes | yes | — | yes | test_fidelity |
| Domain/state validation | yes | yes | yes | — | — | yes | test_fidelity (§37 negatives) |
| Compiler | yes | yes | yes | yes (real evidence) | — | yes (byte-identical) | test_fidelity, repeat-compile |
| Pareto filtering | yes | yes | yes | yes | — | yes | test_fidelity |
| Host budgets | yes | yes | yes | yes | — | yes | BudgetFromHostEvidence + run index |
| Session budgets | yes (distinct axes) | yes | yes | — | — | yes | test_fidelity |
| Candidate artifact | yes | yes | yes | yes (from package) | — | yes | fidelity_package |
| Runtime governor | yes | yes | yes | yes (CPU axis) | — | yes | test_fidelity + m4-accept.log |
| Hysteresis | yes | yes | — | yes | — | yes | test_fidelity, stress run |
| Dwell time | yes | yes | — | yes | — | yes | test_fidelity, stress run |
| Reason tracing | yes | yes | — | yes (live) | — | yes | m4-accept.log |
| FidelityLab application | yes | — | yes | yes (7 domains) | **blocked** | yes | selftest + m4 logs |
| Package integration | yes | — | yes | yes (generation view) | — | yes | fidelity_package |
| Visual differences | yes | — | — | **BLOCKED_DRIVER** | **blocked** | — | m4-bisect.log |
| Pressure adaptation | yes | yes | — | yes (CPU axis) | — | yes | m4-accept.log trace |

### M4 driver blocker (M1-F classification)

`DXGI_ERROR_DEVICE_HUNG` (0x887A0001) asynchronously during/after
runtime-compiled shader **PSO creation** — reproduced on BOTH adapters
(NVIDIA RTX 5070 Ti 32.0.16.1078; Intel Graphics 32.0.101.6629); device
healthy through every init step incl. all 3 PSO creations, removal observed
at the next CPU-side call. `dc-displayprobe` (no PSOs, real GPU submissions)
remains healthy on the same NVIDIA adapter. One controlled reproduction +
bisect only (M1-F discipline). Evidence:
`evidence/fidelity/m4-acceptance/m4-{bisect,gpu,intel,final}.log`.
Consequence: GPU timestamp telemetry + visual-difference proof remain
UNAVAILABLE/BLOCKED_DRIVER; the governor's CPU-axis loop is real and
measured; UNKNOWN is never fabricated.

## Known issues / environment

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python scripts/validate/contract_tests.py
./build/Release/dc-hostprof.exe --report --out evidence/host-capability.json
./build/Release/dc-hostprof.exe --qualify quick --out evidence/qualification/qualification-summary.json
```

## Definition of next session

1. **DK0-M4 — Fidelity Lab (VFR) seed**: consume the capability surfaces
   (GetHostCapabilities / GetSessionCapabilities / capability-changed
   subscriptions) with the first negotiated fidelity profile on the packaged
   FidelityLab title.
2. **Physical controller acceptance**: §36 controller-only walk against the
   INSTALLED package (now the launch path), §38 input-loss reaction.
3. **Post-reboot RT re-test**: `gpu.rt_inline`, `gpu.rt_pipeline`
   (RESEARCH_LEDGER §24–§25) — the last DK0-M1 certification blocker.
4. **Production signing policy**: trust anchor + key ceremony design for
   signed packages (current: documented unsigned-development mode).
