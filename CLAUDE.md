# CLAUDE.md — 11vated Digital Console

## Canon authority

Read `docs/canon/DC-CANON-001.md` fully before modifying architecture. It is the
canonical authority (v0.1.0). `docs/canon/DC-BLUEPRINT-001.md` is the blueprint
companion. Do not silently contradict canon. Architectural changes require:
identify conflict → authoritative evidence → ADR → tradeoff explanation → canon
update → then implement.

## Truth classification

Every roadmap item carries exactly one classification:
`PROVEN/IMPLEMENTABLE-NOW` | `PLAUSIBLE-EXTENSION` | `RESEARCH-GRADE`.
Preserve this distinction in all product communication.

## Completion vocabulary (mandatory)

`implemented` · `compiled` · `unit-tested` · `integration-tested` ·
`runtime-verified` · `visually inspected` · `benchmarked` ·
`partially verified` · `blocked`. Never "complete/finished/production-ready"
unless the corresponding verification actually ran and passed.

## Build & verify (DevKit-0, MSVC)

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python scripts/validate/contract_tests.py        # schema/canon/dependency gate
./build/Release/dc-hostprof.exe --report --out evidence/host-capability.json
./build/Release/dc-hostprof.exe --qualify quick --out evidence/qualification/qualification-summary.json
```

## Measured-truth rules (benchmarks + profiles)

- Every score needs methodology id + raw samples + evidence identity
  (`docs/benchmarks.md` is normative).
- `unmeasured` stays null/0-with-state; aborted runs are never scores (C10).
- Host vs session profiles (ADR-0022): host profiles never depend on attached
  display state; session profiles (DCX-*) require enumerated modes, HDR active
  state, and actively-proven VRR. Two-section qualification report.
- DCP profile thresholds are versioned data; calibration record lives in
  `docs/benchmarks.md`. Never hardcode qualification logic in C++.
- Never expose GameInput/D3D12/DXGI types beyond the host boundary.
- GPU bench ordering is evidence-pinned (raster last; see `qualify_runner.cpp`)
  — DevKit-0 preview-driver context-wedge defect (RESEARCH_LEDGER §24).
- DXC toolchain: `TraceRay` WORKS (prior "unavailable" claim DISPROVEN —
  call-arity mistake, not a toolchain gap; RESEARCH_LEDGER §23). Full DXR
  pipeline shaders compile as library targets (lib_6_3+) with the 8-param
  signature `TraceRay(AS, flags, mask, c0, c1, missIdx, rayDesc, payload)`.
  Project-vendored DXC lives in `tools/dxc/` (v1.9.2607) for authoring-time
  shader compilation; embedded DXIL pattern is the platform's shader-artifact
  precedent (ASD-aligned).

## Dependency rules (DC-CANON-001 §39.1, normative)

- `runtime/core` depends on no host implementation.
- Host code implements `runtime/host` interfaces.
- SDK public headers contain no OS-only types.
- Compatibility runtime is never a dependency of native core services.
- VFR never depends on a game engine.
- No mutable globals for service ownership; explicit lifetime/DI.

## Repository conventions

- C++23, CMake 3.30+, MSVC (Windows) / Clang-GCC (ConsoleOS), warnings-as-errors.
- First executable was `dc-hostprof` (canon §50) — capability truth before shell.
- Windows Host Edition is the bootstrap host; ConsoleOS is the long-term
  appliance host. Same public contracts on both (canon §6).
- Milestones: DK0-M1 Host Doctor → DK0-M2 Console Session → DK0-M3 Minimal
  native title → DK0-M4 Fidelity Lab → DK0-M5 Certification runner.
