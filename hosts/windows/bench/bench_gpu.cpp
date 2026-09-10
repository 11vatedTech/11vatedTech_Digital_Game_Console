// bench_gpu.cpp — D3D12 GPU qualification (DK0-M1B; docs/benchmarks.md).
// GPU time via timestamp queries (never wall-clock alone). Every benchmark
// records identity, warmups, samples, variance; honest Unavailable when a
// domain's prerequisites are missing. No score is invented (C10).
//
// State correctness notes: resources live in COMMON state across flushes
// (legacy decay rule); textures transition COMMON->RENDER_TARGET per list,
// buffers rely on implicit promotion. ResolveQueryData dest transitions
// COMMON->COPY_DEST per list.
#include "bench_gpu.hpp"
#include "compute_cs_dxil.h"
#include "gpu_rt_dxil.h"
#include "gpu_rt_pipeline_dxil.h"
#include "dc/qualification.hpp"
#include "win_util.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <dxgi.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace dcwin {

namespace {

template <typename T>
static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

static std::string HrMsg(const char* what, HRESULT hr) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s failed hr=0x%08lX", what, static_cast<unsigned long>(hr));
    return buf;
}

// Drain the first debug-layer validation messages into `out` (evidence must
// name the exact invalid call, not just the failing Close).
static void DrainInfoQueue(GpuBenchEnv& env, std::string& out) {
    if (!env.info) return;
    UINT64 n = env.info->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 i = 0; i < n && i < 3; ++i) {
        SIZE_T size = 0;
        if (FAILED(env.info->GetMessageA(i, nullptr, &size)) || size == 0) continue;
        std::vector<char> buf(size);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (FAILED(env.info->GetMessageA(i, msg, &size)) || !msg->pDescription) continue;
        std::string desc(msg->pDescription);
        if (desc.size() > 220) desc = desc.substr(0, 220) + "...";
        out += " | dl: " + desc;
    }
    env.info->ClearStoredMessages();
}

static bool MakeDefaultBuffer(ID3D12Device* dev, uint64_t bytes, ID3D12Resource** out) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_COMMON, nullptr,
                                              IID_PPV_ARGS(out));
    return SUCCEEDED(hr);
}

static bool MakeUploadBuffer(ID3D12Device* dev, uint64_t bytes, ID3D12Resource** out) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              IID_PPV_ARGS(out));
    return SUCCEEDED(hr);
}

static IDXGIAdapter1* AcquireBenchAdapter() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) return nullptr;
    IDXGIAdapter1* best = nullptr;
    UINT64 best_vram = 0;
    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* a = nullptr;
        if (FAILED(factory->EnumAdapters1(i, &a)) || !a) break;
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(a->GetDesc1(&d)) && (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            d.DedicatedVideoMemory > best_vram) {
            best_vram = d.DedicatedVideoMemory;
            SafeRelease(best);
            best = a; // ownership transferred
            continue;
        }
        a->Release();
    }
    factory->Release();
    return best;
}

// Flush+diagnostics: every failure step is reported through `why` so the
// evidence names the exact failing call (Close/Signal/fence-wait) instead of
// an opaque downstream symptom (DevKit-0 diagnostics discipline).
static bool Flush(GpuBenchEnv& env, ID3D12GraphicsCommandList* list,
                  std::string* why = nullptr) {
    HRESULT hr = list->Close();
    if (FAILED(hr)) {
        if (why) *why = HrMsg("close", hr);
        return false;
    }
    ID3D12CommandList* cl = list;
    env.queue->ExecuteCommandLists(1, &cl);
    hr = env.queue->Signal(env.fence, ++env.fence_value);
    if (FAILED(hr)) {
        if (why) *why = HrMsg("signal", hr);
        return false;
    }
    if (env.fence->GetCompletedValue() < env.fence_value) {
        env.fence->SetEventOnCompletion(env.fence_value, env.fence_event);
        DWORD w = WaitForSingleObject(env.fence_event, 10000);
        if (w != WAIT_OBJECT_0) {
            if (why) *why = (w == WAIT_TIMEOUT) ? "fence-timeout" : "fence-wait-failed";
            return false;
        }
    }
    env.alloc->Reset();
    list->Reset(env.alloc, nullptr);
    return true;
}

// Read slot_a/slot_b timestamps (UINT64 each, slot i at byte i*8).
// `why` carries the exact failure (map hr, raw a/b values) for evidence.
// Read a timestamp interval from resolve page `page` (256-byte aligned pages;
// ResolveQueryData's destination offset must be 256-byte aligned, so each
// sample resolves its 2 slots into its own page instead of one over-range
// resolve covering never-written slots).
static bool ResolveTimestamps(GpuBenchEnv& env, uint32_t page,
                              double& out_ms, std::string* why = nullptr) {
    void* mapped = nullptr;
    D3D12_RANGE read_range{0, 4096};
    HRESULT hr = env.ts_readback->Map(0, &read_range, &mapped);
    if (FAILED(hr)) {
        if (why) *why = HrMsg("ts map", hr);
        return false;
    }
    constexpr uint32_t kQwordsPerPage = 256 / sizeof(UINT64); // 32
    const UINT64* data = static_cast<const UINT64*>(mapped);
    UINT64 a = data[page * kQwordsPerPage];
    UINT64 b = data[page * kQwordsPerPage + 1];
    D3D12_RANGE written{0, 0};
    env.ts_readback->Unmap(0, &written);
    if (env.ts_frequency == 0) {
        if (why) *why = "ts frequency zero";
        return false;
    }
    if (b <= a) {
        if (why) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "ts-invalid a=%llu b=%llu",
                          static_cast<unsigned long long>(a),
                          static_cast<unsigned long long>(b));
            *why = buf;
        }
        return false;
    }
    out_ms = static_cast<double>(b - a) * 1000.0 / static_cast<double>(env.ts_frequency);
    return true;
}

// Device-removed probe used across stages; "stage=ok" or "stage failed hr=...".
static std::string RemovedProbe(GpuBenchEnv& env, const char* stage) {
    if (!env.device) return {};
    HRESULT r = env.device->GetDeviceRemovedReason();
    if (SUCCEEDED(r)) return std::string(stage) + "=ok";
    return HrMsg(stage, r);
}

// Identity helper: stamp records with the bench adapter id + driver truth.
static BenchIdentity BenchId(const GpuBenchEnv& env) {
    BenchIdentity id;
    id.adapter_id = env.adapter_id;
    id.driver_version = env.driver_version;
    return id;
}

// Timed-sample harness: per sample, Begin timestamp, record work, End,
// resolve into readback (COMMON->COPY_DEST transition), flush, read delta.
// `record_work` records exactly one workload iteration (kIterations of it).
static bool RunTimedSamples(GpuBenchEnv& env, BenchCancel& cancel,
                            const char* benchmark_id, uint32_t iterations,
                            const char* methodology, uint32_t warmups,
                            const std::function<void()>& record_work,
                            BenchmarkRecord& rec) {
    StampRecord(rec, BenchId(env), benchmark_id, iterations, warmups, methodology);

    std::vector<BenchSample> samples;
    std::string stage_probes = " " + RemovedProbe(env, "rts-entry");
    for (int s = 0; s < 5; ++s) {
        if (cancel.requested || cancel.Expired(QpcMs())) {
            rec.state = BenchState::Aborted; rec.abort_reason = "cancelled";
            return false;
        }
        // list is open (post-Flush reset) or fresh: reset defensively.
        // Timestamp queries support EndQuery only (BeginQuery is invalid for
        // TIMESTAMP and poisons the list — DevKit-0 root cause). An interval
        // is two EndQuery stamps around the measured region.
        env.list->EndQuery(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, s * 2);
        for (uint32_t it = 0; it < iterations; ++it) record_work();
        env.list->EndQuery(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, s * 2 + 1);
        // Readback lives in COPY_DEST: created in COPY_DEST (READBACK rule),
        // decays to COMMON at ECL, promoted back for ResolveQueryData.
        env.list->ResolveQueryData(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, s * 2, 2,
                                   env.ts_readback, static_cast<UINT64>(s) * 256);
        std::string why;
        if (!Flush(env, env.list, &why)) {
            DrainInfoQueue(env, why);
            env.ok = false; env.error = why;
            rec.state = BenchState::Aborted; rec.abort_reason = why;
            if (env.device) {
                HRESULT removed = env.device->GetDeviceRemovedReason();
                if (FAILED(removed)) rec.abort_reason += " " + HrMsg("removed", removed);
            }
            return false;
        }
        stage_probes += " " + RemovedProbe(env, "post-sample");
        double ms = 0.0;
        if (!ResolveTimestamps(env, s, ms, &why) || ms <= 0.0) {
            rec.state = BenchState::Aborted;
            rec.abort_reason = (why.empty() ? "timestamp-unavailable" : why) + stage_probes;
            if (env.device) {
                HRESULT removed = env.device->GetDeviceRemovedReason();
                if (FAILED(removed)) rec.abort_reason += " " + HrMsg("removed", removed);
            }
            return false;
        }
        samples.push_back({ms, ms});
    }
    FinalizeRecord(rec, samples, static_cast<double>(iterations));
    return rec.state == BenchState::Complete;
}


} // namespace

