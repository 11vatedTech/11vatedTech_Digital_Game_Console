# ADR-0021 — Host Capability Schema v2: measured qualification data

**Status:** Accepted · **Date:** 2026-09-06 · **Supersedes:** none (extends ADR-0007)

## Context

`dc.host-capability/1` was designed for the discovery-only pass. DK0-M1
qualification (measured CPU/GPU/memory/storage benchmarks, sustained
qualification, DirectStorage discovery, quantitative HDR, three-state VRR,
profile derivation) adds data that cannot be expressed compatibly:

1. **Semantic break — "0 means unmeasured".** The /1 convention encodes
   unmeasured scores as `0`. The DK0-M1 exit gate forbids score fields left at
   zero *and* forbids claiming unmeasured values. A benchmark that legitimately
   measures near-zero, and a benchmark that never ran, must be
   distinguishable. /2 makes measurement state explicit
   (`qualification.measured = true|false`); a numeric `0` is now always a real
   measured value or a legitimate zero.
2. **New sections:** DirectStorage discovery, build identity (git/build config/
   runtime version per ADR-0018), qualification mode (quick/standard/
   certification), profile derivation results with machine-readable reason
   codes, `profiles` (bare string array) replaced by `profiles_claimed` /
   `profiles_rejected` objects.
3. **Extended existing sections:** storage latency p95, queue depth, HDR
   metadata formats + primaries + white point, VRR three-state model
   (`capability` / `path_compatible` / `actively_proven`), GPU copy
   bandwidth, matrix-capability explicit state.

Adding optional fields could keep /1, but (1) is a semantic break that silently
misreads under /1 consumers: they cannot tell "unmeasured" from "measured 0".

## Decision

- New schema **`dc.host-capability/2`** (`formats/schemas/host-capability.schema.json`
  carries /2; `/1` is preserved in `formats/schemas/host-capability-v1.schema.json`).
- `dc-hostprof` emits /2 by default; `--schema-version 1` emits a /1-shaped
  record for downstream compatibility (legacy field mapping, unmeasured → 0).
- /1 remains readable forever for archived evidence; the compat mapping is
  covered by tests.
- Profile claims move from `profiles: string[]` to:
  `profiles_claimed: [{id, version, derived_from, mode}]` and
  `profiles_rejected: [{id, reasons: [REASON_CODE...]}]`.
- All benchmark-derived values live under a `qualification` sub-object with
  `measured: bool` + `mode` + raw measurement records; discovery fields stay
  at the section top level.

## Consequences

- Producers must be updated in lockstep (this repo only, so far).
- Consumers of /1 evidence must use the documented mapping; archived /1 files
  remain valid as historical records.
- Determinism: for identical benchmark version + host state class + profile
  registry, /2 derivation output is byte-identical (tested).
