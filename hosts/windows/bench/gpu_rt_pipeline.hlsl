struct Payload { float4 color; uint hit; };
RaytracingAccelerationStructure scene : register(t0);
RWByteAddressBuffer outBuf : register(u0);
[shader("raygeneration")]
void RayGenMain() {
    uint2 idx = DispatchRaysIndex().xy;
    uint2 dim = DispatchRaysDimensions().xy;
    float2 uv = (float2(idx) + 0.5) / float2(dim);
    RayDesc ray;
    ray.Origin = float3(0.5, 0.5, -2.0);
    ray.Direction = normalize(float3(uv.x - 0.5, 0.5 - uv.y, 1.0));
    ray.TMin = 0.001;
    ray.TMax = 100.0;
    Payload p;
    TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p);
    uint base = (idx.y * dim.x + idx.x) * 16;
    outBuf.Store(base, asuint(p.color.r));
    outBuf.Store(base + 12, p.hit);
}
[shader("miss")]
void MissMain(inout Payload p) { p.color = float4(0.05, 0.07, 0.12, 1); p.hit = 0; }
[shader("closesthit")]
void ClosestHitMain(inout Payload p, in BuiltInTriangleIntersectionAttributes attr) {
    p.color = float4(1, 1, 1, 1); p.hit = 1;
}
