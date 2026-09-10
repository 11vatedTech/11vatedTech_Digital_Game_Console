# ADR-0006 — Package signing and TUF-inspired update trust

**Status:** Accepted · **Date:** 2026-09-06

## Context
Package updates are a primary attack surface: repository compromise, rollback
and freeze attacks, key compromise, malicious mirrors (canon §30.2; Gate P8).
Verified 2026-09-06 that TUF remains the current, maintained update-security
specification (RESEARCH_LEDGER §10).

## Decision
- Package trust follows a **TUF-inspired model**: publisher identity, signed
  manifest metadata, content hashes, expiry/rotation policy for repository
  metadata, rollback/freeze protection for online update metadata, local
  installation provenance.
- Custom cryptographic update protocol design is prohibited. Audited standard
  designs are used (canon §44 anti-goals).
- Offline installs carry signed manifests + provenance; online metadata adds
  TUF role structure when store/update services land.

## Consequences
- dc-securityd owns key/role verification; dc-packaged refuses unsigned or
  expired metadata.
- Key rotation ceremony and role thresholds must be documented before online
  distribution (Phase 2 exit requirement).
