# ADR-0019 — SDK ABI/versioning policy

**Status:** Accepted · **Date:** 2026-09-06

## Context
The SDK must serve C++ engines (Unreal/Unity/Godot/custom) without leaking
OS-specific types, and must evolve across console generations without breaking
titles (canon §33, §16.2).

## Decision
- Public SDK headers contain **no Windows-only or Linux-only types** (canon
  §39.1).
- C++ interfaces for C++ consumers; a **minimal, versioned C ABI boundary**
  for non-C++ engines; the C ABI is the stability contract, C++ interfaces may
  evolve with schema-style versioning.
- Services are injected through a versioned `ConsoleServices` context; no
  process-wide mutable globals.
- Semantic versioning for SDK releases; IPC/manifest schemas are versioned
  with explicit backwards-compatibility rules (ADR-0004).
- Engine plugins (Unreal/Unity/Godot) wrap the same public boundary; engine
  integration is never a dependency of core services.

## Consequences
- ABI/behavior breaks require major versions plus migration notes; the C ABI
  surface stays minimal.
- `sdk/include` mirrors the platform contract surface (Lifecycle, Display,
  Input, Audio, Storage, Saves, Achievements, Users, Overlay, Capture,
  Fidelity, Telemetry, Networking, Accessibility, Package).
