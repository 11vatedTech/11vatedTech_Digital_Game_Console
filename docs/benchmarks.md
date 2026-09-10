# Benchmark methodology — dc.host-capability/2 (DK0-M1)

Every score in a `dc.host-capability/2` record must be reproducible from this
document + the recorded raw samples. Benchmark IDs are `dc-bench/1:<domain>`.
All benchmarks are platform-owned workloads; none embed vendor telemetry or
proprietary SDKs.

## Methodology identifiers

| benchmark_id | methodology_id | units | notes |
|---|---|---|---|
| `cpu.single-thread` | `dc-bench/1:cpu-single` | ops/ms | serial integer/FP mix, deterministic PRNG input |
| `cpu.game-thread` | `dc-bench/1:cpu-gamethread` | ops/ms | scene traversal + entity update + command prep simulation |
| `cpu.worker` | `dc-bench/1:cpu-worker` | ops/ms | parallel task fan-out over `logical_cores - 2` workers |
| `cpu.game-thread.sustained` | `dc-bench/1:cpu-gamethread` | ops/ms | same workload, sustained window (below) |
| `gpu.raster` | `dc-bench/1:gpu-raster` | pixels/ms | vertex+pixel workload at fixed 1280x720 target |
| `gpu.compute` | `dc-bench/1:gpu-compute` | MACs/ms | fp32 raw-buffer compute: 4M elements, 128 threads/group, 16 unrolled MADs/element; SM6.0 DXIL compiled by DXC at authoring time (`compute_bench_cs.hlsl` → `compute_cs_dxil.h`), bound via inline root descriptors |
| `gpu.rt` | `dc-bench/1:gpu-rt` | rays/ms | DXR 1.1 **ray-query** traversal (inline domain, renamed `gpu.rt_inline`): deterministic 4096-triangle BLAS × 8 TLAS instances (PREFER_FAST_TRACE), SM6.5 compute PSO (embedded signed DXIL, `gpu_rt_dxil.h` from `gpu_rt_bench.hlsl`, DXC 1.9.5402), GPU-timestamped; AS build GPU time recorded separately in `elapsed_gpu_time_ms` (never mixed into the score). Full DXR pipeline domain (`gpu.rt_pipeline`, `DispatchRays`) implemented separately — **prior "TraceRay unavailable in DXC" claim was DISPROVEN** (7-arg call arity mistake; real signature takes RayDesc as arg 7; RESEARCH_LEDGER §23). |
| `gpu.copy` | `dc-bench/1:gpu-copy` | MB/ms | device-internal 256 MiB buffer copies |
| `memory.bandwidth` | `dc-bench/1:mem-bandwidth` | MB/ms | large-buffer streaming reads+writes |
| `memory.pressure` | `dc-bench/1:mem-pressure` | (score = safe bytes) | capped allocation ladder, never exceeds 50% of commit limit |
| `storage.seq` | `dc-bench/1:storage-seq` | MB/ms (MB/s) | unbuffered (`FILE_FLAG_NO_BUFFERING`) sequential reads |
| `storage.random` | `dc-bench/1:storage-random` | IO/ms (kIOPS) | 4 KiB unbuffered reads at documented queue depth |
| `storage.sustained` | `dc-bench/1:storage-sustained` | MB/s | streaming window, sustains cache-cold behavior |

## Rules

1. **GPU time is measured with D3D12 timestamp queries**, never wall-clock
   alone. `elapsed_gpu_time_ms` and per-sample `gpu_time_ms` are query-derived;
   `cpu_time_ms` is advisory (host overhead) and never used for GPU scores.
2. **Warmup:** each benchmark runs documented warmup iterations (cache, shader,
   power state stabilization) excluded from samples. GPU benchmarks warm up
   >= 10 frames; storage benchmarks pre-read 256 MiB to stabilize Windows
   read-ahead, then switch to unbuffered IO.
3. **Variance:** a scored run has >= 3 post-warmup samples; `variance` is the
   population stddev of per-iteration scores. Runs whose sample spread exceeds
   25% of the mean are **aborted** (`state=Aborted`, reason `unstable-samples`),
   not scored.
4. **NaN/Infinity:** any non-finite sample invalidates the whole benchmark
   (`Aborted/invalid-sample`). The score engine re-checks and rejects non-finite
   inputs (defense in depth).
5. **Sustained windows** (why these durations: short enough for developer
   iteration, long enough to pass boost clocks on mobile parts — measured
   ramp on laptop silicon completes well inside 45 s):
   - `quick`: 8 s sustained window, 3 samples (variance-bearing)
   - `standard`: 20 s sustained window, 4 samples
   - `certification`: 45 s sustained window, 6 samples
   Discovery microbenchmarks are duration-independent (fixed work).
   **CPU sustained methodology (fixed 2026-09-10, DevKit-0):** the window is
   CONTINUOUS load — a 25%-of-window (min 5 s) untimed steady-state warmup
   ramps clocks up, and untimed filler work runs *between* timed samples so
   the core never drops toward idle. The previous yield-between-samples
   design measured its own DVFS ramp (monotonic 10.3→3.4 ms) and aborted on
   instability; idle-gaps are a measurement defect, not host instability.
