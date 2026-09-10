# ADR-0024 — Native title launch context: typed envelope transport

**Status:** Accepted · **Date:** 2026-09-10

## Context

Canon §16 defines the native title runtime contract (typed `LaunchContext`,
injected services) and §16.2 the `ConsoleServices` context. Until now the
sample title received *no* runtime context: a supervised launch passed only
`CreateProcess` arguments, and the title admitted "ConsoleServices pipe will
replace this in DK0-M3". Titles therefore could not learn session identity,
display mode, or certified capability truth from the platform.

The launch context must cross a **process boundary owned by the title
supervisor** without violating the platform constitution:

- No secrets in transit (§C11); the context is advisory capability truth.
- Titles must never parse loose per-variable platform state (canon §30:
  "Do not pass giant raw JSON blobs if a typed contract is already
  available" — the correct reading is *typed schema*, not *no serialization*).
- Host-agnostic core (§39.1): the contract types cannot include Windows
  headers, and the core contract-gate forbids OS includes in `runtime/core`.
- The transport must be replaceable by the canon §16.2 ConsoleServices pipe
  (DK0-M3) without changing the types titles consume.

## Alternatives considered

1. **Command line** — visible via tasklist/Process Explorer, length-limited,
   argv-escaping hazards with embedded JSON. Rejected.
2. **Temp file + path on command line** — adds a file-lifecycle contract
   (cleanup, permissions, stale files after crashes) for no benefit at this
   scale. Rejected for now.
3. **Named shared memory / COM** — premature: requires a runtime service
   process that does not exist yet (that *is* DK0-M3's ConsoleServices work).
4. **Versioned environment envelope (selected)** — the supervisor constructs
   the child's environment block; the envelope is one variable containing a
   schema-tagged JSON document (`dc.title-context/1`), parsed with the core
   strict parser. No visible command line, no file lifecycle, no new
   process.

## Decision

- `runtime/core/dc/title_context.hpp` defines the typed
  `TitleLaunchContext` (identity, session, rational display mode per
  ADR-0022, capability documents, lifecycle flags).
- `runtime/core/dc/title_context_json.hpp` defines the `DC_TITLE_CONTEXT/1`
  envelope (serialize + strict parse). A foreign or missing schema id is a
  hard rejection — titles never guess.
- `ITitleSupervisor::Launch` gains a typed overload; the Windows
  implementation injects the envelope into the child environment block
  (inherited env + one entry; stale envelopes are never forwarded).
- The envelope carries `host_capability` verbatim (the exact certified
  `dc.host-capability/2` document) — the title sees what the platform
  certified, with no shell-side recombination (§40).
- Transport is explicitly an **embryo**: the DK0-M3 ConsoleServices pipe
  replaces the environment transport; the types and schema stay.

## Consequences

- Titles tolerate an absent envelope (direct-run/development) but reject
  foreign schemas — enforced by contract tests, including a **live
  cross-process proof** (parent supervises a child copy of itself; the child
  verifies every field through the real env-block transport).
- The Guide-ownership flag is contract-enforced on the title side: a context
  claiming `guide_owned_by_platform == false` is refused (§34 — titles never
  own the system action).
- Player-visible console semantics (semantic input, no backend type leaks)
  are unchanged; the sample title now demonstrates consumption of both the
  context and the semantic router.

## Status vocabulary

- Contract types + envelope + supervisor transport: **IMPLEMENTED /
  UNIT_TESTED / RUNTIME_VERIFIED** (cross-process live test).
- ConsoleServices pipe (canon §16.2 full services): **DESIGNED** (DK0-M3).
