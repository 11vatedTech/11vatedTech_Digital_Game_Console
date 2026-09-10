# ADR-0016 — ConsoleOS immutable/A-B update architecture

**Status:** Accepted · **Date:** 2026-09-06

## Context
A failed platform update must never brick a functioning console (C12). Verified
2026-09-06: RAUC provides atomic A/B slot updates, signed bundles, boot-attempt
counting with automatic rollback (default 3 attempts) and mark-good flow;
systemd defines boot assessment concepts (RESEARCH_LEDGER §11).

## Decision
- ConsoleOS layout: EFI System Partition; **System A (read-only image)**;
  **System B (read-only image)**; persistent /var; game content store; user/
  save data; recovery tools (canon §31.2).
- Update flow: boot A → download/verify signed B → write inactive B → mark B
  trial → reboot B → health gates → mark B good; repeated boot/health failure
  → bootloader selects A automatically.
- **Reuse proven machinery** (RAUC/systemd boot assessment) over a custom
  updater for the earliest ConsoleOS generation.
- Update channels: stable / preview / developer. Moving to a less stable
  channel is explicit; developer channel never silently becomes consumer
  default.

## Consequences
- ConsoleOS implementation (Phase 7) is gated behind Windows-host contract
  validation, per ADR-0002.
- Health-gate definition (what "good" means) is a certification artifact.
