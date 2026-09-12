# ADR-0026 — Fidelity contract ABI, candidate artifact, and governor semantics

**Status:** Accepted · **Date:** 2026-09-12 · **Milestone:** DK0-M4

## Context

DK0-M4 implements the platform's central fidelity thesis for the first time: a
native title authors a *scalable representation* (domains × discrete states
with declared costs), the platform validates it against measured host
capability, and the runtime selects only validated states. Three boundaries
needed durable decisions before the code could be trusted:

1. What the title-facing fidelity ABI actually is.
2. How compiled validation results travel from dev-time to runtime.
3. What the runtime governor is allowed to do at 60 Hz under pressure.

## Decision

### 1. Fidelity contract ABI (`dc.fidelity/1`)

* A contract is a set of **domains**; each domain owns **discrete states**
  with `ordinal`, `utility`, declared costs (`gpu_ms`, `cpu_ms`, `vram_mb`,
  `io_mbps`), and a **transition policy**
  (`INSTANT_SAFE`, `HYSTERETIC_RUNTIME`, `SCENE_BOUNDARY`, `RELOAD_REQUIRED`,
  `RESTART_REQUIRED`).
* Titles never see compiler internals (candidate sets, dominance metadata).
  The title-facing surface is `IFidelityService`: current selection, current
  budget, evidence snapshot. Semantic states only — no preset names.
* Costs may be **UNKNOWN**. Unknown is never treated as zero and never
  accused of exceeding a budget (§17 semantics, tested).

### 2. Candidate artifact (`fidelity.candidates.json`)

* The offline compiler (`dc-fidelity-compile`) — never the runtime — performs
  bounded enumeration, budget evaluation, and Pareto dominance filtering.
  Guardrails (`MAX_DOMAINS`, `MAX_STATES_PER_DOMAIN`, `MAX_COMBINATIONS`)
  fail closed; no silent truncation.
* The artifact is **deterministic**: identical inputs produce byte-identical
  output (verified by repeat-compile SHA-256 test). No timestamps, no
  machine-local identity inside the canonical payload.
* The artifact binds to its contract by **canonical contract SHA-256**
  (`contract_sha256`). The runtime loader rejects stale artifacts
  (`FIDELITY_VERSION_MISMATCH`) — an edited contract invalidates compiled
  candidates instead of silently driving the title with old assumptions.
* Budgets are derived **once, in the core** (`BudgetFromHostEvidence`) from
  the latest immutable qualification run and shared by the compiler tool and
  the runtime — a single derivation can never diverge.
* The artifact carries **domain transition policies**, so runtime legality
  checks need only the artifact (self-contained, contract-hash-bound).

### 3. Governor semantics

* The governor is deliberately small: it consumes only validated candidates,
  chooses the best budget-admissible candidate for the intent, and paces
  changes. It never invents combinations the compiler did not validate.
* **Hysteresis + dwell**: pressure must persist `down_windows` observation
  windows before a step-down; health must persist `up_windows` before a
  step-up. Values are versioned policy data, not hardcoded constants.
* **Binding-axis step-down (no oscillation, §39)**: under sustained pressure
  the replacement must be strictly lighter **on the axis that raised
  pressure**. Any-axis lightness allowed Pareto trade-off ping-pong (observed
  in live stress testing: down → up → down within 24 windows); binding-axis
  lightness produced a monotone descent under sustained overload.
* **Transition legality (§23)**: runtime adaptation may only change domains
  whose policy is `INSTANT_SAFE` or `HYSTERETIC_RUNTIME`. `SCENE_BOUNDARY` /
  `RELOAD_REQUIRED` / `RESTART_REQUIRED` domains are frozen at their launch
  state; a requested sim-density change is refused with visible evidence.
* Intent changes (`SetIntent`) are runtime transitions and obey the same
  legality rules.
* Decision events are a **low-frequency reason-coded trace** (`INITIAL_
  SELECTION`, `*_PRESSURE`, `RECOVERY`, `PLAYER_INTENT_CHANGED`, …) with
  before/after candidates and the binding detail — emitted live, not only at
  clean exit, so evidence survives abnormal termination.

## Consequences

* The same (host evidence, contract, intent, policy version) always yields
  the same selection — deterministic and explainable, per §5.
* Fidelity data is loaded from the **installed package generation view**
  only; the source tree is never consulted by the acceptance path (§28).
* GPU-side telemetry (timestamp queries) degrades honestly to UNKNOWN under
  the current driver blocker; CPU-axis adaptation remains real and measured.
  Visual-difference evidence for the GPU leg stays `BLOCKED_DRIVER` until the
  DevKit-0 driver stack allows PSO workloads (see
  `evidence/milestones/DK0-M4-status.json`).

## References

* `docs/canon/DC-BLUEPRINT-001.md` §Fidelity Compiler / §Fidelity Governor
* ADR-0022 (host ≠ session capability), ADR-0023 (evidence policy),
  ADR-0025 (`.11g` package format)
* `evidence/fidelity/m4-acceptance/` (live decision traces, bisect record)
