// FidelityLab — the DK0-M4 native fidelity vertical slice (M2 §29 seed,
// M4 §11/§14/§15/§32/§33). Same package lineage as native-minimal.
//
// What M4 adds on top of the M2 runtime proof:
//   - the title declares a dc.fidelity/1 contract (packaged at
//     manifests/fidelity.json) and CONSUMES the platform's fidelity service:
//     the governor — never the title — selects concrete states from the
//     compiled candidate set (C5/C6: no Low/Medium/High/Ultra, no presets)
//   - the selected states are APPLIED for real (§14): internal resolution
//     renders at a genuinely different target size, geometry/shadow/
//     reflection/volumetric/particle states change the actual scene
//     composition. The harness can SEE which state is active (§33).
//   - the title MEASURES its own frame cost with D3D12 timestamp queries and
//     feeds measured windows back to the governor (§10: measured, never
//     fabricated — unknown metrics stay unknown)
//   - simulation stays deterministic and frame-rate independent (§15): the
//     sim runs on accumulated real time, never on frame count; the
//     simulation_quality domain only changes simulated entity density
//
// Modes (§32, one binary, deterministic scenes):
//   DC_FIDELITY_MODE=visual|simulation|baseline  (default baseline)
//   DC_FIDELITY_PIN=<candidate_id>   developer pin — diagnostics/capture only
//   DC_FIDELITY_STRESS=1             §40 controlled GPU pressure: fixed heavy
//                                    composition for the adaptation experiment
//   DC_TITLE_CONTEXT                 typed launch envelope (M2 contract)
#define _CRT_SECURE_NO_WARNINGS
#include "dc/fidelity_service.hpp"
#include "dc/input.hpp"
#include "dc/title_context.hpp"
#include "dc/title_context_json.hpp"
#include "dc/title_supervisor.hpp"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace dc {
IInputRouter* CreateGameInputRouter();   // hosts/windows (GameInput primary)
void DestroyGameInputRouter(IInputRouter*);
} // namespace dc

namespace {

constexpr int kExitClean = 0;
constexpr float kPulseRate = 2.4f;   // dev-run (no controller) pulse rate

// ---- scene parameters per authored state id (§14: real application) --------
// The contract's state ids are authoritative; these tables translate state
// into actual scene composition. Costs were authored in the contract to
// match (DECLARED class; §10 distinguishes them from MEASURED at runtime).
struct SceneParams {
    UINT instances;      // geometry_density geo_0..2
    UINT shadow_slabs;   // shadow_quality shadow_0..2
    UINT refl_layers;    // reflection_quality refl_0..2
    UINT vol_slabs;      // volumetric_quality vol_0..1
    UINT particles;      // particle_density part_0..1
};

SceneParams SceneFor(const std::string& geo, const std::string& shadow,
                     const std::string& refl, const std::string& vol,
                     const std::string& part) {
    SceneParams p{};
    p.instances     = geo == "geo_2" ? 640 : geo == "geo_1" ? 320 : 120;
    p.shadow_slabs  = shadow == "shadow_2" ? 6 : shadow == "shadow_1" ? 3 : 1;
    p.refl_layers   = refl == "refl_2" ? 3 : refl == "refl_1" ? 2 : 1;
    p.vol_slabs     = vol == "vol_1" ? 10 : 4;
    p.particles     = part == "part_1" ? 180 : 48;
    return p;
}

// Simulation entity count per sim state (§15: gameplay-safe — only entity
// density; rules, ordering, and semantics are identical in every state).
UINT SimEntities(const std::string& sim) { return sim == "sim_1" ? 256 : 64; }

// Internal-resolution per res state (fraction of output size).
float ResFraction(const std::string& res) {
    return res == "res_100" ? 1.0f : res == "res_75" ? 0.75f : 0.50f;
}

double GpuPeriodMs(UINT64 delta, UINT64 freq) {
    return freq ? 1000.0 * double(delta) / double(freq) : 0.0;
}

// Heap-property helper (avoids pulling d3dx12.h).
struct CD3DX12_HEAP_PROP_DEFAULT_ {
    operator const D3D12_HEAP_PROPERTIES*() const {
        static D3D12_HEAP_PROPERTIES h = [] {
            D3D12_HEAP_PROPERTIES x{};
            x.Type = D3D12_HEAP_TYPE_DEFAULT;
            return x;
        }();
        return &h;
    }
};
inline CD3DX12_HEAP_PROP_DEFAULT_ CD3DX12_HEAP_PROP_DEFAULT() { return {}; }

// Root-constant block (matches cbuffer b0; 12 floats).
struct DrawUniforms {
    float xform[4];   // xy center, zw half-scale
    float col[4];     // rgba
    float misc[4];    // x = slab phase
};

// Resolved per-domain state ids driving the scene (§14).
struct TitleState {
    std::string res, geo, shadow, refl, vol, part, sim;
    bool valid = false;
};

// Pull the current selection into concrete state ids (§12 semantic API:
// titles see state ids per domain, never candidates or cost structures).
void ApplyStates(dc::fidelity::FidelityService* svc, TitleState* st) {
    if (std::string v = svc->StateFor("internal_resolution"); !v.empty()) st->res = v;
    if (std::string v = svc->StateFor("geometry_density"); !v.empty()) st->geo = v;
    if (std::string v = svc->StateFor("shadow_quality"); !v.empty()) st->shadow = v;
    if (std::string v = svc->StateFor("reflection_quality"); !v.empty()) st->refl = v;
    if (std::string v = svc->StateFor("volumetric_quality"); !v.empty()) st->vol = v;
    if (std::string v = svc->StateFor("particle_density"); !v.empty()) st->part = v;
    if (std::string v = svc->StateFor("simulation_quality"); !v.empty()) st->sim = v;
    st->valid = true;
}

// ---- deterministic simulation entity (§15) ---------------------------------
struct SimEntity {
    float phase0;   // radians in [0, 2π)
    float speed;    // rad/s
    float r, g, b;
    float dist;     // orbit radius
};

// ---- procedural scene shaders (no engine, no assets; SM5.0) ----------------
// One VS (SV_VertexID quad); three PS: solid composition, uv-patterned
// volumetric slab, and the internal-resolution upscale pass.
const char kShaderHlsl[] = R"HLSL(
cbuffer Xf : register(b0) {
    float4 g_xform;   // xy = center, zw = half-scale (w doubles as VS clip)
    float4 g_col;     // rgba
    float4 g_misc;    // x = slab phase, y unused, z unused, w unused
};
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};
VSOut VSMain(uint vid : SV_VertexID) {
    float2 corners[6] = { float2(-1,-1), float2(1,-1), float2(-1,1),
                          float2(-1,1), float2(1,-1), float2(1,1) };
    VSOut o;
    float2 p = corners[vid] * g_xform.zw + g_xform.xy;
    o.pos = float4(p, 0, 1);
    o.uv  = corners[vid] * 0.5 + 0.5;
    o.col = g_col;
    return o;
}
float4 PSSolid(VSOut i) : SV_Target { return i.col; }
float4 PSSlab(VSOut i) : SV_Target {
    float n = sin(i.uv.x * 14.0 + g_misc.x) * cos(i.uv.y * 11.0 - g_misc.x * 0.7);
    float a = saturate(0.5 + 0.5 * n) * i.col.a;
    return float4(i.col.rgb, a);
}
Texture2D    g_src  : register(t0);
SamplerState g_samp : register(s0);
float4 PSUpscale(VSOut i) : SV_Target { return g_src.Sample(g_samp, i.uv); }
)HLSL";

