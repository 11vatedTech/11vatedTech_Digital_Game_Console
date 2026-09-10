// bench_gpu.hpp — GPU qualification entry points (DK0-M1B).
#pragma once

#include "bench_common.hpp"

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12CommandAllocator;
struct ID3D12GraphicsCommandList;
struct ID3D12QueryHeap;
struct ID3D12Resource;
struct ID3D12Fence;
struct ID3D12InfoQueue;

namespace dcwin {

// One D3D12 environment shared by all GPU benchmarks.
struct GpuBenchEnv {
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12QueryHeap* ts_heap = nullptr;
    ID3D12Resource* ts_readback = nullptr;
    ID3D12Fence* fence = nullptr;
    ID3D12InfoQueue* info = nullptr; // debug-layer message capture (diagnostics)
    HANDLE fence_event = nullptr;
    UINT64 fence_value = 0;
    UINT64 ts_frequency = 1; // GPU timestamp frequency (ticks/s)
    std::string adapter_id;  // "LUID:high:low"
    std::string driver_version; // filled by the runner from discovery truth
    std::string error;
    bool ok = false;
};

// Creates device (on the max-VRAM hardware adapter) + queue + allocators.
bool InitGpuBenchEnv(GpuBenchEnv& env);
void ShutdownGpuBenchEnv(GpuBenchEnv& env);
// True when the env's device reports no pending removal (health probe used
// by the runner: a freshly created device can be born into a transiently
// wedged per-process adapter context — recoverable by recreating the device).
bool GpuEnvHealthy(const GpuBenchEnv& env);

// Each benchmark: fixed work per iteration, >=3 timed samples (GPU timestamps).
void BenchGpuRaster(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec);
void BenchGpuCompute(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec);
void BenchGpuCopy(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec);
// DXR 1.1 inline ray-query traversal domain (gpu.rt_inline).
void BenchGpuRaytracing(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec,
                        double raytracing_tier);
// Full DXR pipeline domain (gpu.rt_pipeline): state object + DispatchRays.
void BenchGpuRaytracingPipeline(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec,
                                double raytracing_tier);
// Sustained: repeated raster window samples (thermal ramp sensitivity).
void BenchGpuSustained(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec,
                       const SustainedConfig& cfg);

} // namespace dcwin
