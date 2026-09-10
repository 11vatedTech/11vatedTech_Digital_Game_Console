# ADR-0012 — Native suspend/resume contract

**Status:** Accepted · **Date:** 2026-09-06

## Context
Quick Resume on fixed consoles works because the OS/runtime architecture owns
it; transparent snapshots of arbitrary GPU-heavy PC games remain research-grade
(verified 2026-09-06 — CRIU GPU plugins are constrained and topology-dependent;
RESEARCH_LEDGER §12). Canon §22 requires lifecycle cooperation for native
titles.

## Decision
- Native titles implement the lifecycle contract (OnSuspend/OnResume with
  deadlines): quiesce external work, journal state, flush transactional saves,
  handle network resources, serialize resume state, acknowledge before timeout.
- Resume: validate user, recreate device/display/audio resources, reinit
  network if needed, restore checkpoint, **revalidate VFR profile** (host/
  display may have changed), continue at acceptable gameplay location.
- Quick Resume tiers: **QR0** (restart only), **QR1** (title-authored checkpoint
  + fast reload), **QR2** (native process suspension + checkpoint safety),
  **QR3** (disk-backed native snapshot where host supports it). Only QR1/QR2 are
  launch-critical; QR3 is a later optimization.
- Legacy titles: best-effort process suspend + adapter hints, always labeled
  best-effort; never presented as native Quick Resume.

## Consequences
- Title supervisor (dc-sessiond) enforces suspend timeouts and QR tier truth
  in metadata; certification verifies tier claims (Gate L4).
- QR3 requires a future ADR per host backend.
