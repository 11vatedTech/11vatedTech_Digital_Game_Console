# ADR-0015 — DirectStorage/GDeflate Windows IO path

**Status:** Accepted · **Date:** 2026-09-06

## Context
Modern consoles treat storage as part of the graphics architecture (Xbox
Velocity Architecture). Verified 2026-09-06: DirectStorage 1.3 is stable with
GPU GDeflate decompression; 1.4 preview adds Zstd + Game Asset Conditioning
Library (RESEARCH_LEDGER §7). Canon §14 defines storage classes and IO QoS.

## Decision
- Windows native titles may use DirectStorage directly or via SDK abstraction;
  the SDK enables GPU-decompression-friendly layouts when GDeflate is selected,
  authored with 64 KiB tile + batching awareness.
- Package chunk codecs (ADR-0005): Zstd portable, GDeflate Windows GPU path,
  raw for latency-critical pre-compressed data. Codec support is part of the
  host capability record.
- Storage classes: DC_STORAGE_LEGACY → SATA_SSD → NVME_BASE → NVME_HIGH →
  STREAMING_CERTIFIED. Titles declare storage requirements semantically.
- **IO QoS priority classes:** CRITICAL_STREAM > GAMEPLAY_STREAM >
  BACKGROUND_PRELOAD > PATCH > CAPTURE_WRITE > CLOUD_SYNC. Game loading must
  not be unpredictably starved by background patching or capture uploads.
- Storage capability feeds host qualification (a large GPU behind a slow HDD
  does not qualify like one behind NVMe).

## Consequences
- DirectStorage 1.4 tracked as preview; not a baseline dependency until stable.
- Streaming behavior is a certification gate (canon §34.8).
