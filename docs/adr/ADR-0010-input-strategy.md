# ADR-0010 — Input strategy: GameInput primary, SDL3 portable fallback

**Status:** Accepted · **Date:** 2026-09-06

## Context
Controller-first operation (C3) requires low-latency input, haptics, hot-plug
resilience, and broad device coverage on both hosts (canon §12). Verified
2026-09-06: GameInput is Microsoft's current unified low-latency input API with
haptics/motion/force-feedback (RESEARCH_LEDGER §3–4).

## Decision
- **Windows:** Microsoft GameInput primary backend; **SDL3** portable/fallback
  abstraction; vendor SDK adapters only for optional proprietary features.
- **ConsoleOS:** evdev/hidraw/libinput/SDL3-class integration; BlueZ Bluetooth;
  vendor adapters optional.
- The platform exposes a **semantic device model** (ConsoleDevice identity/
  connection/battery/player_assignment/capabilities; GamepadSemanticState;
  ExtendedState for gyro/touch/paddles/adaptive triggers/haptics/LEDs). Titles
  target baseline semantics and discover richer optional capabilities.
- The runtime owns: Guide/system button, player assignment, reconnect,
  active-user association, shell focus, battery notifications, remapping/
  accessibility transforms. Games receive resolved player/controller identity.
- Haptics are semantic channels (low/high frequency, impulse L/R, surface
  texture, impact, continuous force); unsupported channels degrade gracefully.

## Consequences
- `dc-inputd` backend interface in `runtime/host` with two implementations
  planned (GameInput, SDL3); semantic layer is backend-independent.
- Guide-button routing is a runtime responsibility, never a title's.
