// gpu-compute-bench.hlsl — DK0-M1B compute qualification workload (SM6.0).
// Raw buffer access: inline root descriptors cannot bind typed buffers
// (DevKit-0: CreateComputePipelineState E_INVALIDARG), so the workload uses
// ByteAddressBuffer/RWByteAddressBuffer with 4-byte-stride addressing.
// 16 unrolled MADs per element; deterministic.
// Compiled at authoring time by DXC (SDK bin) to a signed DXIL container and
// embedded in compute_cs_dxil.h.
ByteAddressBuffer gIn : register(t0);
RWByteAddressBuffer gOut : register(u0);

[numthreads(128, 1, 1)]
void main(uint3 dt : SV_DispatchThreadID) {
    uint i = dt.x;
    float v = asfloat(gIn.Load(i * 4));
    float a = 0.0f;
    [unroll] for (int k = 0; k < 16; ++k) {
        a = a * 1.000001f + v * 0.000001f + float(k) * 1e-7f;
    }
    gOut.Store(i * 4, asuint(a));
}