5a. **Deadlines** (two layers, both recorded): each benchmark gets its own
   per-bench budget (quick 30 s / standard 60 s / certification 180 s) so a
   slow domain cannot starve the rest (DevKit-0: a global-only 60 s budget
   expired before the GPU benches started, recording them all as
   `cancelled`). The global ceiling (180 s / 360 s / 900 s) is a last-resort
   safety stop; expiry records `Aborted/cancelled`, never a score.
5b. **GPU environment isolation:** every GPU benchmark runs on its own
   D3D12 device (fresh init/shutdown). A fault in one bench must neither
   poison its successors nor mask which bench failed. Benches whose device
   is born into a transiently wedged per-process adapter context get one
   re-init retry before an honest `Unavailable` is recorded.
6. **Storage safety:** read-only workloads against a temporary 1 GiB benchmark
   file, deleted on all exit paths (RAII); sequential re-reads only after the
   first pass, so SSD write amplification is bounded to file creation.
7. **Memory safety:** the pressure ladder aborts at 50% of the commit limit or
   on the first failed allocation; failure is data (`pressure_safe_bytes`),
   not an error.
8. **Aborts** (user Ctrl+C, timeout, unstable samples) are recorded with
   `state=Aborted` + reason; they never produce scores, never fail capability.
9. **Determinism:** `ExtractScores` and `DeriveProfiles` are pure functions of
   their inputs. Identical benchmark version + raw samples + profile registry
   produce byte-identical derivation output.
10. **Normalization:** scores are raw domain units (ops/ms, MB/s, rays/ms).
    Cross-host comparison happens only through DCP profile thresholds, which
    are versioned data — never through implicit "bigger is better" scaling.
11. **List-state invariant (D3D12):** after init and after every flush the
    command list is recording-fresh; recording sites never Reset. Timestamp
    intervals are two `EndQuery` stamps (BeginQuery is invalid for TIMESTAMP
    and poisons the list). Resolve destinations are 256-byte-aligned pages
    (one per sample). READBACK-heap resources are created in COPY_DEST;
    textures with ALLOW_RENDER_TARGET are created in RENDER_TARGET.
12. **Compute shader provenance:** the compute workload is SM6.0 DXIL
    compiled by DXC (Windows SDK 10.0.26100.0) from `compute_bench_cs.hlsl`
    at authoring time and embedded in `compute_cs_dxil.h`; the .cso beside it
    is the provenance artifact. Rationale (DevKit-0, driver 32.0.16.1078 on
    RTX 5070 Ti Laptop): the runtime-D3DCompile DXBC variant of the same
    workload wedged the driver's async PSO compilation (device removal with
    `DXGI_ERROR_INVALID_CALL` surfacing between PSO creation and first
    dispatch, contained to that device). Binding uses inline root
    descriptors (raw GPUVA) — typed buffers cannot bind through inline root
    descriptors, so the workload reads/writes via ByteAddressBuffer.
    Revisit on driver update.

## DCP profile calibration basis (2026-09-07)

Profile thresholds were first authored before any measurement existed and
were recalibrated against measured DevKit-0 quick-mode ranges (this table is
the calibration record; thresholds are versioned data and change only via
this document + a registry version bump):

| domain | DevKit-0 measured (quick) | DCP-2026-BASE threshold | basis |
|---|---|---|---|
| cpu.game_thread | 45–83 Mops/s | 20 Mops/s | ~25% of measured floor |
| cpu.sustained | 28–60 Mops/s | 12 Mops/s | ~25% of measured floor |
| gpu.raster | 550–707 Mpx/ms | 150 Mpx/ms | ~25% |
| gpu.compute | 3.24–3.35 GMACs/ms | 800 MMACs/ms | ~25% |
| gpu.sustained | 490–673 Mpx/ms | 120 Mpx/ms | ~25% |
| memory.vram | 11.7 GiB | 4 GiB | entry console class |
| memory.ram | ~23.5 GiB safe | 16 GiB | entry console class |
| storage.seq | 240–1700 MB/s (load-dependent) | 400 MB/s | SATA-class floor; quiet-system requirement |

Tier scaling: 4K60 ≈ 50% of DevKit-0 capability, 4K120 ≈ 80%, RT tiers per
ray-throughput domain (unmeasured until the DXR benchmark lands — RT profiles
cannot be claimed before then, honestly). HIGHIO storage.seq=1200 MB/s is a
quiet-system NVMe-class bar; a certification run on a loaded system
legitimately fails it.
