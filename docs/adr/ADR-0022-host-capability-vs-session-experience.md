# ADR-0022 — Host capability vs session experience

**Status:** Accepted · **Date:** 2026-09-07

## Problem

`DCP-2026-4K60` / `DCP-2026-4K120` combined GPU/CPU/VRAM thresholds with a
display-refresh requirement in one profile. Two unrelated facts were thus
collapsed into one claim:

1. what the **host** (compute hardware) can sustain under the platform's
   reference workload; and
2. what the **currently attached display + presentation path + OS state**
   can actually deliver right now.

A user with a console-class host and a 1080p panel was truthfully render-
capable of UHD120 while unable to *experience* UHD120 — and the old profile
model could not express that distinction. It also failed canon C5/C10 in the
report: "4K120 PASS" claimed an experience the session did not deliver.

## Alternatives considered

- **One profile namespace with optional display requirements** (status quo):
  rejected — the ambiguity is the bug; derivation results could not be reused
  when display topology changes.
- **Separate namespaces `DCP` (host) / `DCX` (session experience)**: adopted.
  Names are short, distinct, and match the canon vocabulary (capability vs
  experience). Version stays embedded (`-2026-`), as with all platform
  identifiers.
- **`HOST-`/`SESSION-` prefixes**: rejected — noisier, and `DCP` already has
  registry identity and evidence history worth preserving.

## Selected model

Two versioned profile families, one derivation engine each:

```text
HostCapabilityRecord        ──► DCP-*  (host profiles)   — deterministic
DisplayCapabilityRecord  ┐
PresentationCapabilityRecord├─► DCX-*  (session profiles) — deterministic
Host DCP results         ┘
```

- **DCP profiles** never require display state. Requirements resolve only
  against measured host domains (cpu, gpu, memory, storage) and host
  discovery truth (VRAM/RAM bytes).
- **DCX profiles** are session-scoped. Requirements may be:
  - `host_profile` — the named DCP must be satisfied by the current host
    derivation (reason `HOST_PROFILE_UNSATISFIED` otherwise);
  - `mode` — an enumerated display mode must exist with
    `width >= min_width ∧ height >= min_height ∧ refresh >= min_refresh_hz`
    (rational arithmetic; reason `DISPLAY_MODE_UNAVAILABLE`);
  - `display.hdr_active` — HDR enabled on the current output
    (`HDR_INACTIVE`);
  - `display.vrr_proven` — VRR actively proven (`VRR_UNPROVEN`).
- Mode predicates are conjunctive over a single mode: independent max-width
  and max-refresh maxima may NOT combine a 4K@30 mode with a 1080p@240 mode.

### Rational refresh

`DisplayMode` carries `refresh_numerator` / `refresh_denominator`. The
Windows CDS enumeration (`EnumDisplaySettingsEx`) reports integer Hz — the
probe records `denominator = 1` honestly rather than inventing precision;
the QueryDisplayConfig rational path is the future source when wired.
`refresh_hz` remains the derived double for human reporting.

## Migration

- `DCP-2026-4K60` → `DCP-2026-RENDER-UHD60` (display requirement removed;
  `supersedes: DCP-2026-4K60` recorded in the file).
- `DCP-2026-4K120` → `DCP-2026-RENDER-UHD120` (same).
- `DCP-2026-HDR` stays a **host** profile (pipeline render capability);
  the display fact moves to the new `DCX-HDR`.
- `DCP-2026-LOWLATENCY` loses `display.vrr_proven` (host-only: response-
  capable CPU/GPU); the display fact moves to the new `DCX-VRR`.
- New session registry: `DCX-UHD60`, `DCX-UHD120`, `DCX-HDR`, `DCX-VRR`.
- Old IDs are not silently dropped: each migrated file records
  `supersedes`, old evidence files remain readable (their profile registry
  is part of the evidence identity), and the contract gate keeps the
  `dc.profile/1` schema validation for both families via the `kind` field.

## Consequences

- **Reports** show two truth sections: HOST CAPABILITIES and SESSION
  CAPABILITIES (PASS / FAIL:<reason> / UNMEASURED / UNPROVEN wording).
- **VFR** consumes `HostCapabilityRecord` + session truth separately — a
  fidelity compile is host-scoped; the governor's presentation target is
  session-scoped. `GetHostCapabilities` / `GetSessionCapabilities` (§32 of
  the DK0-M2 directive) map directly onto these two record families.
- **Shell** renders host status and session experience from the two
  derivations independently.
- **Certification** certifies a host against DCP and a session against DCX;
  a display change recomputes DCX (never DCP) — session recompute is cheap
  and display-driven (DK0-M2 §31).
- **DisplayCapabilityRecord / PresentationCapabilityRecord** as standalone
  types arrive with the session service (DK0-M2); until then the session
  derivation reads the display truth already present in
  `HostCapabilityRecord`. This is a documented interim, not a shortcut.