// ---------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------

bool InitGpuBenchEnv(GpuBenchEnv& env) {
    HMODULE d3d12mod = LoadLibraryW(L"d3d12.dll");
    if (!d3d12mod) { env.error = "d3d12.dll missing"; return false; }
    // Enable the D3D12 debug layer when Graphics Tools are installed: silent
    // invalid usage becomes visible HRESULT failures (DevKit-0 diagnostics).
    // Absent tools => skipped gracefully; device creation below still works.
    using PFN_D3D12GetDebugInterface = HRESULT(WINAPI*)(REFIID, void**);
    auto getdbg = reinterpret_cast<PFN_D3D12GetDebugInterface>(
        reinterpret_cast<void*>(GetProcAddress(d3d12mod, "D3D12GetDebugInterface")));
    if (getdbg) {
        ID3D12Debug* dbg = nullptr;
        if (SUCCEEDED(getdbg(IID_PPV_ARGS(&dbg))) && dbg) {
            dbg->EnableDebugLayer();
            dbg->Release();
        }
    }
    using PFN_D3D12CreateDevice = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto create = reinterpret_cast<PFN_D3D12CreateDevice>(
        reinterpret_cast<void*>(GetProcAddress(d3d12mod, "D3D12CreateDevice")));
    if (!create) { env.error = "D3D12CreateDevice export missing"; return false; }
    // d3d12mod intentionally never freed (process-lifetime module).

    IDXGIAdapter1* adapter = AcquireBenchAdapter();
    if (!adapter) { env.error = "no hardware adapter"; return false; }
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "LUID:%08lX:%08lX",
                      static_cast<unsigned long>(desc.AdapterLuid.HighPart),
                      static_cast<unsigned long>(desc.AdapterLuid.LowPart));
        env.adapter_id = buf;
    }

    HRESULT hr = create(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&env.device));
    adapter->Release();
    if (FAILED(hr) || !env.device) { env.error = HrMsg("device creation", hr); return false; }
    // Info queue exists when the debug layer is active; capture validation
    // messages so Close()/ECL failures carry the exact invalid API call.
    env.device->QueryInterface(IID_PPV_ARGS(&env.info));

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = env.device->CreateCommandQueue(&qd, IID_PPV_ARGS(&env.queue));
    if (FAILED(hr)) { env.error = HrMsg("queue", hr); return false; }

    hr = env.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&env.alloc));
    if (FAILED(hr)) { env.error = HrMsg("allocator", hr); return false; }

    hr = env.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, env.alloc, nullptr,
                                       IID_PPV_ARGS(&env.list));
    if (FAILED(hr)) { env.error = HrMsg("command list", hr); return false; }
    // Invariant: after Init and after every Flush the list is recording-fresh
    // (Close'd then Reset here, and Flush ends with list->Reset). Recording
    // sites therefore never Reset — recording into the post-init closed list
    // silently no-ops and the later Close fails E_FAIL (DevKit-0).
    env.list->Close();
    env.list->Reset(env.alloc, nullptr);

    D3D12_QUERY_HEAP_DESC qhd{};
    qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count = 16;
    hr = env.device->CreateQueryHeap(&qhd, IID_PPV_ARGS(&env.ts_heap));
    if (FAILED(hr)) { env.error = HrMsg("query heap", hr); return false; }

    // Timestamp readback: READBACK-heap resources MUST be created in
    // COPY_DEST (D3D12 initial-state rule; COMMON here is invalid usage —
    // resolves were dropped and the driver removed the device with
    // DXGI_ERROR_INVALID_CALL on DevKit-0). Per-list use relies on buffer
    // decay-to-COMMON at ECL + implicit promotion back to COPY_DEST.
    {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = 4096; // 16 aligned 256B resolve pages
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = env.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&env.ts_readback));
        if (FAILED(hr)) { env.error = HrMsg("ts readback", hr); return false; }
        // ts_readback size: 16 pages x 256B = 4096 (one aligned page per
        // timed sample).
    }

    hr = env.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&env.fence));
    if (FAILED(hr)) { env.error = HrMsg("fence", hr); return false; }
    env.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!env.fence_event) { env.error = "fence event failed"; return false; }

    env.queue->GetTimestampFrequency(&env.ts_frequency);
    if (env.ts_frequency == 0) { env.error = "timestamp frequency zero"; return false; }

    env.ok = true;
    return true;
}

void ShutdownGpuBenchEnv(GpuBenchEnv& env) {
    if (env.fence_event) { CloseHandle(env.fence_event); env.fence_event = nullptr; }
    SafeRelease(env.info);
    SafeRelease(env.fence);
    SafeRelease(env.ts_readback);
    SafeRelease(env.ts_heap);
    SafeRelease(env.list);
    SafeRelease(env.alloc);
    SafeRelease(env.queue);
    SafeRelease(env.device);
    env.ok = false;
}

bool GpuEnvHealthy(const GpuBenchEnv& env) {
    if (!env.ok || !env.device) return false;
    return SUCCEEDED(env.device->GetDeviceRemovedReason());
}

// ---------------------------------------------------------------------------
// Raster (also the sustained workload shape)
// ---------------------------------------------------------------------------