// ---- QR1 lifecycle (M2 contract, unchanged) --------------------------------
enum class LifeState { Running, Suspended };
volatile LifeState g_life = LifeState::Running;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            if (msg == dc::kTitleMsgSuspend) {
                g_life = LifeState::Suspended;
                std::printf("fidelitylab: checkpoint (suspend)\n");
                std::fflush(stdout);
                wchar_t ack[128];
                dc::TitleEventName(L"ACK", GetCurrentProcessId(), ack, 128);
                if (HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, ack)) {
                    SetEvent(h);
                    CloseHandle(h);
                }
                return 0;
            }
            if (msg == dc::kTitleMsgResume) {
                g_life = LifeState::Running;
                std::printf("fidelitylab: resume\n");
                std::fflush(stdout);
                wchar_t ack[128];
                dc::TitleEventName(L"ACK", GetCurrentProcessId(), ack, 128);
                if (HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, ack)) {
                    SetEvent(h);
                    CloseHandle(h);
                }
                return 0;
            }
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

// ---- D3D12 title ------------------------------------------------------------
class D3D12Title {
public:
    bool device_dead = false;   // set when removal/fence-hang is observed
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> cmdlist;
    ComPtr<IDXGISwapChain3> swapchain;
    UINT width = 1280, height = 720;

    ID3D12CommandQueue* Queue() { return queue.Get(); }
    ID3D12Device* Device() { return device.Get(); }

