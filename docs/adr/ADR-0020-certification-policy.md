# ADR-0020 — Certification policy/versioning

**Status:** Accepted · **Date:** 2026-09-06

## Context
Certification is the mechanism that turns heterogeneous hardware into a
trustworthy console platform (canon §34). It must be evidence-based,
version-controlled, and immune to silent weakening.

## Decision
- **Certification layers L1–L10:** Package Integrity, Runtime Contract,
  Controller/UX, Lifecycle/Recovery, Display/Audio/Input,
  Performance/Fidelity, Storage/Streaming, Security/Permissions,
  Accessibility, Long-duration Reliability. A title is "Digital Console
  Certified" only when all levels required by its declared profile pass.
- Pass/fail thresholds are **explicit and version-controlled** (no per-run
  tuning).
- Performance certification captures representative traversal corpus, combat/
  stress scenes, worst authored scenes, cutscenes, loading spikes, long-
  duration thermal run per declared envelope (60/120/HDR/RT/Remote targets).
- Fidelity evidence: selected domains match a generated validated profile;
  resources within margins; transitions respect declared safety; no sustained
  oscillation; corruption tests pass; traces attached.
- Shader hitch tests: cold/warm cache, driver-cache invalidation, update
  scenario, pipeline-creation audit (unplanned runtime PSO stalls = defect).
- Platform gates P0–P8 (canon §42) govern the runtime itself.
- Fidelity marks (11G 60/120/HDR/RT/INSTANT-RESUME, 11G MAX) certify measured
  experience, never marketing resolution; frame generation never counts as
  simulation performance (C7).
- Every run emits a deterministic evidence bundle (ADR-0018).

## Consequences
- The certification harness is the canonical evidence layer (canon §40.2);
  RenderDoc/PIX/Nsight/RGP are optional diagnostics.
- Certification policy changes require a version bump + ADR.
