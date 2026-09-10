# ADR-0018 — Telemetry/evidence format and privacy

**Status:** Accepted · **Date:** 2026-09-06

## Context
Performance certification requires end-to-end evidence (C14): frame time,
pacing, latency, VRAM/RAM/IO pressure, thermal, present behavior, recovery —
not FPS alone. Local-first diagnosability is mandatory (C9, canon §36).

## Decision
- **Trace events** (compact binary, never verbose JSON for high-frequency
  data): monotonic timestamp, process/service, thread, category, event id,
  duration/value, host profile hash, game build id, VFR profile id.
- **Metrics baseline** for any performance certification (canon §21.2):
  sim FPS, render FPS, generated FPS (distinct), avg/p95/p99/p99.9 frame time,
  1%/0.1% lows, pacing variance, CPU main/worker, GPU frame time, VRAM/RAM
  high-water, storage pressure, present mode, VRR state, throttle events;
  latency-capable titles add input-to-sim, sim-to-present, input-to-photon.
- **Evidence bundle** per certification run: cert-result.json,
  host-capability.json, game-manifest.json, vfr-profile.json,
  performance.trace, crash/log extracts, capture references, shader-cache
  report, storage report, hash manifest.
- **Privacy:** upload opt-in or crash/security-scoped; local diagnostics
  without account creation.

## Consequences
- `formats/traces/` defines the binary trace format before Phase 3 telemetry.
- Evidence bundles are deterministic and hash-manifested (auditability).
