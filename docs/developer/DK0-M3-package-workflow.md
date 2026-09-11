# DK0-M3 `.11g` Package Workflow (developer)

Everything below runs offline. The packaged artifact — not the source-tree
executable — is the thing the console launches.

## One-time: generate a signing key (optional but recommended)

```bash
# CNG ECDSA P-256 key pair. Private blob stays local; never commit it.
# (Key generation tool ships with the test binary; see
#  tests/contract/test_package_signing.cpp GenerateSigningKeyPair usage.)
```

DK0 policy: unsigned developer packages are accepted only via the explicit
`--allow-unsigned` flag on `dc-packaged install`. The verifier always reports
the true trust state (`UNSIGNED` / `SIGNED_TRUSTED` / `SIGNED_UNTRUSTED` /
`SIGNATURE_INVALID`).

## Build the package

```bash
./build/Release/dc-pack packages/fidelitylab ./FidelityLab.11g
```

- Hashes every content file (SHA-256), builds `dc.package/1` canonical
  manifest, computes `package_id`, **verifies its own output** before writing.
- Deterministic: same inputs → byte-identical `.11g` (no timestamps/paths).
- With signing: `dc-pack packages/fidelitylab ./FidelityLab.11g --sign <priv-hex>`

## Inspect / verify

```bash
./build/Release/dc-verify-package ./FidelityLab.11g
./build/Release/dc-verify-package ./FidelityLab.11g --json
```

Emits precise reason codes on any failure:
`PACKAGE_SCHEMA_INVALID`, `PACKAGE_HASH_MISMATCH`, `PACKAGE_IDENTITY_MISMATCH`,
`PACKAGE_ENTRYPOINT_MISSING`, `PACKAGE_RUNTIME_INCOMPATIBLE`,
`PACKAGE_PLATFORM_UNSUPPORTED`, …

## Install + activate (atomic; rollback-safe)

```bash
./build/Release/dc-packaged install ./FidelityLab.11g "$LOCALAPPDATA/11vated/console/library" --allow-unsigned
./build/Release/dc-packaged active tech.11vated.fidelitylab "$LOCALAPPDATA/11vated/console/library"
./build/Release/dc-packaged generations tech.11vated.fidelitylab "$LOCALAPPDATA/11vated/console/library"
./build/Release/dc-packaged rollback tech.11vated.fidelitylab g0001 "$LOCALAPPDATA/11vated/console/library"
```

Library layout:

```text
library/
  store/objects/<h2>/<h62>                      content-addressed objects
  titles/<game-id>/generations/gNNNN/...        immutable generation views
  titles/<game-id>/active.json                  atomic activation pointer
```

A failed install never touches `active.json` (§11 test:
`test_package_contract` "v2 STILL active after failed v3 install").

Re-running install with the identical package is **idempotent** (same
content identity + version → same active generation; generations do not
stack). One-shot helper (also used by the CTest fixture):

```bash
bash tools/pack/fidelitylab.sh          # stage built title + pack + verify
bash tools/pack/install_library.sh      # install + activate + print state
```

## Launch path (what DK0-M3 changed)

```text
installed package → validated manifest → resolved entrypoint
  → host-profile preflight → title supervisor → packaged executable
```

The shell resolves the title through `dc::package::Library` first
(`DC_LIBRARY_DIR`, default `%LOCALAPPDATA%/11vated/console/library`); the
registry entry `registry/tech.11vated.fidelitylab.title.json` (game_id
matches the package) supplies shell metadata and remains the documented
development fallback when no package is installed. Launches use the typed
`DC_TITLE_CONTEXT/1` envelope (ADR-0024) with the package's lifecycle
contract and carry the `PACKAGED <game> g=<gen>` marker in the session log.

Acceptance proof: CTest `package_library_install` (fixture) +
`session_shell_selftest` — pack→verify→install→activate→shell launch of the
**packaged** executable → QR1 suspend/resume (ok/ok) → clean exit →
SHELL_ACTIVE. `tests/contract/test_package_e2e.cpp` covers the same chain
headlessly, including v0.1.0→v0.1.1 update and corrupt-v0.1.2 rollback.
