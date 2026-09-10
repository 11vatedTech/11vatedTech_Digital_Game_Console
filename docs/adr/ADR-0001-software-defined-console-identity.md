# ADR-0001 — Software-defined console platform identity

**Status:** Accepted · **Date:** 2026-09-06 · **Supersedes:** none

## Context
Console identity has historically been fused to a hardware generation
(PS5 → PS5 Pro → PS6). This forces fidelity ceilings, forced upgrade cycles,
and dead platform investment. Modern hosts (DevKit-0 class laptops/desktops)
exceed fixed-console compute but lack console determinism.

## Decision
The Digital Console is defined by its **runtime contracts**, not by hardware:
host qualification, native package (.11g), lifecycle, fidelity negotiation
(VFR), certification, saves/profiles/services. A machine becomes a console by
satisfying a versioned capability contract and running the runtime. Console
identity persists across host replacement.

## Consequences
- Every subsystem must expose host-agnostic contracts (DC-CANON-001 §7, L2–L7
  stable across Windows/ConsoleOS).
- Host-specific code is an implementation detail behind interfaces.
- Acceptance test canon §45 item 18 (same title, stronger host → higher
  validated fidelity, no manual configuration) is a platform-level goal.

## Alternatives rejected
- "PC launcher with console skin" — no runtime ownership, no determinism.
- Custom manufactured box — abandons upgradeability and open development.