namespace {

struct RasterObjects {
    ID3D12Resource* rt = nullptr;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    ID3D12Resource* vb = nullptr;
    ID3D12Resource* vb_upload = nullptr;
    ID3D12RootSignature* rs = nullptr;
    ID3D12PipelineState* pso = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    ~RasterObjects() {
        SafeRelease(pso); SafeRelease(rs);
        SafeRelease(rtv_heap); SafeRelease(rt);
        SafeRelease(vb_upload); SafeRelease(vb);
    }
};

bool BuildRasterObjects(GpuBenchEnv& env, RasterObjects& ro, std::string& error) {
    static const char* vs_src = R"HLSL(
struct VIn { float4 pos : POSITION; };
struct VOut { float4 sv : SV_POSITION; };
VOut main(VIn v) { VOut o; o.sv = v.pos; return o; }
)HLSL";
    static const char* ps_src = R"HLSL(
float4 main(float4 sv : SV_POSITION) : SV_Target {
    float a = sv.x * 0.5 + sv.y * 0.25 + sv.z * 0.125;
    [unroll] for (int i = 0; i < 16; ++i) { a = a * 1.0001 + 0.001; }
    return float4(a, a, a, 1);
}
)HLSL";

    ID3DBlob* vs_blob = nullptr; ID3DBlob* ps_blob = nullptr; ID3DBlob* err = nullptr;
    if (FAILED(D3DCompile(vs_src, std::strlen(vs_src), nullptr, nullptr, nullptr,
                          "main", "vs_5_0", 0, 0, &vs_blob, &err)) ||
        FAILED(D3DCompile(ps_src, std::strlen(ps_src), nullptr, nullptr, nullptr,
                          "main", "ps_5_0", 0, 0, &ps_blob, &err))) {
        if (err) err->Release();
        SafeRelease(vs_blob); SafeRelease(ps_blob);
        error = "shader-compile-failed";
        return false;
    }
    SafeRelease(err);

    // Render target in COMMON; transitions per command list (decay rule).
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = 1280; rd.Height = 720;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    // Initial-state rule: textures with ALLOW_RENDER_TARGET must be created
    // in RENDER_TARGET, not COMMON (DevKit-0 device-removal defect). Writable
    // states decay to COMMON at the ECL boundary, so a one-time transition
    // puts the texture into the steady COMMON state the per-list barriers
    // in RecordRasterWork expect.
    HRESULT hr = env.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                     D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                     IID_PPV_ARGS(&ro.rt));
    if (FAILED(hr)) { SafeRelease(vs_blob); SafeRelease(ps_blob); error = HrMsg("rt", hr); return false; }

    D3D12_DESCRIPTOR_HEAP_DESC rthd{};
    rthd.NumDescriptors = 1;
    rthd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = env.device->CreateDescriptorHeap(&rthd, IID_PPV_ARGS(&ro.rtv_heap));
    if (FAILED(hr)) { SafeRelease(vs_blob); SafeRelease(ps_blob); error = HrMsg("rtv heap", hr); return false; }
    ro.rtv = ro.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    env.device->CreateRenderTargetView(ro.rt, nullptr, ro.rtv);

    // One-time RENDER_TARGET->COMMON transition + flush: establishes the
    // steady COMMON state (subsequent lists decay back to COMMON at every
    // ECL boundary, so the per-list COMMON->RENDER_TARGET barrier is valid).
    {
        D3D12_RESOURCE_BARRIER decay{};
        decay.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        decay.Transition.pResource = ro.rt;
        decay.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        decay.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        decay.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        env.list->ResourceBarrier(1, &decay);
        Flush(env, env.list);
    }

    // Vertex data: 4096 random triangles (deterministic).
    struct V { float x, y, z, w; };
    const uint32_t kVerts = 4096u * 3u;
    std::vector<V> verts(kVerts);
    BenchRng rng(0xCAFE0001ULL);
    for (size_t i = 0; i < verts.size(); ++i) {
        const float fx = static_cast<float>(rng.NextUnit()) * 2.0f - 1.0f;
        const float fy = static_cast<float>(rng.NextUnit()) * 2.0f - 1.0f;
        verts[i] = { fx, fy, 0.5f, 1.0f };
    }
    const uint64_t vb_bytes = verts.size() * sizeof(V);
    if (!MakeUploadBuffer(env.device, vb_bytes, &ro.vb_upload) ||
        !MakeDefaultBuffer(env.device, vb_bytes, &ro.vb)) {
        SafeRelease(vs_blob); SafeRelease(ps_blob);
        error = "vb alloc failed";
        return false;
    }
    {
        void* mapped = nullptr;
        D3D12_RANGE rr{0, 0};
        if (SUCCEEDED(ro.vb_upload->Map(0, &rr, &mapped))) {
            memcpy(mapped, verts.data(), static_cast<size_t>(vb_bytes));
            D3D12_RANGE wr{0, static_cast<SIZE_T>(vb_bytes)};
            ro.vb_upload->Unmap(0, &wr);
        }
    }
    ro.vbv.BufferLocation = ro.vb->GetGPUVirtualAddress();
    ro.vbv.StrideInBytes = sizeof(V);
    ro.vbv.SizeInBytes = static_cast<UINT>(vb_bytes);

    static const D3D12_INPUT_ELEMENT_DESC kElems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* rs_blob = nullptr;
    hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1_0, &rs_blob, &err);
    if (SUCCEEDED(hr)) {
        hr = env.device->CreateRootSignature(0, rs_blob->GetBufferPointer(),
                                             rs_blob->GetBufferSize(), IID_PPV_ARGS(&ro.rs));
    }
    SafeRelease(rs_blob);
    SafeRelease(err);
    if (FAILED(hr)) { SafeRelease(vs_blob); SafeRelease(ps_blob); error = HrMsg("root sig", hr); return false; }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = ro.rs;
    pso.InputLayout.pInputElementDescs = kElems;
    pso.InputLayout.NumElements = 1;
    pso.VS = { vs_blob->GetBufferPointer(), vs_blob->GetBufferSize() };
    pso.PS = { ps_blob->GetBufferPointer(), ps_blob->GetBufferSize() };
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.SampleMask = 0xFFFFFFFF;
    pso.SampleDesc.Count = 1;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    hr = env.device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&ro.pso));
    SafeRelease(vs_blob); SafeRelease(ps_blob);
    if (FAILED(hr) || !ro.pso) { error = HrMsg("pso", hr); return false; }
    return true;
}

// One raster workload: explicit COMMON->RENDER_TARGET transition, 4096
// triangles at 1280x720, explicit RT->COMMON back-transition. The texture was
// created in COMMON and *repeatedly* bound as RTV across separate command
// lists — implicit decay does not re-promote for RTV writes, so both
// transitions are required (device-removal bug found on DevKit-0).
void RecordRasterWork(GpuBenchEnv& env, const RasterObjects& ro) {
    D3D12_RESOURCE_BARRIER to_rt{};
    to_rt.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_rt.Transition.pResource = ro.rt;
    to_rt.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    to_rt.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    to_rt.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    env.list->ResourceBarrier(1, &to_rt);

    const D3D12_VIEWPORT vp{ 0, 0, 1280.0f, 720.0f, 0.0f, 1.0f };
    const D3D12_RECT sc{ 0, 0, 1280, 720 };
    env.list->SetPipelineState(ro.pso);
    env.list->SetGraphicsRootSignature(ro.rs);
    env.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    env.list->IASetVertexBuffers(0, 1, &ro.vbv);
    env.list->RSSetViewports(1, &vp);
    env.list->RSSetScissorRects(1, &sc);
    env.list->OMSetRenderTargets(1, &ro.rtv, FALSE, nullptr);
    env.list->DrawInstanced(4096u * 3u, 1, 0, 0);

    D3D12_RESOURCE_BARRIER to_common{};
    to_common.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_common.Transition.pResource = ro.rt;
    to_common.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    to_common.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    to_common.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    env.list->ResourceBarrier(1, &to_common);
}

constexpr double kRasterPixels = 1280.0 * 720.0;

