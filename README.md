# 11vated Digital Console

A software-defined digital game console whose identity, runtime, game contract,
services, fidelity system, saves, profiles, lifecycle and developer platform are
independent of a single physical hardware generation.

**Computer ≠ Console.** The physical machine is a *host*. The Digital Console is
the persistent platform.

```
Computer
+ Certified Console Host Layer
+ Console Runtime
+ Native Game Contract (.11g)
+ Fidelity Runtime (VFR)
+ Console Services
= Digital Console
```

## Canonical documents

| Document | Path | Status |
|---|---|---|
| Platform Constitution & Architecture | `docs/canon/DC-CANON-001.md` | Canonical v0.1.0 |
| Digital Console Canon (blueprint) | `docs/canon/DC-BLUEPRINT-001.md` | Canonical v0.1 |
| Architecture Decision Records | `docs/adr/` | ADR-0001..0020 |
| Research ledger | `docs/research/RESEARCH_LEDGER.md` | Living |
| Repository state | `CURRENT_STATE.md` | Living |

## Repository topology

```
runtime/core      Platform-agnostic interfaces (C++23), no host dependencies
runtime/host      Host abstraction interfaces
hosts/windows     Windows 11 host implementation (capability discovery, etc.)
tools/hostprof    dc-hostprof — Host Doctor (first executable, DK0-M1)
formats/schemas   JSON Schema registry (host-capability, game, fidelity, lifecycle)
tests/contract    Schema/interface contract tests
tests/unit        Unit tests
samples/          minimal-title, fidelity-lab (later)
apps/shell        dc-shell (later phases)
docs/canon        Immutable constitutional documents
docs/adr          Architecture Decision Records
docs/research     Research ledger + source records
evidence/         Verification artifacts (JSON, logs, reports)
scripts/validate  Validation gate runners
```

## Dependency rules (normative, DC-CANON-001 §39.1)

- `runtime/core` depends on no host implementation.
- Host-specific code implements interfaces in `runtime/host`.
- SDK public headers never include OS-only types.
- Compatibility runtime is never a dependency of native core services.
- VFR never depends on a specific game engine.
- No mutable globals for service ownership.

## Build (DevKit-0, VS 2022 BuildTools + MSVC)

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python scripts/validate/contract_tests.py
./build/Release/dc-hostprof.exe --report --out evidence/host-capability.json
```

Note: the local MinGW-w64 toolchain's assembler is currently blocked by an OS
permission issue (`as.exe: Permission denied`); MSVC (canon §40.1 designated
Windows toolchain) is the working baseline.

## Status

**Phase 0** — Research freeze + architecture scaffold (per DC-CANON-001 §43).
Phase 1 (DevKit-0 host foundation, first executable `dc-hostprof`) is active.
