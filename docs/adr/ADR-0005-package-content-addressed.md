# ADR-0005 — .11g package: content-addressed format

**Status:** Accepted · **Date:** 2026-09-06

## Context
Native titles must install without arbitrary executable installers, update
differentially, verify integrity independently, and activate atomically
(canon §15). The currently playable version must never be mutated by a partial
update (canon §15.4; Gate P4).

## Decision
`.11g` (provisional name) is a **content-addressed chunk store** with signed
manifests:
- Logical layout per canon §15.2 (manifest/, binaries/<host-triple>/, shaders/,
  content/{base,optional,fidelity-packs}/, metadata/, signatures/).
- Physical form: chunked + indexed, never a naïve folder archive.
- Install flow: resolve manifest → import/verify chunks → verify signed
  metadata → construct candidate view → preflight → atomic activation → retain
  rollback metadata.
- Content identity: SHA-256 cryptographic baseline (canon §15.5); faster local
  fingerprints only as dedup acceleration.
- Codecs per chunk: Zstd (portable), GDeflate (Windows GPU decompression),
  raw.

## Consequences
- `formats/package/` owns the chunk/index/manifest specification.
- dc-packaged owns install/mount/verify/update; nothing else mutates packages.
- Differential updates and optional fidelity packs fall out naturally.

## Alternatives rejected
- Installer executables (violates C2).
- Single-file archives (no dedup, no atomic update, poor streaming).
