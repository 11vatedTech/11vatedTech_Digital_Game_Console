# ADR-0014 — HDR/VRR display policy

**Status:** Accepted · **Date:** 2026-09-06

## Context
Display topology is heterogeneous (TVs, monitors, HDR or not, VRR or not).
Canon §11 requires capability detection with truthful fallback (C10) and stable
shell presentation. Verified 2026-09-06: DXGI flip model + VRR flags and
AdvancedColorInfo luminance/primaries metadata are current, implementable
surfaces (RESEARCH_LEDGER §5–6).

## Decision
- dc-displayd owns: hotplug, display identity, mode/refresh enumeration, color
  depth/format, HDR/WCG state, luminance metadata, VRR range, safe-area/
  overscan, multi-display ownership, HDMI-audio correlation, per-display
  calibration persistence, display-loss recovery with safe rollback.
- **Shell stays in one stable high-quality mode**; no constant physical mode
  changes. Titles prefer modern flip-model/borderless presentation; nominal
  refresh rates are never hard-coded — enumerated timing data only.
- **HDR:** one system-level calibration flow; result exposed to titles as a
  display profile (supported/active state, bit depth, primaries, white point,
  min/max/maxFullFrame luminance, SDR white, metadata formats). Titles may add
  artistic calibration but must not force repeated hardware calibration.
- **VRR/ALLM/QFT/LIP:** exploited where GPU+driver+cable+display expose them;
  the runtime works correctly when none are available.
- **CEC is optional**; failure never blocks console use.
- **Hot-unplug policy:** preserve game if safe → try another validated display
  → else suspend title → journal → restore on return.

## Consequences
- Display transitions are certification gates (canon §34.9).
- No silent capability claims: telemetry records actual active state.
