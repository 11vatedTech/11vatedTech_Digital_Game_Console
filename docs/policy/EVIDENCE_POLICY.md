# Qualification Evidence Policy — dc.policy/1

Status: Accepted (v1) · Date: 2026-09-08
Companion: ADR-0023 (immutable evidence architecture)

## Normative rules

1. **Immutability.** Every qualification run writes an immutable directory
   `evidence/qualification/runs/<run-id>/` (manifest, host-capability,
   benchmarks, session-profiles). A prior run is never modified or deleted.
   `evidence/qualification/index.json` is the append-only run index.
2. **No cherry-picking.** Certification derives from policy over the full run
   set (`dc-qualify-report`). Restoring an earlier "good" run as canonical
   evidence because a later run is worse is prohibited.
3. **Aborted ≠ failed capability.** Aborted benchmarks are recorded as
   aborted, never consumed as measured scores (C10).
4. **Conservative aggregate.** Profile certification uses the **median**
   across valid runs (never the maximum). p10/p90 and coefficient of
   variation are reported for transparency; high CV (>0.15) marks the domain
   as environment-sensitive and disqualifies certification claims until
   quiet-system conditions are met.

## Profile claim levels

| Level        | Requirement                                                        |
|--------------|--------------------------------------------------------------------|
| DEVELOPMENT  | ≥1 valid (all-domain-complete or honestly-partial) run, mode quick or standard |
| CERTIFIED    | ≥3 valid **certification-mode** runs, domain complete in all, median ≥ threshold, CV ≤ 0.15, quiet-system conditions recorded |

N=3 for CERTIFIED is the smallest set that distinguishes persistent
capability from a lucky run while remaining practical on dev hardware. This
number is **policy, not code**: it lives here and in `dc-qualify-report`
defaults, and may be revised with data by updating this file (version bump).

## Environment conditions

Certification runs must record: time since previous qualification, power
source, background load indicators. A machine heat-soaked by back-to-back
runs measures its own testing mistake (directive §46); such runs are tagged
and excluded from CERTIFIED aggregates.

## Status vocabulary (mandatory, directive §47)

DESIGNED · IMPLEMENTED · COMPILED · UNIT_TESTED · INTEGRATION_TESTED ·
RUNTIME_VERIFIED · BENCHMARKED · CERTIFIED · BLOCKED_ENVIRONMENT ·
BLOCKED_DRIVER. Never collapse these.