    bool Init(HWND hwnd) {
        // Adapter selection (hybrid-GPU laptops): explicit, evidence-printed.
        // The old path was D3D12CreateDevice(nullptr) — "whatever DXGI picks",
        // which on this DevKit resolves against the iGPU/driver stack with a
        // TDR history. A console host must choose its compute adapter the way
        // the qualification pipeline did: max dedicated VRAM among D3D12-
        // capable hardware adapters. DC_TITLE_GPU=<index> pins one explicitly.
        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
        std::vector<ComPtr<IDXGIAdapter1>> d3d12_adapters;
        {
            ComPtr<IDXGIAdapter1> it;
            for (UINT i = 0;
                 factory->EnumAdapters1(i, &it) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 d{};
                if (FAILED(it->GetDesc1(&d))) continue;
                if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                ComPtr<ID3D12Device> probe;
                if (FAILED(D3D12CreateDevice(it.Get(), D3D_FEATURE_LEVEL_11_0,
                                             IID_PPV_ARGS(&probe))))
                    continue;
                d3d12_adapters.push_back(it);
            }
        }
        if (d3d12_adapters.empty()) return false;
        UINT pick = 0;
        SIZE_T best_mem = 0;
        for (UINT i = 0; i < d3d12_adapters.size(); ++i) {
            DXGI_ADAPTER_DESC1 d{};
            d3d12_adapters[i]->GetDesc1(&d);
            if (d.DedicatedVideoMemory > best_mem) {
                best_mem = d.DedicatedVideoMemory;
                pick = i;
            }
        }
        if (const char* e = std::getenv("DC_TITLE_GPU")) {
            const long w = std::strtol(e, nullptr, 10);
            if (w >= 0 && UINT(w) < d3d12_adapters.size()) pick = UINT(w);
        }
        DXGI_ADAPTER_DESC1 chosen_desc{};
        d3d12_adapters[pick]->GetDesc1(&chosen_desc);
        char name[128] = {};
        WideCharToMultiByte(CP_ACP, 0, chosen_desc.Description, -1, name,
                            sizeof(name), nullptr, nullptr);
        std::printf("fidelitylab: adapter[%u] %s vid=0x%04X did=0x%04X "
                    "vram=%.1fGB luid=%08lX:%08lX\n",
                    pick, name, chosen_desc.VendorId, chosen_desc.DeviceId,
                    chosen_desc.DedicatedVideoMemory / 1073741824.0,
                    (unsigned long)chosen_desc.AdapterLuid.HighPart,
                    (unsigned long)chosen_desc.AdapterLuid.LowPart);
        std::fflush(stdout);

        if (FAILED(D3D12CreateDevice(d3d12_adapters[pick].Get(),
                                     D3D_FEATURE_LEVEL_11_0,
                                     IID_PPV_ARGS(&device))))
            return false;
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))))
            return false;
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&allocator))))
            return false;
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&cmdlist))))
            return false;
        cmdlist->Close();

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = width;
        sd.Height = height;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = kFrameCount;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        // The swapchain is created from the SAME factory the render device's
        // adapter came from — the documented hybrid-GPU alignment rule.
        ComPtr<IDXGISwapChain1> sc1;
        if (FAILED(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &sd,
                                                   nullptr, nullptr, &sc1)))
            return false;
        if (FAILED(sc1.As(&swapchain))) return false;

        // RTV heap: kFrameCount backbuffers + 1 offscreen scene target.
        D3D12_DESCRIPTOR_HEAP_DESC rhd{};
        rhd.NumDescriptors = kFrameCount + 1;
        rhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(device->CreateDescriptorHeap(&rhd, IID_PPV_ARGS(&rtv_heap))))
            return false;
        rtv_size = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT i = 0; i < kFrameCount; ++i) {
            if (FAILED(swapchain->GetBuffer(i, IID_PPV_ARGS(&backbufs[i]))))
                return false;
            D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap->GetCPUDescriptorHandleForHeapStart();
            h.ptr += i * rtv_size;
            device->CreateRenderTargetView(backbufs[i].Get(), nullptr, h);
        }
        offscreen_rtv_ = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        offscreen_rtv_.ptr += kFrameCount * rtv_size;

        // SRV heap for the upscale source + sampler heap.
        D3D12_DESCRIPTOR_HEAP_DESC shd{};
        shd.NumDescriptors = 1;
        shd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        if (FAILED(device->CreateDescriptorHeap(&shd, IID_PPV_ARGS(&srv_heap))))
            return false;
        D3D12_DESCRIPTOR_HEAP_DESC smhd{};
        smhd.NumDescriptors = 1;
        smhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        if (FAILED(device->CreateDescriptorHeap(&smhd, IID_PPV_ARGS(&sampler_heap))))
            return false;
        D3D12_SAMPLER_DESC sm{};
        sm.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sm.AddressU = sm.AddressV = sm.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sm.MaxAnisotropy = 1;
        sm.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sm.MaxLOD = D3D12_FLOAT32_MAX;
        device->CreateSampler(&sm, sampler_heap->GetCPUDescriptorHandleForHeapStart());

        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&fence))))
            return false;
        fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return fence_event != nullptr;
    }

    bool InitGraphics(const void* vs, size_t vs_size, const void* ps_solid,
                      size_t solid_size, const void* ps_slab, size_t slab_size,
                      const void* ps_up, size_t up_size) {
        // Root signature: [0] root constants (b0), [1] SRV table (t0),
        // static linear sampler.
        D3D12_ROOT_PARAMETER rp[2]{};
        D3D12_ROOT_CONSTANTS rc{};
        rc.ShaderRegister = 0;
        rc.Num32BitValues = 12;
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[0].Constants = rc;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_DESCRIPTOR_RANGE dr{};
        dr.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        dr.NumDescriptors = 1;
        dr.BaseShaderRegister = 0;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable.NumDescriptorRanges = 1;
        rp[1].DescriptorTable.pDescriptorRanges = &dr;
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC ss{};
        ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss.MaxAnisotropy = 1;
        ss.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        ss.MaxLOD = D3D12_FLOAT32_MAX;
        ss.ShaderRegister = 0;
        ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 2;
        rsd.pParameters = rp;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers = &ss;
        ComPtr<ID3DBlob> sig, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &sig, &err)))
            return false;
        if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(),
                                               sig->GetBufferSize(),
                                               IID_PPV_ARGS(&root_sig))))
            return false;

        // PSOs: solid (instances/particles/reflections), slab (volumetrics),
        // upscale (backbuf composite). All alpha-blended; upscale opaque.
        const D3D12_SHADER_BYTECODE bvs{vs, vs_size};
        auto make_pso = [&](const void* ps, size_t ps_size, bool blend,
                            bool, ComPtr<ID3D12PipelineState>* out) {   // srv_bind unused (static sampler)
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
            pd.pRootSignature = root_sig.Get();
            pd.VS = bvs;
            pd.PS = {ps, ps_size};
            pd.BlendState.RenderTarget[0].RenderTargetWriteMask =
                D3D12_COLOR_WRITE_ENABLE_ALL;
            if (blend) {
                auto& b = pd.BlendState.RenderTarget[0];
                b.BlendEnable = TRUE;
                b.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                b.BlendOp = D3D12_BLEND_OP_ADD;
                b.SrcBlendAlpha = D3D12_BLEND_ONE;
                b.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            }
            pd.SampleMask = UINT_MAX;
            pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pd.NumRenderTargets = 1;
            pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            pd.SampleDesc.Count = 1;
            return SUCCEEDED(device->CreateGraphicsPipelineState(
                &pd, IID_PPV_ARGS(out->GetAddressOf())));
        };
        if (!make_pso(ps_solid, solid_size, true, false, &pso_solid))
            return false;
        if (!make_pso(ps_slab, slab_size, true, false, &pso_slab)) return false;
        if (!make_pso(ps_up, up_size, false, true, &pso_upscale)) return false;
        // M1-F checkpoint: PSO creation performs driver-side shader
        // compilation; on this DevKit the driver can TDR asynchronously
        // right here. Surface it immediately instead of failing later with
        // no context.
        const HRESULT pso_removed = device->GetDeviceRemovedReason();
        if (FAILED(pso_removed)) {
            std::printf("fidelitylab: device removed during PSO creation "
                        "removed=0x%08lX\n", (unsigned long)pso_removed);
            std::fflush(stdout);
            return false;
        }

        // Internal-resolution target at full size initially.
        return EnsureSceneTarget(1.0f);
    }

    // Recreates the offscreen scene target for a new internal resolution
    // (§14: the res domain really renders at a different size). Returns true
    // when the target already matches; on recreation failure the previous
    // target stays (graceful — fidelity may not crash the title).
    bool EnsureSceneTarget(float frac) {
        UINT w = std::max<UINT>(64, UINT(width * frac));
        UINT h = std::max<UINT>(36, UINT(height * frac));
        if (scene_target_ && w == scene_w_ && h == scene_h_) return true;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = w;
        rd.Height = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.SampleDesc.Count = 1;
        ComPtr<ID3D12Resource> tex;
        D3D12_HEAP_PROPERTIES hp_default{};
        hp_default.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (FAILED(device->CreateCommittedResource(
                &hp_default, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(&tex))))
            return false;
        device->CreateRenderTargetView(tex.Get(), nullptr, offscreen_rtv_);
        device->CreateShaderResourceView(tex.Get(), nullptr,
                                         srv_heap->GetCPUDescriptorHandleForHeapStart());
        scene_target_ = tex;   // old target released (GPU idle: fence-waited)
        scene_w_ = w;
        scene_h_ = h;
        return true;
    }

    // One measured frame (§10/§14): scene at the internal resolution, then
    // upscale composite to the backbuffer. GPU cost bracketed by timestamp
    // queries and resolved into ts_readback.
    void RenderScene(float pulse, float res_frac, const SceneParams& sp,
                     const SimEntity* sims, UINT sim_count, double sim_time,
                     ID3D12QueryHeap* ts_heap, ID3D12Resource* ts_readback) {
        if (device_dead) return;
        EnsureSceneTarget(res_frac);

        frame_index = swapchain->GetCurrentBackBufferIndex();
        allocator->Reset();
        cmdlist->Reset(allocator.Get(), pso_solid.Get());

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backbufs[frame_index].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cmdlist->ResourceBarrier(1, &barrier);

        // GPU frame-cost bracket (§10): START before any scene work.
        if (ts_heap) cmdlist->EndQuery(ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);

        ID3D12DescriptorHeap* heaps[] = {srv_heap.Get(), sampler_heap.Get()};
        cmdlist->SetDescriptorHeaps(2, heaps);
        cmdlist->SetGraphicsRootSignature(root_sig.Get());

        // ---- scene pass at the internal resolution --------------------------
        D3D12_CPU_DESCRIPTOR_HANDLE scrtv = offscreen_rtv_;
        cmdlist->OMSetRenderTargets(1, &scrtv, FALSE, nullptr);
        D3D12_VIEWPORT vp{0, 0, float(scene_w_), float(scene_h_), 0.f, 1.f};
        D3D12_RECT sc{0, 0, LONG(scene_w_), LONG(scene_h_)};
        cmdlist->RSSetViewports(1, &vp);
        cmdlist->RSSetScissorRects(1, &sc);

        // Background: deep console blue, pulse-lit (M2 continuity).
        cmdlist->SetPipelineState(pso_solid.Get());
        const float bg_xf[4] = {0, 0, 1, 1};
        const float bg_col[4] = {0.04f + 0.05f * pulse, 0.07f + 0.08f * pulse,
                                 0.16f + 0.30f * pulse, 1.0f};
        DrawQuad(bg_xf, bg_col, 0.0f);

        // Shadow slabs: instance silhouettes offset by slab index (darker,
        // behind geometry) — the shadow domain's real cost: instances × slabs.
        for (UINT s = 1; s <= sp.shadow_slabs; ++s) {
            for (UINT i = 0; i < sp.instances; ++i) {
                float px, py, sz;
                InstancePos(i, sim_time, pulse, &px, &py, &sz);
                const float xf[4] = {px + 0.035f * s, py + 0.055f * s, sz, sz};
                const float col[4] = {0.0f, 0.0f, 0.0f, 0.30f / s};
                DrawQuad(xf, col, 0.0f);
            }
        }

        // Reflection layers: mirrored instances below the "floor" line.
        for (UINT l = 1; l <= sp.refl_layers; ++l) {
            for (UINT i = 0; i < sp.instances; ++i) {
                float px, py, sz;
                InstancePos(i, sim_time, pulse, &px, &py, &sz);
                float r, g, b;
                InstanceColor(i, pulse, &r, &g, &b);
                const float xf[4] = {px * 1.02f, -py * 0.72f - 0.62f, sz, sz};
                const float col[4] = {r, g, b, 0.16f / l};
                DrawQuad(xf, col, 0.0f);
            }
        }

        // Geometry instances (the visible body of the scene).
        for (UINT i = 0; i < sp.instances; ++i) {
            float px, py, sz;
            InstancePos(i, sim_time, pulse, &px, &py, &sz);
            float r, g, b;
            InstanceColor(i, pulse, &r, &g, &b);
            const float xf[4] = {px, py, sz, sz};
            const float col[4] = {r, g, b, 0.85f};
            DrawQuad(xf, col, 0.0f);
        }

        // Simulation entities (§15: density from the sim domain; motion from
        // real elapsed time — never frame count).
        for (UINT i = 0; i < sim_count; ++i) {
            const SimEntity& e = sims[i];
            const float a = e.phase0 + float(sim_time) * e.speed;
            const float xf[4] = {std::cos(a) * e.dist,
                                 std::sin(a) * e.dist * 0.75f, 0.014f, 0.014f};
            const float col[4] = {e.r, e.g, e.b, 0.9f};
            DrawQuad(xf, col, 0.0f);
        }

        // Particles (fine quads on independent deterministic orbits).
        for (UINT i = 0; i < sp.particles; ++i) {
            const float a = float(i) * 0.6180339f + float(sim_time) *
                            (0.4f + 0.2f * float(i % 5));
            const float rad = 0.2f + 0.25f * float(i % 9) / 9.0f;
            const float tw = 0.5f + 0.5f * std::sin(a * 3.0f);
            const float xf[4] = {std::cos(a) * rad, std::sin(a) * rad * 0.75f,
                                 0.008f, 0.008f};
            const float col[4] = {1.0f, 0.85f, 0.4f, 0.25f + 0.5f * tw};
            DrawQuad(xf, col, 0.0f);
        }

        // Hero quad: the clear pulse indicator (M2's input-lit feedback).
        const float hero_xf[4] = {0.0f, 0.0f, 0.16f + 0.10f * pulse,
                                  0.16f + 0.10f * pulse};
        const float hero_col[4] = {0.10f + 0.20f * pulse,
                                   0.45f + 0.35f * pulse, 0.85f, 0.95f};
        DrawQuad(hero_xf, hero_col, 0.0f);

        // Volumetric slabs (uv-patterned; count from the vol domain).
        if (sp.vol_slabs > 0) {
            cmdlist->SetPipelineState(pso_slab.Get());
            for (UINT s = 0; s < sp.vol_slabs; ++s) {
                const float vol_xf[4] = {0, 0, 1, 1};
                const float vol_col[4] = {0.35f + 0.1f * pulse, 0.55f, 0.85f,
                                          0.10f};
                DrawQuad(vol_xf, vol_col,
                         float(sim_time) * 0.4f + float(s) * 0.7f);
            }
        }

        // ---- upscale composite to the backbuffer ----------------------------
        cmdlist->SetPipelineState(pso_upscale.Get());
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += frame_index * rtv_size;
        cmdlist->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        D3D12_VIEWPORT fvp{0, 0, float(width), float(height), 0.f, 1.f};
        D3D12_RECT fsc{0, 0, LONG(width), LONG(height)};
        cmdlist->RSSetViewports(1, &fvp);
        cmdlist->RSSetScissorRects(1, &fsc);
        cmdlist->SetGraphicsRootDescriptorTable(
            1, srv_heap->GetGPUDescriptorHandleForHeapStart());
        const float up_xf[4] = {0, 0, 1, 1};
        const float up_col[4] = {1, 1, 1, 1};
        DrawQuad(up_xf, up_col, 0.0f);

        // GPU frame-cost bracket (§10): END after all scene work, then
        // resolve the pair into the readback buffer (mapped next frame).
        if (ts_heap) {
            cmdlist->EndQuery(ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
            cmdlist->ResolveQueryData(ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0,
                                      2, ts_readback, 0);
        }

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cmdlist->ResourceBarrier(1, &barrier);
        cmdlist->Close();

        ID3D12CommandList* cl = cmdlist.Get();
        queue->ExecuteCommandLists(1, &cl);
        // M1-F: never pretend to render on a removed device. Present is the
        // first checked submit-side call; any removal here aborts the loop
        // with structured evidence instead of silent fakes.
        const HRESULT present_hr = swapchain->Present(1, 0);
        if (present_hr == DXGI_ERROR_DEVICE_REMOVED ||
            present_hr == DXGI_ERROR_DEVICE_HUNG ||
            present_hr == DXGI_ERROR_DEVICE_RESET ||
            present_hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR) {
            device_dead = true;
            return;
        }

        fence_value++;
        queue->Signal(fence.Get(), fence_value);
        if (fence->GetCompletedValue() < fence_value) {
            fence->SetEventOnCompletion(fence_value, fence_event);
            // Finite wait: a hung device must not wedge the title forever.
            if (WaitForSingleObject(fence_event, 5000) != WAIT_OBJECT_0) {
                device_dead = true;
                return;
            }
        }
    }

    ~D3D12Title() {
        if (fence_event) CloseHandle(fence_event);
    }

private:
    static constexpr UINT kFrameCount = 2;
    ComPtr<ID3D12Resource> backbufs[kFrameCount];
    ComPtr<ID3D12DescriptorHeap> rtv_heap, srv_heap, sampler_heap;
    UINT rtv_size = 0;
    UINT frame_index = 0;
    UINT64 fence_value = 0;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event = nullptr;

    ComPtr<ID3D12RootSignature> root_sig;
    ComPtr<ID3D12PipelineState> pso_solid, pso_slab, pso_upscale;
    ComPtr<ID3D12Resource> scene_target_;
    UINT scene_w_ = 0, scene_h_ = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE offscreen_rtv_{};

    // Deterministic instance placement (golden-angle ring layout; motion is
    // a pure function of sim_time + pulse — identical every run, §15/§32).
    static void InstancePos(UINT i, double t, float pulse, float* px,
                            float* py, float* sz) {
        const float a = float(i) * 2.399963f +
                        float(t) * (0.15f + 0.05f * float(i % 4));
        const float rad = 0.16f + 0.30f * float(i % 7) / 7.0f;
        *px = std::cos(a) * rad;
        *py = std::sin(a) * rad * 0.75f;
        *sz = 0.05f + 0.04f * pulse + 0.02f * float(i % 5) / 5.0f;
    }
    static void InstanceColor(UINT i, float pulse, float* r, float* g,
                              float* b) {
        const float m = float(i % 5) / 5.0f;
        *r = 0.25f + 0.55f * m * pulse;
        *g = 0.45f + 0.35f * (1.0f - m);
        *b = 0.55f + 0.40f * pulse * (1.0f - m);
    }

    void DrawQuad(const float xf[4], const float col[4], float phase) {
        DrawUniforms u;
        u.xform[0] = xf[0]; u.xform[1] = xf[1];
        u.xform[2] = xf[2]; u.xform[3] = xf[3];
        u.col[0] = col[0]; u.col[1] = col[1];
        u.col[2] = col[2]; u.col[3] = col[3];
        u.misc[0] = phase;
        cmdlist->SetGraphicsRoot32BitConstants(0, 12, &u, 0);
        cmdlist->DrawInstanced(6, 1, 0, 0);
    }
};

} // namespace

