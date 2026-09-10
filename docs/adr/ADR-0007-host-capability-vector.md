# ADR-0007 — Host capability vector and profile versioning

**Status:** Accepted · **Date:** 2026-09-06

## Context
D3D feature levels describe functionality, not performance (blueprint §4).
GPU marketing tiers cannot qualify a console. Hardware fragmentation must be
converted into deterministic, machine-readable capability (C4, canon §10).

## Decision
- Every host produces a **HostCapabilityRecord** (`dc.host-capability/1`
  schema): OS, CPU (topology + measured scores), GPU (APIs, VRAM, measured
  raster/compute/RT/matrix scores, feature flags), memory, storage (class +
  measured latency/throughput + GPU decompression), display, audio, input,
  thermal, trust.
- Scores derive from **platform-owned reproducible microbenchmarks**, never
  vendor model-name tables alone.
- Profiles are **composable and versioned per capability family**
  (DCP-2026-BASE, -HIGHIO, -RT1, -RT2, -AI1, -4K120, -HDR, …). A host claims
  the intersection it satisfies; titles declare required/optional profiles.
- Requalification triggers: GPU change, CPU/firmware power config change,
  major driver update, memory size change, game-storage change, display change
  (for display-dependent certs), major runtime version, material thermal/
  power-policy change (canon §10.4).
- A consumer "Experience Class" may summarize, but SDK retains full data.

## Consequences
- `dc-hostprof` is the first executable (canon §50) — capability truth first.
- Profile registry is version-controlled in `formats/schemas/`.
