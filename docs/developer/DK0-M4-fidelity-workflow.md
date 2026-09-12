# DK0-M4 — Fidelity workflow (contract → compile → package → run)

The complete fidelity loop against the packaged title. All fidelity data is
loaded from the **installed package generation view** — never the source tree.

## 1. Compile the fidelity candidate artifact

Budgets are derived from the latest immutable qualification run
(`evidence/qualification/runs/*/host-capability.json`) by the shared core
derivation (`BudgetFromHostEvidence`) — identical to what the runtime uses.

```bash
./build/Release/dc-fidelity-compile \
    packages/fidelitylab/content/manifests/fidelity.json \
    --evidence "$(ls -d evidence/qualification/runs/*/ | tail -1)" \
    --out packages/fidelitylab/content/manifests/fidelity.candidates.json
```

The artifact is byte-deterministic (same inputs ⇒ same SHA-256) and binds to
the contract by canonical contract hash. Editing `fidelity.json` afterwards
invalidates the artifact at load time (`FIDELITY_VERSION_MISMATCH`).

## 2. Package + install

```bash
bash tools/pack/fidelitylab.sh        # builds FidelityLab.11g (incl. fidelity manifests)
bash tools/pack/install_library.sh    # verify → import → atomic activation
```

## 3. Run

- **Through the shell (normal)**: launch FidelityLab from the console shell.
  The Home view's `FIDELITY: AUTOMATIC` tile cycles the intent
  (AUTOMATIC → RESPONSIVE → BALANCED → CINEMATIC); the System view shows
  developer diagnostics.
- **Direct (developer harness)**:

```bash
GEN=$(ls -d "$LOCALAPPDATA/11vated/console/library/titles/tech.11vated.fidelitylab/generations/"*/ | tail -1)
cd "$GEN/windows-x64"
DC_FIDELITY_STRESS=1 ./FidelityLab.exe        # sustained CPU-pressure harness (§39)
DC_FIDELITY_MODE=visual_stress ./FidelityLab.exe
DC_FIDELITY_PIN="<candidate-id>" ./FidelityLab.exe   # dev pin (validated set only)
DC_TITLE_GPU=0 ./FidelityLab.exe              # pin adapter index explicitly
```

## 4. Read the evidence

| Line | Meaning |
| --- | --- |
| `fidelity: selected …` | governor initial selection (reason + intent) |
| `fidelity: states …` | the 7 domain states actually applied to rendering/simulation |
| `fidelity-metrics: wN …` | 1 Hz measured telemetry window (gpu_ms=-1 ⇒ honest UNKNOWN) |
| `fidelity-trace: wN A -> B (REASON) detail` | decision events, emitted live |
| `fidelity-final: …` | exit footer: selection + trace + measured cost |
| `fidelitylab: DEVICE_REMOVED …` | structured GPU device-loss evidence (exit 3) |

## Notes

* `simulation_quality` is RESTART_REQUIRED — the governor refuses runtime sim
  changes (visible refusal line); density is fixed at launch.
* `reflection_quality` is SCENE_BOUNDARY — likewise frozen at runtime.
* The player-facing surface is intent-only; raw domain states are a
  developer diagnostic (§34/§35).
