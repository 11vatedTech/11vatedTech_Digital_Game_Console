# ADR-0002 — Windows Host Edition vs ConsoleOS separation

**Status:** Accepted · **Date:** 2026-09-06

## Context
Two host classes are needed: immediate compatibility + driver maturity
(Windows), and long-term appliance ownership (Linux-based). Shell Launcher
replacing Explorer is restricted to Enterprise/Education/IoT editions, so
ordinary Windows Home/Pro cannot be a true controlled appliance shell
(verified 2026-09-06, RESEARCH_LEDGER §1).

## Decision
- **Windows Host Edition** (bootstrap + compatibility host): console *session*
  on Windows 11 — borderless/full-screen, auto-start option, controller-first,
  safe desktop escape, supervised titles. D3D12 + DirectStorage reference path.
- **Windows Lab Shell Edition**: Shell Launcher (Edu/IoT/Enterprise) used only
  as a laboratory target for appliance-style shell ownership validation.
- **ConsoleOS** (long-term): Linux-based immutable appliance host — Vulkan,
  DRM/KMS embedded compositor (gamescope-derived concepts), PipeWire, A/B
  updates. Implements the same public contracts (L2–L7).

## Consequences
- Platform contracts must never leak Windows assumptions into `runtime/core`.
- ConsoleOS work is gated behind Windows-host contract validation (canon §43
  Phase 7; anti-goal: OS fork before Windows host proves contracts).
- Xbox Mode exists but is aggregation-only; it neither replaces nor blocks us.

## Alternatives rejected
- Windows-only forever (appliance behavior unreachable on Home editions).
- ConsoleOS-first (delays compatibility, contracts unproven).
