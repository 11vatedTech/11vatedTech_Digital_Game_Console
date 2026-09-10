# ADR-0003 — C++23 service-oriented runtime architecture

**Status:** Accepted · **Date:** 2026-09-06

## Context
The runtime needs systems-level control (input latency, frame pacing, memory
pressure, GPU APIs, package IO), long-term ABI discipline for the SDK, and
process isolation so component failures do not cascade (canon §8).

## Decision
- Primary language: **C++23**. Secondary research/tooling: Python 3.12+.
- Multi-process production model with named services (dc-hostd, dc-sessiond,
  dc-packaged, dc-shell, …). Not every service must be a separate executable in
  DevKit-0, but **service boundaries are preserved** so separation later cannot
  break APIs.
- Privilege model: dc-hostd/dc-packaged/dc-securityd/dc-updated may hold
  platform privileges; shell, games, UX services run unprivileged; native
  titles least-privilege with declared capabilities.
- No mutable global service state; explicit lifetime and dependency injection
  through versioned contexts (canon §16.2).

## Consequences
- Interfaces in `runtime/core` are host-agnostic; hosts implement
  `runtime/host` interfaces.
- Each logical service gets an isolated translation unit boundary and (later)
  its own process binary without API churn.

## Alternatives rejected
- Single monolithic process (crash propagation violates Gate P7).
- C (no type safety for contract surfaces), Rust (ecosystem/tooling decision
  left open for future ADR if evidence demands; C++23 chosen per canon).