// Shared raster/sustained driver. spread_window=false: 5 back-to-back
// samples. true: cfg.samples spread across cfg.window_ms (yield-wait between
// samples so boost clocks decay — thermal ramp sensitivity).
void RasterBench(GpuBenchEnv& env, BenchCancel& cancel, const char* benchmark_id,
                 bool spread_window, const SustainedConfig& cfg, BenchmarkRecord& rec) {
    if (!env.ok) { rec.state = BenchState::Unavailable; rec.abort_reason = env.error; return; }

    RasterObjects ro;
    std::string error;
    if (!BuildRasterObjects(env, ro, error)) {
        rec.state = BenchState::Unavailable; rec.abort_reason = error;
        return;
    }

    auto one_workload = [&]() { RecordRasterWork(env, ro); };
    std::string warm_probes = RemovedProbe(env, "rb-entry");

    if (!spread_window) {
        for (int w = 0; w < 6; ++w) {
            one_workload();
            std::string why;
            if (!Flush(env, env.list, &why)) {
                DrainInfoQueue(env, why);
                env.ok = false; env.error = why; // poisoned list: stop cleanly
                rec.state = BenchState::Aborted; rec.abort_reason = "warmup: " + why;
                return;
            }
            warm_probes += " " + RemovedProbe(env, "post-warm");
        }
        // 40 workloads per timed sample: a single ~sub-ms draw is below the
        // timer/pacing noise floor on a boost-heavy laptop GPU (DevKit-0:
        // 1-workload samples tripped the instability gate).
        RunTimedSamples(env, cancel, benchmark_id, 40, "dc-bench/1:gpu-raster", 6,
                        one_workload, rec);
        if (rec.state != BenchState::Complete) {
            rec.abort_reason += " rprobes:" + warm_probes;
        }
    } else {
        StampRecord(rec, BenchId(env), benchmark_id, 1, 0, "dc-bench/1:gpu-raster");
        std::vector<BenchSample> samples;
        double window_start = QpcMs();
        double per = cfg.window_ms / static_cast<double>(cfg.samples);
        for (uint32_t s = 0; s < cfg.samples; ++s) {
            double target = window_start + per * (s + 1);
            while (QpcMs() < target) {
                if (cancel.requested) break;
                std::this_thread::yield();
            }
            if (cancel.requested || cancel.Expired(QpcMs())) {
                rec.state = BenchState::Aborted; rec.abort_reason = "cancelled";
                return;
            }
            env.list->EndQuery(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
            for (uint32_t it = 0; it < 40; ++it) one_workload();
            env.list->EndQuery(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
            // Readback promoted COMMON->COPY_DEST implicitly (buffer rule).
            env.list->ResolveQueryData(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                       env.ts_readback, 0); // page 0 (256B aligned)
            std::string why;
            if (!Flush(env, env.list, &why)) {
                DrainInfoQueue(env, why);
                env.ok = false; env.error = why;
                rec.state = BenchState::Aborted; rec.abort_reason = why;
                return;
            }
            double ms = 0.0;
            if (!ResolveTimestamps(env, 0, ms, &why) || ms <= 0.0) {
                rec.state = BenchState::Aborted;
                rec.abort_reason = why.empty() ? "timestamp-unavailable" : why;
                return;
            }
            samples.push_back({ms, ms});
        }
        FinalizeRecord(rec, samples, 40.0); // 40 workloads per sample
    }

    if (rec.state == BenchState::Complete) {
        // FinalizeRecord emits workloads/s for 1-iteration records; convert
        // to pixels/ms (docs/benchmarks.md table).
        rec.score = (rec.score / 1000.0) * kRasterPixels;
    }
}

} // namespace

void BenchGpuRaster(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec) {
    RasterBench(env, cancel, "gpu.raster", false, SustainedConfig{}, rec);
}

void BenchGpuSustained(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec,
                       const SustainedConfig& cfg) {
    RasterBench(env, cancel, "gpu.raster.sustained", true, cfg, rec);
}

// ---------------------------------------------------------------------------
// Compute
// ---------------------------------------------------------------------------

void BenchGpuCompute(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec) {
    StampRecord(rec, BenchId(env), "gpu.compute", 1, 2, "dc-bench/1:gpu-compute");
    if (!env.ok) { rec.state = BenchState::Unavailable; rec.abort_reason = env.error; return; }
    // SM6.0 DXIL compiled by DXC at authoring time (compute_cs_dxil.h; source
    // of truth: compute_bench_cs.hlsl). The runtime-D3DCompile DXBC variant
    // wedged the driver's async PSO compilation on this Blackwell preview
    // driver (DevKit-0 evidence); the precompiled DXIL path is the workaround.

    // Inline root descriptors (t0/u0, space 0) — no descriptor heap needed.
    D3D12_ROOT_PARAMETER rp[2]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rp[0].Descriptor.ShaderRegister = 0;
    rp[0].Descriptor.RegisterSpace = 0;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rp[1].Descriptor.ShaderRegister = 0;
    rp[1].Descriptor.RegisterSpace = 0;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 2;
    rsd.pParameters = rp;
    ID3DBlob* rs_blob = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1_0, &rs_blob, &err);
    if (FAILED(hr)) {
        if (err) err->Release();
        rec.state = BenchState::Unavailable; rec.abort_reason = "rs serialize failed";
        return;
    }
    ID3D12RootSignature* rs = nullptr;
    hr = env.device->CreateRootSignature(0, rs_blob->GetBufferPointer(),
                                         rs_blob->GetBufferSize(), IID_PPV_ARGS(&rs));
    SafeRelease(rs_blob);
    SafeRelease(err);
    if (FAILED(hr)) {
        rec.state = BenchState::Unavailable; rec.abort_reason = "rs create failed";
        return;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC cpso{};
    cpso.pRootSignature = rs;
    cpso.CS = { dxil::kComputeCsDxil, dxil::kComputeCsSize };
    ID3D12PipelineState* pso = nullptr;
    hr = env.device->CreateComputePipelineState(&cpso, IID_PPV_ARGS(&pso));
    if (FAILED(hr) || !pso) {
        SafeRelease(rs);
        rec.state = BenchState::Unavailable; rec.abort_reason = HrMsg("compute pso", hr);
        return;
    }

    const uint32_t kElems = 4u * 1024 * 1024; // 4M floats = 16 MB per buffer
    ID3D12Resource *in = nullptr, *out = nullptr, *in_upload = nullptr;
    if (!MakeDefaultBuffer(env.device, kElems * 4ull, &in) ||
        !MakeDefaultBuffer(env.device, kElems * 4ull, &out) ||
        !MakeUploadBuffer(env.device, kElems * 4ull, &in_upload)) {
        SafeRelease(rs); SafeRelease(pso);
        SafeRelease(in); SafeRelease(out); SafeRelease(in_upload);
        rec.state = BenchState::Unavailable; rec.abort_reason = "buffer alloc failed";
        return;
    }
    {
        void* mapped = nullptr;
        D3D12_RANGE rr{0, 0};
        if (SUCCEEDED(in_upload->Map(0, &rr, &mapped))) {
            float* f = static_cast<float*>(mapped);
            BenchRng rng(0xC0FFEEULL);
            for (uint32_t i = 0; i < kElems; ++i) f[i] = static_cast<float>(rng.NextUnit());
            D3D12_RANGE wr{0, kElems * 4};
            in_upload->Unmap(0, &wr);
        }
    }

    // Binding: inline root descriptors (raw GPUVA) — no shader-visible heap,
    // no descriptor tables. Exercises a different driver path than the
    // table-based binding whose PSO-compile wedge DevKit-0 exposed.

    // Per-stage removal probes (DevKit-0 diagnostics): evidence must show
    // whether the device is already removed at entry, after the upload,
    // after each warmup, or only at the timed sample.
    auto probe = [&](const char* stage) {
        if (!env.device) return std::string();
        HRESULT r = env.device->GetDeviceRemovedReason();
        if (SUCCEEDED(r)) return std::string(stage) + "=ok";
        return HrMsg(stage, r);
    };
    std::string probes = probe("entry");

    // Upload in (buffers promote from COMMON; no transitions needed).
    env.list->CopyBufferRegion(in, 0, in_upload, 0, kElems * 4ull);
    {
        std::string why;
        if (!Flush(env, env.list, &why)) {
            DrainInfoQueue(env, why);
            env.ok = false; env.error = why;            rec.state = BenchState::Aborted; rec.abort_reason = "upload: " + why;
            SafeRelease(rs); SafeRelease(pso);
            SafeRelease(in); SafeRelease(out); SafeRelease(in_upload);

            return;
        }
    }

    probes += " " + probe("post-upload");

    // Single dispatch per record_work (DevKit-0 bisect).
    const uint32_t kIters = 1;
    auto record_work = [&]() {
        env.list->SetPipelineState(pso);
        env.list->SetComputeRootSignature(rs);
        for (uint32_t i = 0; i < kIters; ++i) {
            env.list->SetComputeRootShaderResourceView(0, in->GetGPUVirtualAddress());
            env.list->SetComputeRootUnorderedAccessView(1, out->GetGPUVirtualAddress());
            // X thread-group limit is 65535: 4M/128 = 32768 groups (4M/64
            // = 65536 was an invalid dispatch — DevKit-0 device removal).
            env.list->Dispatch(kElems / 128, 1, 1);
            D3D12_RESOURCE_BARRIER bar{};
            bar.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            env.list->ResourceBarrier(1, &bar);
        }
    };
    for (int w = 0; w < 2; ++w) {
        record_work();
        std::string why;
        if (!Flush(env, env.list, &why)) {
            DrainInfoQueue(env, why);
            env.ok = false; env.error = why;
            rec.state = BenchState::Aborted; rec.abort_reason = "warmup: " + why;
            if (env.device) {
                HRESULT removed = env.device->GetDeviceRemovedReason();
                if (FAILED(removed)) rec.abort_reason += " " + HrMsg("removed", removed);
            }
            SafeRelease(rs); SafeRelease(pso);
            SafeRelease(in); SafeRelease(out); SafeRelease(in_upload);
            return;
        }
        probes += " " + probe("post-warmup");
    }
    RunTimedSamples(env, cancel, "gpu.compute", 1, "dc-bench/1:gpu-compute", 2, record_work, rec);
    if (rec.state != BenchState::Complete) {
        rec.abort_reason += " probes: " + probes;
    }

    if (rec.state == BenchState::Complete) {
        // MACs/ms: workloads/s / 1000 * MACs-per-workload (16 MADs/element).
        const double macs = static_cast<double>(kElems) * 16.0 * static_cast<double>(kIters);
        rec.score = (rec.score / 1000.0) * macs;
    }

    SafeRelease(rs); SafeRelease(pso);
    SafeRelease(in); SafeRelease(out); SafeRelease(in_upload);
}

// ---------------------------------------------------------------------------
// Copy (device-internal bandwidth)
// ---------------------------------------------------------------------------

void BenchGpuCopy(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec) {
    StampRecord(rec, BenchId(env), "gpu.copy", 1, 2, "dc-bench/1:gpu-copy");
    if (!env.ok) { rec.state = BenchState::Unavailable; rec.abort_reason = env.error; return; }
    const uint64_t kBytes = 256ull << 20;
    ID3D12Resource *a = nullptr, *b = nullptr;
    if (!MakeDefaultBuffer(env.device, kBytes, &a) || !MakeDefaultBuffer(env.device, kBytes, &b)) {
        SafeRelease(a); SafeRelease(b);
        rec.state = BenchState::Unavailable; rec.abort_reason = "copy buffers failed";
        return;
    }
    const uint32_t kIters = 4; // 4 * 256 MiB = 1 GiB per sample
    auto record_work = [&]() {
        for (uint32_t i = 0; i < kIters; ++i) {
            env.list->CopyBufferRegion(b, 0, a, 0, kBytes);
        }
    };
    for (int w = 0; w < 2; ++w) {
        record_work();
        std::string why;
        if (!Flush(env, env.list, &why)) {
            DrainInfoQueue(env, why);
            env.ok = false; env.error = why;
            rec.state = BenchState::Aborted; rec.abort_reason = "warmup: " + why;
            SafeRelease(a); SafeRelease(b);
            return;
        }
    }
    RunTimedSamples(env, cancel, "gpu.copy", 4, "dc-bench/1:gpu-copy", 2, record_work, rec);
    if (rec.state == BenchState::Complete) {
        // FinalizeRecord emits workloads/s; per-docs unit is MB/ms, which is
        // numerically identical to GB/s. workloads/s / 1000 * MB-per-workload.
        const double mb_total = static_cast<double>(kIters * kBytes) / (1024.0 * 1024.0);
        rec.score = rec.score / 1000.0 * mb_total;
    }
    SafeRelease(a); SafeRelease(b);
}

// ---------------------------------------------------------------------------
// Ray tracing — DXR 1.1 ray-query traversal benchmark.
// Setup (BLAS/TLAS build) GPU time is recorded separately in
// rec.elapsed_gpu_time_ms; the score is pure traversal throughput
// (GPU-timestamped dispatch, rays/ms). The full DispatchRays pipeline remains
// UNMEASURED: HLSL TraceRay's declaration is unavailable in DXC 1.8.2502 SDK
// and 1.9.5402 release builds (RESEARCH_LEDGER §rt-toolchain) — never faked
// (C10). RayQuery exercises the same hardware BVH traversal path.
// ---------------------------------------------------------------------------

void BenchGpuRaytracing(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec,
                        double raytracing_tier) {
    StampRecord(rec, BenchId(env), "gpu.rt_inline", 1, 2, "dc-bench/1:gpu-rt-inline");
    if (!env.ok) { rec.state = BenchState::Unavailable; rec.abort_reason = env.error; return; }
    if (raytracing_tier < 1.1) {
        rec.state = BenchState::Unavailable;
        rec.abort_reason = "ray-query-unavailable-dxr-tier-below-1.1";
        return;
    }

    ID3D12Device5* dev5 = nullptr;
    if (FAILED(env.device->QueryInterface(IID_PPV_ARGS(&dev5))) || !dev5) {
        rec.state = BenchState::Unavailable; rec.abort_reason = "device5-unavailable";
        return;
    }
    ID3D12GraphicsCommandList4* list4 = nullptr;
    if (FAILED(env.list->QueryInterface(IID_PPV_ARGS(&list4))) || !list4) {
        SafeRelease(dev5);
        rec.state = BenchState::Unavailable; rec.abort_reason = "commandlist4-unavailable";
        return;
    }

    // Root signature: descriptor table (t0 = TLAS AS SRV) + inline raw UAV (u0).
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER rp[2]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[0].DescriptorTable.NumDescriptorRanges = 1;
    rp[0].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rp[1].Descriptor.ShaderRegister = 0;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 2;
    rsd.pParameters = rp;
    ID3DBlob* rs_blob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1_0, &rs_blob, nullptr);
    if (FAILED(hr)) {
        SafeRelease(dev5); SafeRelease(list4);
        rec.state = BenchState::Unavailable; rec.abort_reason = "rs serialize failed";
        return;
    }
    ID3D12RootSignature* rs = nullptr;
    hr = env.device->CreateRootSignature(0, rs_blob->GetBufferPointer(),
                                         rs_blob->GetBufferSize(), IID_PPV_ARGS(&rs));
    SafeRelease(rs_blob);
    if (FAILED(hr)) {
        SafeRelease(dev5); SafeRelease(list4);
        rec.state = BenchState::Unavailable; rec.abort_reason = HrMsg("rs create", hr);
        return;
    }

    // SM6.5 signed DXIL (gpu_rt_dxil.h; source of truth gpu_rt_bench.hlsl),
    // compiled by DXC 1.9.5402 at authoring time.
    D3D12_COMPUTE_PIPELINE_STATE_DESC cpso{};
    cpso.pRootSignature = rs;
    cpso.CS = { reinterpret_cast<const BYTE*>(kGpuRtDxil), kGpuRtDxilBytes };
    ID3D12PipelineState* pso = nullptr;
    hr = env.device->CreateComputePipelineState(&cpso, IID_PPV_ARGS(&pso));
    if (FAILED(hr) || !pso) {
        SafeRelease(rs); SafeRelease(dev5); SafeRelease(list4);
        rec.state = BenchState::Unavailable; rec.abort_reason = HrMsg("rt pso", hr);
        return;
    }

    // ---- Scene: 4096 deterministic triangles, 8 TLAS instances ------------
    constexpr uint32_t kTris = 4096;
    constexpr uint32_t kInstances = 8;
    constexpr uint32_t kThreads = 512u * 256u; // dispatch size (groups*threads)
    constexpr uint32_t kRaysPerThread = 8;

    const uint64_t vb_bytes = static_cast<uint64_t>(kTris) * 3ull * 12ull;
    ID3D12Resource* vb = nullptr;
    ID3D12Resource* vb_upload = nullptr;
    ID3D12Resource* blas_scratch = nullptr;
    ID3D12Resource* blas_result = nullptr;
    ID3D12Resource* tlas_scratch = nullptr;
    ID3D12Resource* tlas_result = nullptr;
    ID3D12Resource* inst_upload = nullptr;
    ID3D12Resource* rt_result = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;

    auto bail = [&](const char* why_msg) {
        if (rec.state == BenchState::NotRun) {
            rec.state = BenchState::Unavailable; rec.abort_reason = why_msg;
        }
        SafeRelease(heap); SafeRelease(rt_result); SafeRelease(inst_upload);
        SafeRelease(tlas_result); SafeRelease(tlas_scratch); SafeRelease(blas_result);
        SafeRelease(blas_scratch); SafeRelease(vb_upload); SafeRelease(vb);
        SafeRelease(rs); SafeRelease(pso); SafeRelease(dev5); SafeRelease(list4);
    };

    if (!MakeDefaultBuffer(env.device, vb_bytes, &vb) ||
        !MakeUploadBuffer(env.device, vb_bytes, &vb_upload)) { bail("vertex buffer alloc failed"); return; }
    // Deterministic geometry (BenchRng, fixed seed — determinism requirement).
    {
        void* mapped = nullptr;
        D3D12_RANGE rr{0, 0};
        if (FAILED(vb_upload->Map(0, &rr, &mapped))) { bail("vertex map failed"); return; }
        float* f = static_cast<float*>(mapped);
        BenchRng rng(0xABCDEF01ULL);
        for (uint32_t t = 0; t < kTris; ++t) {
            float cx = static_cast<float>(rng.NextUnit() * 4.0) - 2.0f;
            float cy = static_cast<float>(rng.NextUnit() * 4.0) - 2.0f;
            float cz = static_cast<float>(rng.NextUnit() * 4.0) + 1.0f; // z in [1,5]
            float s = 0.05f + static_cast<float>(0.10 * rng.NextUnit());
            const float tri[9] = {
                cx - s, cy - s, cz,
                cx + s, cy - s, cz,
                cx, cy + s, cz,
            };
            for (int k = 0; k < 9; ++k) f[t * 9 + k] = tri[k];
        }
        D3D12_RANGE wr{0, static_cast<SIZE_T>(vb_bytes)};
        vb_upload->Unmap(0, &wr);
    }

    // BLAS prebuild
    D3D12_RAYTRACING_GEOMETRY_DESC geo{};
    geo.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geo.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geo.Triangles.VertexBuffer.StartAddress = vb->GetGPUVirtualAddress();
    geo.Triangles.VertexBuffer.StrideInBytes = 12;
    geo.Triangles.VertexCount = kTris * 3;
    geo.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_in{};
    blas_in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    blas_in.NumDescs = 1;
    blas_in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blas_in.pGeometryDescs = &geo;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_pre{};
    dev5->GetRaytracingAccelerationStructurePrebuildInfo(&blas_in, &blas_pre);
    if (blas_pre.ResultDataMaxSizeInBytes == 0) { bail("blas prebuild failed"); return; }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_in{};
    tlas_in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlas_in.NumDescs = kInstances;
    tlas_in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_pre{};
    dev5->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_in, &tlas_pre);
    if (tlas_pre.ResultDataMaxSizeInBytes == 0) { bail("tlas prebuild failed"); return; }

    if (!MakeDefaultBuffer(env.device, blas_pre.ScratchDataSizeInBytes, &blas_scratch) ||
        !MakeDefaultBuffer(env.device, tlas_pre.ScratchDataSizeInBytes, &tlas_scratch) ||
        !MakeDefaultBuffer(env.device, kThreads * 8ull, &rt_result)) { bail("scratch alloc failed"); return; }

    // AS result buffers: RT AS initial state, ALLOW_UNORDERED_ACCESS flag
    // (spec-required for acceleration structure storage).
    auto make_as_buffer = [&](uint64_t bytes, ID3D12Resource** out) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        HRESULT h = env.device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
            IID_PPV_ARGS(out));
        return SUCCEEDED(h);
    };
    if (!make_as_buffer(blas_pre.ResultDataMaxSizeInBytes, &blas_result) ||
        !make_as_buffer(tlas_pre.ResultDataMaxSizeInBytes, &tlas_result)) { bail("as buffer alloc failed"); return; }
    if (!MakeUploadBuffer(env.device, kInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), &inst_upload)) {
        bail("instance buffer alloc failed"); return;
    }
    {
        void* mapped = nullptr;
        D3D12_RANGE rr{0, 0};
        if (FAILED(inst_upload->Map(0, &rr, &mapped))) { bail("instance map failed"); return; }
        auto* inst = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(mapped);
        for (uint32_t i = 0; i < kInstances; ++i) {
            memset(&inst[i], 0, sizeof(inst[i]));
            inst[i].InstanceID = i;
            inst[i].InstanceMask = 0xFF;
            inst[i].AccelerationStructure = blas_result->GetGPUVirtualAddress();
            inst[i].Transform[0][0] = 1.0f; inst[i].Transform[1][1] = 1.0f; inst[i].Transform[2][2] = 1.0f;
            inst[i].Transform[3][0] = static_cast<float>(i % 4) * 0.25f;      // spread instances
            inst[i].Transform[3][1] = static_cast<float>(i / 4) * 0.25f;
            inst[i].Transform[3][2] = 0.0f;
        }
        D3D12_RANGE wr{0, kInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC)};
        inst_upload->Unmap(0, &wr);
    }

    // Shader-visible heap: one SRV for the TLAS (canonical AS binding path).
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 1;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(env.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) { bail("heap create failed"); return; }
    env.device->CreateShaderResourceView(tlas_result, nullptr, heap->GetCPUDescriptorHandleForHeapStart());

    // ---- Setup: upload geometry, build BLAS + TLAS (GPU-timestamped) ------
    // list is recording-fresh after init (state discipline).
    env.list->EndQuery(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0); // build start
    env.list->CopyBufferRegion(vb, 0, vb_upload, 0, vb_bytes);
    // AS-build geometry buffers must be in NON_PIXEL_SHADER_RESOURCE state;
    // GPUVA access bypasses implicit promotion (buffers live in COMMON).
    {
        D3D12_RESOURCE_BARRIER rb{};
        rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb.Transition.pResource = vb;
        rb.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        rb.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        env.list->ResourceBarrier(1, &rb);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_build{};
    blas_build.ScratchAccelerationStructureData = blas_scratch->GetGPUVirtualAddress();
    blas_build.DestAccelerationStructureData = blas_result->GetGPUVirtualAddress();
    blas_build.Inputs = blas_in;
    list4->BuildRaytracingAccelerationStructure(&blas_build, 0, nullptr);

    D3D12_RESOURCE_BARRIER uav_bar{};
    uav_bar.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    env.list->ResourceBarrier(1, &uav_bar);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build{};
    tlas_build.ScratchAccelerationStructureData = tlas_scratch->GetGPUVirtualAddress();
    tlas_build.DestAccelerationStructureData = tlas_result->GetGPUVirtualAddress();
    tlas_build.Inputs = tlas_in;
    tlas_build.Inputs.InstanceDescs = inst_upload->GetGPUVirtualAddress();
    list4->BuildRaytracingAccelerationStructure(&tlas_build, 0, nullptr);
    env.list->EndQuery(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 1); // build end
    env.list->ResolveQueryData(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, env.ts_readback, 0);
    {
        std::string why;
        if (!Flush(env, env.list, &why)) {
            DrainInfoQueue(env, why);
            env.ok = false; env.error = why;
            rec.state = BenchState::Aborted; rec.abort_reason = "as-build: " + why;
            if (env.device) {
                HRESULT removed = env.device->GetDeviceRemovedReason();
                if (FAILED(removed)) rec.abort_reason += " " + HrMsg("removed", removed);
            }
            bail("as-build");
            return;
        }
    }
    // Setup metric: AS build GPU time (never mixed into the traversal score).
    double build_ms = 0.0;
    std::string build_why;
    if (ResolveTimestamps(env, 0, build_ms, &build_why)) {
        rec.elapsed_gpu_time_ms = build_ms;
    }

    // ---- Traversal sampling -----------------------------------------------
    auto record_work = [&]() {
        ID3D12DescriptorHeap* heaps[] = { heap };
        env.list->SetDescriptorHeaps(1, heaps);
        env.list->SetPipelineState(pso);
        env.list->SetComputeRootSignature(rs);
        env.list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
        env.list->SetComputeRootUnorderedAccessView(1, rt_result->GetGPUVirtualAddress());
        env.list->Dispatch(512, 1, 1);
        D3D12_RESOURCE_BARRIER bar{};
        bar.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        env.list->ResourceBarrier(1, &bar);
    };
    for (int w = 0; w < 2; ++w) {
        record_work();
        std::string why;
        if (!Flush(env, env.list, &why)) {
            DrainInfoQueue(env, why);
            env.ok = false; env.error = why;
            rec.state = BenchState::Aborted; rec.abort_reason = "warmup: " + why;
            if (env.device) {
                HRESULT removed = env.device->GetDeviceRemovedReason();
                if (FAILED(removed)) rec.abort_reason += " " + HrMsg("removed", removed);
            }
            bail("warmup");
            return;
        }
    }
    RunTimedSamples(env, cancel, "gpu.rt_inline", 1, "dc-bench/1:gpu-rt-inline", 2, record_work, rec);
    if (rec.state == BenchState::Complete) {
        // rays/ms: workloads/s / 1000 * rays-per-dispatch.
        const double rays = static_cast<double>(kThreads) * kRaysPerThread;
        rec.score = (rec.score / 1000.0) * rays;
    }

    SafeRelease(heap); SafeRelease(rt_result); SafeRelease(inst_upload);
    SafeRelease(tlas_result); SafeRelease(tlas_scratch); SafeRelease(blas_result);
    SafeRelease(blas_scratch); SafeRelease(vb_upload); SafeRelease(vb);
    SafeRelease(rs); SafeRelease(pso); SafeRelease(dev5); SafeRelease(list4);
}

