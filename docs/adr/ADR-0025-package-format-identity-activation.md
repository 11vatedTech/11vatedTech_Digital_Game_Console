# ADR-0025 — `.11g` package format, content identity, and activation model

**Status:** Accepted · **Date:** 2026-09-10 · **Milestone:** DK0-M3

## Context

Before DK0-M3 the shell launched titles from the source tree through a
registry entry whose `entrypoint` was an executable path. The package — not
the platform — was the de-facto unit of trust, identity, and update. The
canon (§15.3, §21) requires `.11g` packages to be self-describing,
content-addressed, inspectable, and atomically activatable.

## Decision

### 1. Logical model is the contract; the container is replaceable

`dc/package.hpp` defines what a package **is** (manifest model, identity,
verification). `dc/package_container.hpp` defines how DK0 **stores** it: a
deterministic tar-like container (magic `11GB`, sorted members, offsets, no
timestamps). No zip/7z semantics reach the public contract. The container
can be replaced without touching the model.

### 2. Identity = SHA-256 over the canonical (package_id-excluded) manifest

- Canonical form: strict JSON, sorted keys (`std::map`), compact, normalized
  derived fields, **declared `package_id` excluded** (identity cannot hash
  itself).
- Verifier recomputes and compares to the declaration
  (`PACKAGE_IDENTITY_MISMATCH` on any tamper).
- Filename, path, and timestamps never contribute; renaming is invisible;
  any content or metadata change flips identity. Proven by
  `test_package_contract` (determinism, rename-invariance, byte-flip).

### 3. SHA-256 implementation in core, crypto delegated to the OS

Core carries a FIPS 180-4 SHA-256 (validated against NIST vectors and
cross-checked against Windows BCrypt in tests) so identity is host-agnostic.
Asymmetric crypto (ECDSA P-256) is **CNG/BCrypt in the host layer** — never
invented, never in core.

### 4. Trust is orthogonal to identity

`UNSIGNED` / `SIGNED_TRUSTED` / `SIGNED_UNTRUSTED` / `SIGNATURE_INVALID`.
DK0 accepts unsigned packages only via the explicit `--allow-unsigned`
development policy; verification always reports the real state. Signed ≠
safe, unsigned ≠ malicious.

### 5. Content-addressed store + generation-based activation

```text
library/store/objects/<h2>/<h62>              deduplicated objects
library/titles/<game>/generations/gNNNN/      immutable materialized views
library/titles/<game>/active.json             atomic activation pointer
```

Install = verify → import → materialize candidate → re-verify → atomic
pointer swap. A failed install never touches `active.json`; rollback is
pointing the pointer at a prior generation. Proven by
`test_package_contract` and `test_package_e2e`.

### 6. The package is the authoritative launch source

The shell resolves the active generation, runs host-profile preflight
(DCP-\*) and session-profile preflight (DCX-\*) as distinct checks (ADR-0022
held), then launches the packaged executable with the **generation view as
working directory** and the typed launch context (ADR-0024). The registry
entrypoint remains a development fallback only.

### 7. Save ownership is (user_id, game_id, save_schema)

No version component in save paths: patches cannot orphan saves. Breaking
save changes bump `save_schema` and declare `migratable_from`; undeclared
breaks are reported incompatible, never silently destroyed.

## Consequences

- DK0 accepts only fully verified packages; every rejection carries a
  precise reason code (`ReasonName`).
- Package updates are cheap: unchanged objects deduplicate in the store.
- Deterministic packaging makes two builds of the same source
  byte-identical — a prerequisite for future reproducible-distribution work.
- The store stays local-first; no CDN/package-service assumptions leaked
  into core.
