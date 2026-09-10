# ADR-0008 — VFR: offline Fidelity Compiler + runtime governor

**Status:** Accepted · **Date:** 2026-09-06

## Context
VFR (Virtual Fidelity Runtime) converts host capability + title-authored
scalable domains into a validated experience (canon §17). Solving an
unconstrained combinatorial optimization every frame is not viable; silently
changing restart-required settings is prohibited (C5, C6, §17.6).

## Decision
Two-stage architecture:
- **Stage A — Fidelity Compiler** (offline/dev/build time): consumes title
  quality domains, runs automated benchmark scenes, measures cost/utility,
  detects invalid combinations, generates Pareto-efficient profile candidates,
  records driver/host exceptions, emits certification evidence.
- **Stage B — Fidelity Governor** (runtime): selects initial validated profile,
  reserves safety headroom, monitors frame/latency/memory/IO pressure, changes
  only approved dynamic domains, uses hysteresis + dwell time, never oscillates,
  records reason codes for every adaptation.

Transitions declare: INSTANT_SAFE, HYSTERETIC_RUNTIME, CAMERA_CUT_ONLY,
SCENE_BOUNDARY, RELOAD_REQUIRED, RESTART_REQUIRED. The governor MUST NOT
silently apply restart-required changes during gameplay.

VFR never renders the game; the engine owns its render graph. VFR negotiates
budgets and quality states only.

## Consequences
- The objective function (canon §17.3) is evaluated against prevalidated
  candidates, not solved fresh at runtime.
- Every fidelity decision is auditable via reason codes (Gate P5).
