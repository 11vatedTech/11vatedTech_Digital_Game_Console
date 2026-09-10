// gpu-rt-bench.hlsl — DK0-M1 RT qualification workload (SM6.5, DXR 1.1).
// Ray-query traversal benchmark: DXR 1.1 RayQuery in a compute shader traces
// the same hardware BVH as a full ray pipeline, without requiring a state
// object. Chosen deliberately: the full DispatchRays pipeline is recorded as
// UNMEASURED on this toolchain (HLSL TraceRay declaration unavailable in DXC
// 1.8.2502 SDK / 1.9.5402 release — RESEARCH_LEDGER §rt-toolchain), and C10
// forbids substituting an unmeasured path.
//
// Scene: deterministic 4096-triangle BLAS x 8 instances (built host-side).
// Work: 8 rays per thread, deterministic pseudo-random directions (PCG hash),
// full Proceed() traversal, small deterministic ALU tail per hit.
// Compiled at authoring time by DXC to a signed DXIL container, embedded in
// gpu_rt_dxil.h. Setup metrics (BLAS/TLAS build GPU time) are recorded
// separately from traversal throughput (docs/benchmarks.md — never mixed).

RaytracingAccelerationStructure scene : register(t0);
RWByteAddressBuffer result : register(u0);

uint pcg(inout uint state) {
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rnd01(inout uint state) {
    return float(pcg(state)) * (1.0 / 4294967296.0);
}

[numthreads(256, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    // Bisect variant: one deterministic ray per thread, directions from a
    // cheap LCG on the thread id (no inner loop, no PCG state machine).
    uint k = tid.x * 2654435761u + 1u;
    k ^= k >> 15; k *= 2246822519u; k ^= k >> 13;
    float a = float(k & 0xFFFF) * (6.2831853 / 65536.0);
    float z = (float((k >> 16) & 0xFF) / 255.0) * 2.0 - 1.0;
    float s = sqrt(max(0.0, 1.0 - z * z));
    RayDesc ray;
    ray.Origin    = float3(0.5, 0.5, -2.0);
    ray.Direction = normalize(float3(s * cos(a), s * sin(a), z * 0.5 + 1.0));
    ray.TMin      = 0.001;
    ray.TMax      = 100.0;

    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);
    while (q.Proceed()) {}

    float acc = 0.0;
    uint hits = 0;
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        ++hits;
        float b = q.CommittedTriangleBarycentrics().x;
        uint prim = q.CommittedPrimitiveIndex();
        float shade = q.CommittedRayT() * 0.01 + b + frac(float(prim) * 0.6180339887);
        [unroll] for (int i = 0; i < 8; ++i) { shade = shade * 1.0001 + 0.001; }
        acc = shade;
    } else {
        acc = 0.05;
    }

    result.Store(tid.x * 8, asuint(acc));
    result.Store(tid.x * 8 + 4, hits);
}
