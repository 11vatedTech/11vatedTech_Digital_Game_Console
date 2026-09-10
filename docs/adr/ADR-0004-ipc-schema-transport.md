# ADR-0004 — IPC schema and transport

**Status:** Accepted · **Date:** 2026-09-06

## Context
Services must exchange control messages and high-frequency telemetry (frame
times, queue depth, input age) without latency or allocation storms, across
Windows and ConsoleOS, with versioned compatibility (canon §8.2).

## Decision
- **Schema:** FlatBuffers-class zero/low-copy versioned schema (final choice at
  implementation ADR); explicit schema versions + backwards-compatibility rules.
- **Transport:** Windows — named pipes + shared memory for high-frequency
  telemetry; ConsoleOS — Unix domain sockets + shared memory.
- Request/response for service calls; publish/subscribe event bus for topology
  and lifecycle events; monotonic timestamps on all latency-sensitive events.
- High-frequency frame telemetry MUST NOT travel as verbose JSON.

## Consequences
- `formats/traces/` defines the compact binary telemetry format separately from
  IPC control schema.
- Human-readable JSON is reserved for tools, manifests, evidence bundles.

## Alternatives rejected
- JSON-everywhere (copy/parse cost, schema drift).
- gRPC (dependency weight, latency profile wrong for frame telemetry).
