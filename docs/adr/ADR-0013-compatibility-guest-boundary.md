# ADR-0013 — Compatibility guest boundary

**Status:** Accepted · **Date:** 2026-09-06

## Context
Steam/Epic/GOG/Win32/Proton/emulators are essential content but must not define
native platform semantics (C8). Playnite/Xbox Mode prove aggregation is not a
differentiator (blueprint §23). Store/DRM terms must not be violated (canon
§44).

## Decision
- Compatibility runs through **adapters** implementing: DiscoverInstalledTitles,
  ResolveMetadata, CanLaunch, Launch, RequestStop, DetectExit,
  FindSaveLocations, GetControllerHints, GetKnownDisplayOverrides,
  GetKnownCompatibilityIssues (canon §27.1).
- dc-compatd owns adapters; launchers are hidden where feasible but store
  authentication/DRM is never broken — a launcher requiring interaction is
  presented as a **compatibility surface**, honestly labeled.
- A versioned, evidence-backed **compatibility knowledge base** records launch
  commands, focus behavior, controller support, display behavior, save paths,
  suspend safety, known anti-cheat restrictions, per-title workarounds.
- Compatibility titles run under a separate, less restrictive process policy
  than native titles (canon §8.1); they are never a dependency of native core
  services.

## Consequences
- Shell library visibly distinguishes Native (`.11g`) vs Compatibility titles.
- Adapter failures never degrade native runtime guarantees.
