# ADR-0011 — Transactional saves

**Status:** Accepted · **Date:** 2026-09-06

## Context
Power loss or crash during a save commit must never corrupt the canonical save
(canon §23.2; Gate P4/L4). Saves are platform-owned for native titles and must
be offline-first (C9).

## Decision
- Platform-owned **save service (dc-saved)** with a transaction API:
  `Begin(slot)` → `Write(key, blob)` → `Commit()`.
- Commit is **atomic**: journal + generation-based. A power loss during commit
  leaves either the previous valid generation or the new valid generation —
  never a partial canonical save.
- Requirements (canon §23.1): multiple generations/recovery points, schema/
  version metadata, per-user ownership, offline-first, corruption detection
  (hashes), storage-pressure policy, optional encryption for sensitive
  payloads, optional sync-provider interface, conflict-resolution metadata.
- Cloud sync is provider-abstracted; core console never requires a paid
  third-party API. Initial builds are local-only.

## Consequences
- Save format and journal protocol live in `formats/package/` (save schema is
  package-declared; journal format is platform-owned).
- Corrupt-save recovery is a certification gate (canon §34.4).