// ============================================================================
// WinMain
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // ---- typed launch contract (M2, unchanged) ------------------------------
    bool ctx_received = false;
    dc::TitleLaunchContext ctx;
    {
        const char* env = std::getenv("DC_TITLE_CONTEXT");
        if (env && *env) {
            std::string err;
            dc::TitleLaunchContext parsed;
            if (dc::ParseTitleContext(env, parsed, err)) {
                ctx = parsed;
                ctx_received = true;
            } else {
                std::fprintf(stderr, "fidelitylab: rejecting launch context: %s\n",
                             err.c_str());
            }
        }
    }
    if (ctx_received) {
        std::printf("fidelitylab: session=%s title=%s v%s\n",
                    ctx.session_id.c_str(), ctx.title_id.c_str(),
                    ctx.title_version.c_str());
        std::printf("fidelitylab: host evidence %zu bytes, DCX claims %zu\n",
                    ctx.host_capability_json.size(),
                    ctx.active_session_profiles.size());
        if (!ctx.guide_owned_by_platform) {
            std::fprintf(stderr,
                         "fidelitylab: refusing context without Guide ownership\n");
            return 1;
        }
    }

    // ---- benchmark mode + developer pin (§32/§35, env = dev transport) ------
    const char* mode_env = std::getenv("DC_FIDELITY_MODE");
    const std::string mode = mode_env ? mode_env : "baseline";
    const char* pin_env = std::getenv("DC_FIDELITY_PIN");
    const std::string pin = pin_env ? pin_env : "";
    const bool stress = std::getenv("DC_FIDELITY_STRESS") != nullptr;

    // ---- platform fidelity service (§12/§13) --------------------------------
    // Gen view = the launch working directory (M3: the package IS the install
    // root). The service loads ONLY the package view (§28) and fails closed
    // with a structured reason (§37) — the title then runs its lowest fixed
    // state and SAYS SO. It never silently self-selects a preset.
    dc::fidelity::ServiceConfig cfg;
    cfg.gen_view_dir = ".";
    cfg.host_capability_json = ctx.host_capability_json;   // §31 real evidence
    cfg.intent = ctx.fidelity_intent;                      // §16 semantic
    cfg.session_profile = ctx.active_session_profiles.empty()
                              ? std::string()
                              : ctx.active_session_profiles.front();

    std::string sreason, sdetail;
    std::unique_ptr<dc::fidelity::FidelityService> fidelity =
        dc::fidelity::FidelityService::Create(cfg, &sreason, &sdetail);
    TitleState st;   // fallback OFF states
    st.res = "res_50"; st.geo = "geo_0"; st.shadow = "shadow_0";
    st.refl = "refl_0"; st.vol = "vol_0"; st.part = "part_0"; st.sim = "sim_0";
    bool applied_by_governor = false;
    if (fidelity && fidelity->SelectInitial()) {
        const dc::fidelity::Selection& sel = fidelity->CurrentSelection();
        ApplyStates(fidelity.get(), &st);
        applied_by_governor = true;
        std::printf("fidelity: selected %s (reason=%s intent=%s)\n",
                    sel.candidate_id.c_str(),
                    dc::fidelity::SelectionReasonName(sel.reason),
                    dc::fidelity::IntentName(sel.intent));
        std::printf("fidelity: states res=%s geo=%s shadow=%s refl=%s vol=%s "
                    "part=%s sim=%s\n", st.res.c_str(), st.geo.c_str(),
                    st.shadow.c_str(), st.refl.c_str(), st.vol.c_str(),
                    st.part.c_str(), st.sim.c_str());
    } else {
        std::printf("fidelity: service unavailable (%s: %s) — title runs "
                    "lowest fixed states (fallback, not governor)\n",
                    sreason.c_str(), sdetail.c_str());
    }
    std::fflush(stdout);

    // Dev pin AFTER initial selection (§35: diagnostics/capture only).
    if (fidelity && !pin.empty() && !fidelity->PinCandidate(pin)) {
        std::fprintf(stderr, "fidelity: PIN refused: '%s' not admissible\n",
                     pin.c_str());
        return 2;   // capture scripts must not mistake a refused pin for success
    }
    const bool pinned_now = fidelity && fidelity->CurrentSelection().candidate_id
                                               == pin && !pin.empty();

    // STRESS (§40 harness): hold the fixed heavy composition. This is a
    // test-harness control, not a fidelity selection — the governor still
    // receives measured telemetry from these frames.
    if (stress) {
        st.res = "res_100"; st.geo = "geo_2"; st.shadow = "shadow_2";
        st.refl = "refl_2"; st.vol = "vol_1"; st.part = "part_1";
        st.sim = "sim_1";
        st.valid = true;
    }

    // ---- semantic console input (platform-owned router, M2) -----------------
    dc::IInputRouter* input = dc::CreateGameInputRouter();

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DC_FidelityLabTitle";
    wc.hCursor = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);

    char title_text[180];
    std::snprintf(title_text, sizeof(title_text),
                  "Digital Console - FidelityLab (%s%s)",
                  mode.c_str(), stress ? " +stress" : "");
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, title_text,
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        if (input) dc::DestroyGameInputRouter(input);
        return 2;
    }
    ShowWindow(hwnd, SW_SHOW);

    // ---- D3D12 init ----------------------------------------------------------
    D3D12Title title;
    if (!title.Init(hwnd)) {
        std::fprintf(stderr, "fidelitylab: D3D12 init failed\n");
        if (input) dc::DestroyGameInputRouter(input);
        return 3;
    }

    ComPtr<ID3DBlob> vsb, ps_solid, ps_slab, ps_up;
    if (FAILED(D3DCompile(kShaderHlsl, strlen(kShaderHlsl), nullptr, nullptr,
                          nullptr, "VSMain", "vs_5_0", 0, 0, &vsb, nullptr)) ||
        FAILED(D3DCompile(kShaderHlsl, strlen(kShaderHlsl), nullptr, nullptr,
                          nullptr, "PSSolid", "ps_5_0", 0, 0, &ps_solid,
                          nullptr)) ||
        FAILED(D3DCompile(kShaderHlsl, strlen(kShaderHlsl), nullptr, nullptr,
                          nullptr, "PSSlab", "ps_5_0", 0, 0, &ps_slab,
                          nullptr)) ||
        FAILED(D3DCompile(kShaderHlsl, strlen(kShaderHlsl), nullptr, nullptr,
                          nullptr, "PSUpscale", "ps_5_0", 0, 0, &ps_up,
                          nullptr))) {
        std::fprintf(stderr, "fidelitylab: shader compile failed\n");
        if (input) dc::DestroyGameInputRouter(input);
        return 3;
    }
    if (!title.InitGraphics(vsb->GetBufferPointer(), vsb->GetBufferSize(),
                            ps_solid->GetBufferPointer(),
                            ps_solid->GetBufferSize(),
                            ps_slab->GetBufferPointer(),
                            ps_slab->GetBufferSize(),
                            ps_up->GetBufferPointer(), ps_up->GetBufferSize())) {
        std::fprintf(stderr, "fidelitylab: graphics pipeline init failed\n");
        if (input) dc::DestroyGameInputRouter(input);
        return 3;
    }

    // Deterministic simulation entities (§15): fixed seeds, fixed ordering.
    std::vector<SimEntity> sims;
    {
        const UINT n = SimEntities(st.sim);
        sims.reserve(n);
        uint32_t seed = 0x9E3779B9u;
        for (UINT i = 0; i < n; ++i) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            SimEntity e;
            e.phase0 = (seed % 3600) / 572.95779f;
            e.speed = 0.55f + (seed % 817) / 1000.0f;
            e.r = (seed % 631) / 1000.0f;
            e.g = ((seed >> 7) % 631) / 1000.0f;
            e.b = ((seed >> 14) % 631) / 1000.0f;
            e.dist = 0.24f + (seed % 387) / 1400.0f;
            sims.push_back(e);
        }
    }

    std::printf("fidelitylab: mode=%s sim_entities=%u ready\n", mode.c_str(),
                static_cast<unsigned>(sims.size()));
    std::fflush(stdout);

    // ---- GPU timestamp resources (§10) ---------------------------------------
    UINT64 ts_freq = 0;
    title.Queue()->GetTimestampFrequency(&ts_freq);
    ComPtr<ID3D12QueryHeap> ts_heap;
    D3D12_QUERY_HEAP_DESC qhd{};
    qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count = 2;
    const HRESULT ts_heap_hr =
        title.Device()->CreateQueryHeap(&qhd, IID_PPV_ARGS(&ts_heap));
    ComPtr<ID3D12Resource> ts_readback;
    {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = 2 * sizeof(UINT64);
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT ts_rb_hr = title.Device()->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&ts_readback));
        std::printf("fidelitylab: ts_readback hr=0x%08lX ptr=%d heap=%d\n",
                    (unsigned long)ts_rb_hr, ts_readback ? 1 : 0,
                    ts_heap ? 1 : 0);
        std::fflush(stdout);
        // M1-F discipline: the removal reason is the authoritative record.
        const HRESULT removed_hr = title.Device()->GetDeviceRemovedReason();
        std::printf("fidelitylab: device-removed-reason=0x%08lX "
                    "ts_heap_hr=0x%08lX\n",
                    (unsigned long)removed_hr, (unsigned long)ts_heap_hr);
        std::fflush(stdout);
    }

    // ---- main loop -----------------------------------------------------------
    LARGE_INTEGER qpf{}, t_prev{}, t_now{}, win_start{};
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&t_prev);
    QueryPerformanceCounter(&win_start);
    double sim_time = 0.0;       // §15: accumulated REAL time
    double gpu_avg_ms = 0.0;     // EMA over timestamp pairs
    UINT gpu_samples = 0;
    double frame_cpu_ms = 0.0;
    uint64_t window_index = 0;
    size_t printed_trace = 0;   // events already emitted (§25 live trace)
    UINT probe_frames = 0;      // timestamp probe budget (first frames only)

    float pulse = 0.0f;
    MSG msg{};
    while (true) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                // Evidence footer (§25/§41): selection, decision trace,
                // measured cost.
                std::printf("fidelity-final: applied_by_governor=%d mode=%s\n",
                            applied_by_governor ? 1 : 0, mode.c_str());
                if (fidelity) {
                    const dc::fidelity::Selection& sel =
                        fidelity->CurrentSelection();
                    std::printf("fidelity-final: candidate=%s\n",
                                sel.candidate_id.c_str());
                    const auto& trace = fidelity->Trace();
                    for (size_t i = printed_trace; i < trace.size(); ++i) {
                        const auto& ev = trace[i];
                        std::printf("fidelity-trace: w%llu %s -> %s (%s) %s\n",
                                    (unsigned long long)ev.window_index,
                                    ev.previous_candidate.c_str(),
                                    ev.new_candidate.c_str(),
                                    dc::fidelity::SelectionReasonName(ev.reason),
                                    ev.detail.c_str());
                    }
                    printed_trace = trace.size();
                }
                std::printf("fidelity-final: gpu_ms=%.3f cpu_ms=%.3f "
                            "windows=%llu\n", gpu_avg_ms, frame_cpu_ms,
                            (unsigned long long)window_index);
                std::fflush(stdout);
                if (input) dc::DestroyGameInputRouter(input);
                return kExitClean;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        QueryPerformanceCounter(&t_now);
        frame_cpu_ms = 1000.0 * double(t_now.QuadPart - t_prev.QuadPart) /
                       double(qpf.QuadPart);
        t_prev = t_now;
        sim_time += frame_cpu_ms / 1000.0;

        if (input) {
            input->Poll(dc::ConsoleAction::None);
            const dc::GamepadState gp = input->StateFor(0);
            pulse = 0.5f + 0.5f * gp.left_y;
            if (gp.buttons & (1u << static_cast<uint32_t>(dc::GamepadButton::South)))
                pulse = 1.0f;
        } else {
            pulse = 0.5f + 0.5f * std::sin(float(sim_time) * kPulseRate);
        }

        if (g_life == LifeState::Running) {
            // Governor adaptations take effect on the next rendered frame
            // (§14: real application, not metadata). STRESS holds its fixed
            // composition (§40 harness control). Pin freezes the selection.
            if (fidelity && !stress && !pinned_now) {
                TitleState now;
                ApplyStates(fidelity.get(), &now);
                // sim density is RESTART_REQUIRED (§23): the governor cannot
                // legally change it at runtime — refuse and keep the launch
                // density; the refusal is visible evidence.
                if (!now.sim.empty() && now.sim != st.sim)
                    std::printf("fidelity: sim density change refused "
                                "(RESTART_REQUIRED, launch=%s)\n", st.sim.c_str());
                now.sim = st.sim;
                st = now;
            }

            const SceneParams sp = SceneFor(st.geo, st.shadow, st.refl,
                                            st.vol, st.part);
            const float res_frac = stress ? 1.0f : ResFraction(st.res);
            title.RenderScene(pulse, res_frac, sp, sims.data(),
                              (UINT)sims.size(), sim_time, ts_heap.Get(),
                              ts_readback.Get());
            if (title.device_dead) {
                // M1-F: structured failure, not a silent fake. The removal
                // reason and measurement context become the evidence record;
                // the loop ends the same way a WM_QUIT would.
                const HRESULT removed_hr = title.Device()->GetDeviceRemovedReason();
                std::printf("fidelity-final: device_dead=1 removed=0x%08lX "
                            "gpu_ms=%.3f windows=%llu\n",
                            (unsigned long)removed_hr, gpu_avg_ms,
                            (unsigned long long)window_index);
                std::fflush(stdout);
                if (fidelity) {
                    std::printf("fidelity-final: candidate=%s\n",
                                fidelity->CurrentSelection().candidate_id.c_str());
                }
                std::printf("fidelitylab: DEVICE_REMOVED exiting\n");
                std::fflush(stdout);
                if (input) dc::DestroyGameInputRouter(input);
                return 3;   // dedicated removal exit code
            }

            // Previous frame's timestamp pair → GPU ms (§10: measured).
            if (ts_readback && ts_freq) {
                void* mapped = nullptr;
                D3D12_RANGE rr{0, 2 * sizeof(UINT64)};
                HRESULT map_hr = ts_readback->Map(0, &rr, &mapped);
                if (SUCCEEDED(map_hr)) {
                    UINT64 ts[2] = {0, 0};
                    memcpy(ts, mapped, sizeof(ts));
                    ts_readback->Unmap(0, nullptr);
                    if (ts[1] > ts[0]) {
                        const double gms = GpuPeriodMs(ts[1] - ts[0], ts_freq);
                        gpu_avg_ms = gpu_samples == 0
                                         ? gms
                                         : gpu_avg_ms + 0.2 * (gms - gpu_avg_ms);
                        ++gpu_samples;
                    } else if (probe_frames < 6) {
                        std::printf("fidelitylab: ts probe f=%u ts=[%llu %llu]\n",
                                    probe_frames, (unsigned long long)ts[0],
                                    (unsigned long long)ts[1]);
                        std::fflush(stdout);
                    }
                } else if (probe_frames < 6) {
                    std::printf("fidelitylab: ts Map failed hr=0x%08lX f=%u\n",
                                (unsigned long)map_hr, probe_frames);
                    std::fflush(stdout);
                }
                ++probe_frames;
            } else if (probe_frames == 0) {
                std::printf("fidelitylab: ts unavailable readback=%d freq=%llu\n",
                            ts_readback ? 1 : 0, (unsigned long long)ts_freq);
                std::fflush(stdout);
            }

            // One telemetry window ≈ 1 s (§22 observation window).
            const double win_s = double(t_now.QuadPart - win_start.QuadPart) /
                                 double(qpf.QuadPart);
            if (win_s >= 1.0 && fidelity) {
                dc::fidelity::TelemetryWindow tw;
                tw.gpu_ms = gpu_samples > 0
                                ? dc::fidelity::Metric::Known(gpu_avg_ms)
                                : dc::fidelity::Metric::Unknown();
                tw.cpu_ms = dc::fidelity::Metric::Known(frame_cpu_ms);
                // VRAM residency and streaming are not instrumented in M4 —
                // UNKNOWN stays unknown (§10/§17: never fabricated).
                tw.vram_mb = dc::fidelity::Metric::Unknown();
                tw.io_mbps = dc::fidelity::Metric::Unknown();
                // 1 Hz metrics heartbeat (§35 developer diagnostic): proves
                // real measurement is flowing without spamming per-frame JSON.
                std::printf("fidelity-metrics: w%llu gpu_ms=%.3f cpu_ms=%.3f\n",
                            (unsigned long long)window_index,
                            gpu_samples > 0 ? gpu_avg_ms : -1.0, frame_cpu_ms);
                std::fflush(stdout);
                fidelity->Observe(tw);
                // §25: decision events are low-frequency trace — print at
                // emission, not only in the exit footer, so evidence capture
                // survives abnormal termination. Trace() only ever appends,
                // so printing everything past the last-printed index is
                // exactly the unprinted suffix.
                const auto& trace = fidelity->Trace();
                for (size_t i = printed_trace; i < trace.size(); ++i) {
                    const auto& ev = trace[i];
                    std::printf("fidelity-trace: w%llu %s -> %s (%s) %s\n",
                                (unsigned long long)ev.window_index,
                                ev.previous_candidate.c_str(),
                                ev.new_candidate.c_str(),
                                dc::fidelity::SelectionReasonName(ev.reason),
                                ev.detail.c_str());
                    std::fflush(stdout);
                }
                printed_trace = trace.size();
                ++window_index;
                QueryPerformanceCounter(&win_start);
            }
        }
        Sleep(16);   // ~60 Hz cadence for the sample; not the platform loop
    }
}