// ---------------------------------------------------------------------------
// Full DXR pipeline (gpu.rt_pipeline): DXIL library + state object +
// DispatchRays. Same scene as the inline domain; measures the complete
// pipeline path (RG/CH/miss shading + shader-table fetch), a distinct
// performance domain from inline ray-query (never conflated).
// ---------------------------------------------------------------------------

void BenchGpuRaytracingPipeline(GpuBenchEnv& env, BenchCancel& cancel, BenchmarkRecord& rec,
                                double raytracing_tier) {
    StampRecord(rec, BenchId(env), "gpu.rt_pipeline", 1, 2, "dc-bench/1:gpu-rt-pipeline");
    if (!env.ok) { rec.state = BenchState::Unavailable; rec.abort_reason = env.error; return; }
    if (raytracing_tier < 1.0) {
        rec.state = BenchState::Unavailable;
        rec.abort_reason = "dxr-unavailable-on-host";
        return;
    }

    ID3D12Device5* dev5 = nullptr;
    if (FAILED(env.device->QueryInterface(IID_PPV_ARGS(&dev5))) || !dev5) {
        rec.state = BenchState::Unavailable; rec.abort_reason = "device5-unavailable";
        return;
    }
    ID3D12GraphicsCommandList4* list4 = nullptr;
    if (FAILED(env.list->QueryInterface(IID_PPV_ARGS(&list4))) || !list4) {
        SafeRelease(dev5);
        rec.state = BenchState::Unavailable; rec.abort_reason = "commandlist4-unavailable";
        return;
    }

    // Global root signature: descriptor table (t0 = TLAS) + inline raw UAV (u0).
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER rp[2]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[0].DescriptorTable.NumDescriptorRanges = 1;
    rp[0].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rp[1].Descriptor.ShaderRegister = 0;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 2;
    rsd.pParameters = rp;
    ID3DBlob* rs_blob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1_0, &rs_blob, nullptr);
    if (FAILED(hr)) {
        SafeRelease(dev5); SafeRelease(list4);
        rec.state = BenchState::Unavailable; rec.abort_reason = "rs serialize failed";
        return;
    }
    ID3D12RootSignature* rs = nullptr;
    hr = env.device->CreateRootSignature(0, rs_blob->GetBufferPointer(),
                                         rs_blob->GetBufferSize(), IID_PPV_ARGS(&rs));
    SafeRelease(rs_blob);
    if (FAILED(hr)) {
        SafeRelease(dev5); SafeRelease(list4);
        rec.state = BenchState::Unavailable; rec.abort_reason = HrMsg("rs create", hr);
        return;
    }

    // DXIL library (lib_6_3; gpu_rt_pipeline_dxil.h from gpu_rt_pipeline.hlsl).
    D3D12_SHADER_BYTECODE lib = { reinterpret_cast<const BYTE*>(kGpuRtPipelineDxil), kGpuRtPipelineDxilBytes };
    D3D12_DXIL_LIBRARY_DESC lib_desc{};
    lib_desc.DXILLibrary = lib;

    // Triangle geometry with a closest-hit shader requires a hit group —
    // ClosestHitMain is exported inside one, never dispatched directly.
    D3D12_HIT_GROUP_DESC hit_group{};
    hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hit_group.ClosestHitShaderImport = L"ClosestHitMain";
    hit_group.HitGroupExport = L"HitGroup0";

    D3D12_STATE_SUBOBJECT subs[6]{};
    subs[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subs[0].pDesc = &lib_desc;
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION assoc{};
    assoc.NumExports = 3;
    LPCWSTR exports[3] = { const_cast<LPCWSTR>(L"RayGenMain"), const_cast<LPCWSTR>(L"MissMain"), const_cast<LPCWSTR>(L"ClosestHitMain") };
    assoc.pExports = exports;
    assoc.pSubobjectToAssociate = &subs[0];
    subs[1].Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    subs[1].pDesc = &assoc;
    subs[2].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subs[2].pDesc = &rs;
    D3D12_RAYTRACING_SHADER_CONFIG shader_cfg{};
    shader_cfg.MaxPayloadSizeInBytes = 20;  // Payload { float4 color; uint hit; }
    shader_cfg.MaxAttributeSizeInBytes = 8; // BuiltInTriangleIntersectionAttributes
    subs[3].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subs[3].pDesc = &shader_cfg;
    subs[4].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subs[4].pDesc = &hit_group;
    D3D12_RAYTRACING_PIPELINE_CONFIG pipe_cfg{};
    pipe_cfg.MaxTraceRecursionDepth = 1;
    subs[5].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subs[5].pDesc = &pipe_cfg;

    D3D12_STATE_OBJECT_DESC so_desc{};
    so_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    so_desc.NumSubobjects = 6;
    so_desc.pSubobjects = subs;
    ID3D12StateObject* state_obj = nullptr;
    hr = dev5->CreateStateObject(&so_desc, IID_PPV_ARGS(&state_obj));
    if (FAILED(hr) || !state_obj) {
        // Honest failure (C10): a fallback state object without the exports
        // association constructs but yields unusable shader identifiers —
        // never proceed with a semantically broken object. E_INVALIDARG with
        // a DXIL-library subobject is the DevKit-0 OS-runtime blocker
        // (RESEARCH_LEDGER §25, BLOCKED_ENVIRONMENT).
        SafeRelease(rs); SafeRelease(dev5); SafeRelease(list4);
        rec.state = BenchState::Unavailable;
        rec.abort_reason = HrMsg("state object", hr) + " dxil-library-rejected (see ledger s25)";
        return;
    }
    ID3D12StateObjectProperties* props = nullptr;
    if (FAILED(state_obj->QueryInterface(IID_PPV_ARGS(&props))) || !props) {
        SafeRelease(state_obj); SafeRelease(rs); SafeRelease(dev5); SafeRelease(list4);
        rec.state = BenchState::Unavailable; rec.abort_reason = "state object properties unavailable";
        return;
    }
    void* rg_id = props->GetShaderIdentifier(L"RayGenMain");
    void* miss_id = props->GetShaderIdentifier(L"MissMain");
    void* hg_id = props->GetShaderIdentifier(L"HitGroup0");
    if (!rg_id || !miss_id || !hg_id) {
        SafeRelease(props); SafeRelease(state_obj); SafeRelease(rs); SafeRelease(dev5); SafeRelease(list4);
        rec.state = BenchState::Unavailable; rec.abort_reason = "shader identifier resolution failed";
        return;
    }

    // ---- Scene: identical deterministic scene to the inline domain --------
    constexpr uint32_t kTris = 4096;
    constexpr uint32_t kInstances = 8;
    constexpr uint32_t kRays = 512u * 256u; // one ray per DispatchRays dimension cell

    const uint64_t vb_bytes = static_cast<uint64_t>(kTris) * 3ull * 12ull;
    ID3D12Resource* vb = nullptr;
    ID3D12Resource* vb_upload = nullptr;
    ID3D12Resource* blas_scratch = nullptr;
    ID3D12Resource* blas_result = nullptr;
    ID3D12Resource* tlas_scratch = nullptr;
    ID3D12Resource* tlas_result = nullptr;
    ID3D12Resource* inst_upload = nullptr;
    ID3D12Resource* rt_result = nullptr;
    ID3D12Resource* table_upload = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;

    auto bail = [&](const char* why_msg) {
        if (rec.state == BenchState::NotRun) {
            rec.state = BenchState::Unavailable; rec.abort_reason = why_msg;
        }
        SafeRelease(heap); SafeRelease(table_upload); SafeRelease(rt_result); SafeRelease(inst_upload);
        SafeRelease(tlas_result); SafeRelease(tlas_scratch); SafeRelease(blas_result);
        SafeRelease(blas_scratch); SafeRelease(vb_upload); SafeRelease(vb);
        SafeRelease(props); SafeRelease(state_obj); SafeRelease(rs); SafeRelease(dev5); SafeRelease(list4);
    };

    if (!MakeDefaultBuffer(env.device, vb_bytes, &vb) ||
        !MakeUploadBuffer(env.device, vb_bytes, &vb_upload)) { bail("vertex buffer alloc failed"); return; }
    {
        void* mapped = nullptr;
        D3D12_RANGE rr{0, 0};
        if (FAILED(vb_upload->Map(0, &rr, &mapped))) { bail("vertex map failed"); return; }
        float* f = static_cast<float*>(mapped);
        BenchRng rng(0xABCDEF01ULL); // same seed => same scene as gpu.rt_inline
        for (uint32_t t = 0; t < kTris; ++t) {
            float cx = static_cast<float>(rng.NextUnit() * 4.0) - 2.0f;
            float cy = static_cast<float>(rng.NextUnit() * 4.0) - 2.0f;
            float cz = static_cast<float>(rng.NextUnit() * 4.0) + 1.0f;
            float s = 0.05f + static_cast<float>(0.10 * rng.NextUnit());
            const float tri[9] = { cx - s, cy - s, cz, cx + s, cy - s, cz, cx, cy + s, cz };
            for (int k = 0; k < 9; ++k) f[t * 9 + k] = tri[k];
        }
        D3D12_RANGE wr{0, static_cast<SIZE_T>(vb_bytes)};
        vb_upload->Unmap(0, &wr);
    }

    D3D12_RAYTRACING_GEOMETRY_DESC geo{};
    geo.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geo.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geo.Triangles.VertexBuffer.StartAddress = vb->GetGPUVirtualAddress();
    geo.Triangles.VertexBuffer.StrideInBytes = 12;
    geo.Triangles.VertexCount = kTris * 3;
    geo.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_in{};
    blas_in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    blas_in.NumDescs = 1;
    blas_in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blas_in.pGeometryDescs = &geo;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_pre{};
    dev5->GetRaytracingAccelerationStructurePrebuildInfo(&blas_in, &blas_pre);
    if (blas_pre.ResultDataMaxSizeInBytes == 0) { bail("blas prebuild failed"); return; }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_in{};
    tlas_in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlas_in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlas_in.NumDescs = kInstances;
    tlas_in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_pre{};
    dev5->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_in, &tlas_pre);
    if (tlas_pre.ResultDataMaxSizeInBytes == 0) { bail("tlas prebuild failed"); return; }

    if (!MakeDefaultBuffer(env.device, blas_pre.ScratchDataSizeInBytes, &blas_scratch) ||
        !MakeDefaultBuffer(env.device, tlas_pre.ScratchDataSizeInBytes, &tlas_scratch) ||
        !MakeDefaultBuffer(env.device, kRays * 16ull, &rt_result)) { bail("scratch alloc failed"); return; }

    auto make_as_buffer = [&](uint64_t bytes, ID3D12Resource** out) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        HRESULT h = env.device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
            IID_PPV_ARGS(out));
        return SUCCEEDED(h);
    };
    if (!make_as_buffer(blas_pre.ResultDataMaxSizeInBytes, &blas_result) ||
        !make_as_buffer(tlas_pre.ResultDataMaxSizeInBytes, &tlas_result)) { bail("as buffer alloc failed"); return; }
    if (!MakeUploadBuffer(env.device, kInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), &inst_upload)) {
        bail("instance buffer alloc failed"); return;
    }
    {
        void* mapped = nullptr;
        D3D12_RANGE rr{0, 0};
        if (FAILED(inst_upload->Map(0, &rr, &mapped))) { bail("instance map failed"); return; }
        auto* inst = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(mapped);
        for (uint32_t i = 0; i < kInstances; ++i) {
            memset(&inst[i], 0, sizeof(inst[i]));
            inst[i].InstanceID = i;
            inst[i].InstanceMask = 0xFF;
            inst[i].AccelerationStructure = blas_result->GetGPUVirtualAddress();
            inst[i].Transform[0][0] = 1.0f; inst[i].Transform[1][1] = 1.0f; inst[i].Transform[2][2] = 1.0f;
            inst[i].Transform[3][0] = static_cast<float>(i % 4) * 0.25f;
            inst[i].Transform[3][1] = static_cast<float>(i / 4) * 0.25f;
            inst[i].Transform[3][2] = 0.0f;
        }
        D3D12_RANGE wr{0, kInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC)};
        inst_upload->Unmap(0, &wr);
    }

    // Shader table (upload heap): raygen record + miss + closest-hit records.
    // Record layout: shader identifier (32 bytes) + root args (none here).
    constexpr uint32_t kRecordSize = 64; // identifier + padding (alignment 32; D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT = 64)
    constexpr uint32_t kTableBytes = kRecordSize * 3;
    if (!MakeUploadBuffer(env.device, kTableBytes, &table_upload)) { bail("shader table alloc failed"); return; }
    {
        void* mapped = nullptr;
        D3D12_RANGE rr{0, 0};
        if (FAILED(table_upload->Map(0, &rr, &mapped))) { bail("shader table map failed"); return; }
        uint8_t* t = static_cast<uint8_t*>(mapped);
        memcpy(t + 0 * kRecordSize, rg_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy(t + 1 * kRecordSize, miss_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy(t + 2 * kRecordSize, hg_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        D3D12_RANGE wr{0, kTableBytes};
        table_upload->Unmap(0, &wr);
    }

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 1;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(env.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) { bail("heap create failed"); return; }
    env.device->CreateShaderResourceView(tlas_result, nullptr, heap->GetCPUDescriptorHandleForHeapStart());

    // ---- Setup: upload geometry, build BLAS + TLAS (GPU-timestamped) ------
    env.list->EndQuery(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
    env.list->CopyBufferRegion(vb, 0, vb_upload, 0, vb_bytes);
    {
        D3D12_RESOURCE_BARRIER rb{};
        rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb.Transition.pResource = vb;
        rb.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        rb.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        env.list->ResourceBarrier(1, &rb);
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_build{};
    blas_build.ScratchAccelerationStructureData = blas_scratch->GetGPUVirtualAddress();
    blas_build.DestAccelerationStructureData = blas_result->GetGPUVirtualAddress();
    blas_build.Inputs = blas_in;
    list4->BuildRaytracingAccelerationStructure(&blas_build, 0, nullptr);
    D3D12_RESOURCE_BARRIER uav_bar{};
    uav_bar.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    env.list->ResourceBarrier(1, &uav_bar);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build{};
    tlas_build.ScratchAccelerationStructureData = tlas_scratch->GetGPUVirtualAddress();
    tlas_build.DestAccelerationStructureData = tlas_result->GetGPUVirtualAddress();
    tlas_build.Inputs = tlas_in;
    tlas_build.Inputs.InstanceDescs = inst_upload->GetGPUVirtualAddress();
    list4->BuildRaytracingAccelerationStructure(&tlas_build, 0, nullptr);
    env.list->EndQuery(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
    env.list->ResolveQueryData(env.ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, env.ts_readback, 0);
    {
        std::string why;
        if (!Flush(env, env.list, &why)) {
            DrainInfoQueue(env, why);
            env.ok = false; env.error = why;
            rec.state = BenchState::Aborted; rec.abort_reason = "as-build: " + why;
            if (env.device) {
                HRESULT removed = env.device->GetDeviceRemovedReason();
                if (FAILED(removed)) rec.abort_reason += " " + HrMsg("removed", removed);
            }
            bail("as-build");
            return;
        }
    }
    double build_ms = 0.0;
    std::string build_why;
    if (ResolveTimestamps(env, 0, build_ms, &build_why)) {
        rec.elapsed_gpu_time_ms = build_ms;
    }

    // ---- DispatchRays sampling --------------------------------------------
    D3D12_DISPATCH_RAYS_DESC drd{};
    drd.RayGenerationShaderRecord.StartAddress = table_upload->GetGPUVirtualAddress() + 0 * kRecordSize;
    drd.RayGenerationShaderRecord.SizeInBytes = kRecordSize;
    drd.MissShaderTable.StartAddress = table_upload->GetGPUVirtualAddress() + 1 * kRecordSize;
    drd.MissShaderTable.SizeInBytes = kRecordSize;
    drd.MissShaderTable.StrideInBytes = kRecordSize;
    drd.HitGroupTable.StartAddress = table_upload->GetGPUVirtualAddress() + 2 * kRecordSize;
    drd.HitGroupTable.SizeInBytes = kRecordSize;
    drd.HitGroupTable.StrideInBytes = kRecordSize;
    drd.Width = 512;
    drd.Height = 256;
    drd.Depth = 1;

    auto record_work = [&]() {
        ID3D12DescriptorHeap* heaps[] = { heap };
        env.list->SetDescriptorHeaps(1, heaps);
        env.list->SetComputeRootSignature(rs);
        env.list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
        env.list->SetComputeRootUnorderedAccessView(1, rt_result->GetGPUVirtualAddress());
        list4->DispatchRays(&drd);
        D3D12_RESOURCE_BARRIER bar{};
        bar.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        env.list->ResourceBarrier(1, &bar);
    };
    for (int w = 0; w < 2; ++w) {
        record_work();
        std::string why;
        if (!Flush(env, env.list, &why)) {
            DrainInfoQueue(env, why);
            env.ok = false; env.error = why;
            rec.state = BenchState::Aborted; rec.abort_reason = "warmup: " + why;
            if (env.device) {
                HRESULT removed = env.device->GetDeviceRemovedReason();
                if (FAILED(removed)) rec.abort_reason += " " + HrMsg("removed", removed);
            }
            bail("warmup");
            return;
        }
    }
    RunTimedSamples(env, cancel, "gpu.rt_pipeline", 1, "dc-bench/1:gpu-rt-pipeline", 2, record_work, rec);
    if (rec.state == BenchState::Complete) {
        rec.score = (rec.score / 1000.0) * static_cast<double>(kRays);
    }

    SafeRelease(heap); SafeRelease(table_upload); SafeRelease(rt_result); SafeRelease(inst_upload);
    SafeRelease(tlas_result); SafeRelease(tlas_scratch); SafeRelease(blas_result);
    SafeRelease(blas_scratch); SafeRelease(vb_upload); SafeRelease(vb);
    SafeRelease(props); SafeRelease(state_obj); SafeRelease(rs); SafeRelease(dev5); SafeRelease(list4);
}

} // namespace dcwin
