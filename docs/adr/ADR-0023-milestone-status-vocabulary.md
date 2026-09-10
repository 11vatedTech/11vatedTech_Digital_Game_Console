# ADR-0023 — Milestone status vocabulary: implementation vs certification

**Status:** Accepted · **Date:** 2026-09-08

## Context

DK0-M1 mixes two fundamentally different kinds of "done":

1. Engineering work that is complete when the code exists, compiles, is
   unit-tested, and has been exercised on the target host.
2. Certification work that additionally requires the **environment** to
   cooperate: a stable driver, a quiet system, an active presentation path.

Collapsing the two keeps an entire milestone permanently "unfinished" because
one environment blocker (a preview-driver defect) refuses to lift — while at
the same time risking the opposite error: calling work "done" that was only
designed, never measured. The directive (§22, §47) requires separating
implementation-closed from certification-blocked, with a precise status
vocabulary.

## Decision

### Status vocabulary (normative, §47)

Every system is reported at exactly one of:

```text
DESIGNED               — architecture/ADR exists; no code
IMPLEMENTED            — code exists, compiles
COMPILED               — build green (subset of IMPLEMENTED; listed for clarity)
UNIT_TESTED            — contract/unit tests pass
INTEGRATION_TESTED     — multiple components exercised together
RUNTIME_VERIFIED       — executed on real DevKit-0 hardware with observed output
BENCHMARKED            — measured, with evidence artifacts
CERTIFIED              — meets a DCP/DCX profile per EVIDENCE_POLICY.md
BLOCKED_ENVIRONMENT    — implemented, but the host environment prevents execution
BLOCKED_DRIVER         — implemented, but the GPU driver prevents execution
```

Statuses never collapse: "implemented" never implies "verified"; "verified"
never implies "certified".

### Milestone closure distinction (normative, §22)

A milestone may be closed in two independent dimensions:

- **IMPLEMENTATION CLOSED** — all systems the milestone defines are at least
  RUNTIME_VERIFIED, or explicitly BLOCKED_* with the blocker classified and
  recorded (HRESULT, driver version, repro, ledger entry).
- **CERTIFICATION CLOSED** — all measurable capability claims are CERTIFIED
  per the evidence policy over immutable runs.

DK0-M1 is governed as follows:

| Area | Status |
|---|---|
| Host discovery (CPU/GPU/D3D12/storage/audio/display/TPM) | RUNTIME_VERIFIED |
| Measured CPU/GPU/memory/storage qualification | BENCHMARKED |
| Sustained qualification | BENCHMARKED (quick) / PARTIAL (standard, heat variance documented) |
| Vulkan host discovery | RUNTIME_VERIFIED (loader 1.4.341, RTX 5070 Ti) |
| GameInput backend | RUNTIME_VERIFIED (redist 4.3.0.218; static-link required) |
| SDL3 fallback backend | RUNTIME_VERIFIED (3.4.16, dynamic load) |
| gpu.rt_inline | IMPLEMENTED / BLOCKED_DRIVER (async device removal, ledger §24) |
| gpu.rt_pipeline | IMPLEMENTED / BLOCKED_DRIVER (state-object DXIL-library rejection, ledger §25) |
| VRR active proof | IMPLEMENTED / BLOCKED_ENVIRONMENT (swapchain creation rejected machine-wide) |
| HDR quantitative | BENCHMARKED (display capability recorded; HDR inactive on current panel) |
| Immutable evidence + aggregator + policy | RUNTIME_VERIFIED |
| DCP host profiles | CERTIFIED for BASE/RENDER-UHD60/RENDER-UHD120 (development policy, quick+standard runs) |
| DCX session profiles | NOT CLAIMED (honest: DISPLAY_MODE_UNAVAILABLE / HDR_INACTIVE / VRR unproven) |

**DK0-M1: IMPLEMENTATION CLOSED. DevKit-0 certification: PARTIALLY BLOCKED
(driver-dependent RT + presentation-path proof remain BLOCKED_*).**

## Consequences

- Governance documents (CURRENT_STATE.md, 11vt.project.yaml) use this
  vocabulary and never mark a blocked item as simply "incomplete".
- Blocked items carry a classification (driver/environment), diagnostics
  captured, and a re-verification path (e.g. reboot/driver-update then rerun).
- The milestone gate in DC-BLUEPRINT-001 is interpreted through this
  distinction; future milestones (DK0-M2+) inherit the vocabulary.

## Alternatives considered

- *Keep DK0-M1 open until every blocker lifts* — rejected: couples engineering
  progress to an external driver defect; violates directive §22.
- *Declare DK0-M1 fully closed* — rejected: would hide BLOCKED_* items behind
  a green flag; violates C10 (no silent degradation) and §45 (no cherry-pick).
