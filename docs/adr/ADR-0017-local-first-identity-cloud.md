# ADR-0017 — Local-first identity / cloud-provider abstraction

**Status:** Accepted · **Date:** 2026-09-06

## Context
Offline operation is foundational (C9). Social/cloud services must never
become boot dependencies. Canon §24 defines profiles/achievements/presence as
services, not prerequisites. Canon §44 forbids cloud-required consoles and
mandatory paid AI/API subscriptions.

## Decision
- **Local profiles** are first-class: display name, avatar, controller
  preferences, accessibility preferences, display calibration references, save
  ownership, local achievement journal, privacy settings — all functional with
  zero connectivity.
- Achievements are event-driven and **locally journaled** with idempotency;
  optional online sync is a later extension.
- No centralized proprietary social graph is required for the first viable
  console; later social SHOULD use open/federated service boundaries.
- Cloud save sync is a **provider-abstracted interface** (ADR-0011); core
  never requires a paid third-party API.
- Telemetry is local-first; upload is opt-in or scoped to essential crash/
  security behavior; local diagnostics require no account (canon §36).

## Consequences
- Every service contract has a local implementation and clearly optional
  network extension.
- Boot path contains no network waits; heavy network/content calls begin after
  local shell readiness (canon §32.3).
