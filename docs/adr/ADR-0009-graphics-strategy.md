# ADR-0009 — Graphics strategy: D3D12 (Windows) / Vulkan (ConsoleOS)

**Status:** Accepted · **Date:** 2026-09-06

## Context
Native titles need a graphics API per host. Inventing a proprietary low-level
API risks worse fidelity and massive adoption friction (blueprint §19; canon
§19.1).

## Decision
- Windows native baseline: **Direct3D 12**.
- ConsoleOS native baseline: **Vulkan**.
- A title may ship both backends through its engine; the platform does not
  invent its own low-level graphics API unless a proven platform limitation
  demands one (future ADR required).
- Reconstruction is a vendor-neutral interface (color/depth/motion vectors/
  exposure/masks/jitter in; policy out) with backends: title-native, FSR, XeSS,
  DLSS where licensed. A non-proprietary fallback MUST remain for certification
  unless a capability profile explicitly requires a vendor technology (canon
  §19.2).
- Ray tracing is a capability domain, not a platform requirement.
- Frame generation telemetry always reports simulation_hz, render_hz,
  generated_present_hz, end_to_end_latency independently (C7).

## Consequences
- SDK graphics abstractions are thin (capability + reconstruction + swapchain
  policy); engines keep full control.
- Certification requires per-backend evidence.
