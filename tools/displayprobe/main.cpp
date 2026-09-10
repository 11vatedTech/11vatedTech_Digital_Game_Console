// main.cpp — dc-displayprobe (DK0-M1I): active presentation-path qualification.
// Proves (or honestly fails to prove) the VRR-compatible presentation path:
//   state 1: capable           — factory supports tearing (discovery, from hostprof)
//   state 2: path_compatible   — flip-model + ALLOW_TEARING swap chain created
//   state 3: actively_proven   — Present(0, ALLOW_TEARING) accepted; frame
//             pacing measured via present-interval statistics
// Never changes display modes; windowed-borderless only; nothing to roll back.
//
// Adapter policy (DevKit-0 2026-09-10): the session's shell and native titles
// create device+swapchain from ONE factory on the DEFAULT adapter. The probe
// reproduces that exact canonical pattern per candidate (max-VRAM dGPU first,
// then default), each with its own factory — a shared factory across
// device/swapchain contexts fails DXGI_ERROR_ACCESS_LOST on the preview
// driver. The verdict reflects the first proven path; per-adapter rejection
// evidence is always recorded (C10).
#include "dc/capability.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <dxgi1_6.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "user32.lib")

namespace {

template <typename T> void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

struct ProbeResult {
    bool device_ok = false;
    bool factory_tearing = false;       // environment capability
    bool swapchain_created = false;     // flip-model path constructible
    bool tearing_swapchain = false;     // created WITH ALLOW_TEARING flag
    bool present_tearing_ok = false;    // Present(0, ALLOW_TEARING) accepted
    bool present_vsync_ok = false;      // Present(1) accepted
    bool stats_available = false;       // presentation stats observable
    double avg_present_ms = 0.0;        // mean present interval over probe
    double min_refresh_hz = 0.0;        // observed frame interval range
    double max_refresh_hz = 0.0;
    std::string proven_adapter;         // which adapter's path passed ("max-vram" | "default")
    std::string error;
    std::string variant;                       // first swapchain desc variant accepted
    std::vector<std::string> variant_attempts; // per-variant rejection diagnostics
};

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

std::string HrHex(HRESULT hr) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08lX", (unsigned long)hr);
    return b;
}

// One self-contained presentation attempt: ONE factory is created and used for
// adapter enumeration, tearing check, device creation AND swapchain creation —
// the same single-factory pattern the session shell uses successfully.
// Returns: 0 = proven, 1 = attempt failed (desc/context), 3 = adapter-level
// failure (ACCESS_LOST/REMOVED — a different adapter is the next remedy).
int AttemptPresentation(bool use_max_vram, const char* adapter_tag,
                        ProbeResult& r, std::string* fail_reason) {
    *fail_reason = "unknown";
    int failure_kind = 2;

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"dc-displayprobe";
    // Re-registration across attempts is expected; tolerate ERROR_CLASS_ALREADY_EXISTS.
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        *fail_reason = "RegisterClass failed"; return 1;
    }

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"dc-displayprobe",
                                WS_OVERLAPPEDWINDOW, 40, 40, 960, 540,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { *fail_reason = "CreateWindow failed"; return 1; }
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    MSG msg;

    HMODULE d3d = LoadLibraryW(L"d3d12.dll");
    if (!d3d) { *fail_reason = "d3d12.dll missing"; DestroyWindow(hwnd); return 1; }
    using PFN = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto create = reinterpret_cast<PFN>(
        reinterpret_cast<void*>(GetProcAddress(d3d, "D3D12CreateDevice")));
    if (!create) { *fail_reason = "D3D12CreateDevice missing"; DestroyWindow(hwnd); return 1; }

    // ---- ONE factory for everything (canonical pattern) ----
    IDXGIFactory5* factory5 = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory5))) || !factory5) {
        *fail_reason = "CreateDXGIFactory1 failed";
        DestroyWindow(hwnd);
        return 1;
    }

    ID3D12Device* device = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    if (use_max_vram) {
        UINT64 best = 0;
        for (UINT i = 0;; ++i) {
            IDXGIAdapter1* a = nullptr;
            if (FAILED(factory5->EnumAdapters1(i, &a)) || !a) break;
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(a->GetDesc1(&d)) &&
                (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                d.DedicatedVideoMemory > best) {
                best = d.DedicatedVideoMemory;
                Rel(adapter);
                adapter = a;
                continue;
            }
            a->Release();
        }
        if (!adapter) {
            *fail_reason = "no dedicated adapter";
            Rel(factory5); DestroyWindow(hwnd);
            return 1;
        }
    }
    HRESULT hr = create(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    Rel(adapter); // device owns its reference now
    if (FAILED(hr) || !device) {
        *fail_reason = "device creation failed hr=" + HrHex(hr);
        Rel(factory5); DestroyWindow(hwnd);
        return 1;
    }
    r.device_ok = true;

    // D3D12 requires the COMMAND QUEUE (not the device) as the swapchain's
    // pDevice argument — passing ID3D12Device* fails with DXGI_ERROR_INVALID_CALL
    // (0x887A0001) on every variant. This was the original probe defect that
    // masqueraded as a driver blocker (DevKit-0, 2026-09-10 bisect).
    ID3D12CommandQueue* queue = nullptr;
    {
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) || !queue) {
            *fail_reason = "command queue creation failed";
            Rel(device); Rel(factory5); DestroyWindow(hwnd);
            return 1;
        }
    }

    // Flip-model swap chain with ALLOW_TEARING; every rejection is evidence.
    IDXGISwapChain1* sc1 = nullptr;
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = 960; sd.Height = 540;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SampleDesc.Count = 1;
    struct Variant { UINT buffers; DXGI_SWAP_EFFECT effect; UINT flags; const char* name; };
    const Variant variants[] = {
        { 3, DXGI_SWAP_EFFECT_FLIP_DISCARD,   DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING, "flip-discard:3:tearing" },
        { 3, DXGI_SWAP_EFFECT_FLIP_DISCARD,   0, "flip-discard:3" },
        { 2, DXGI_SWAP_EFFECT_FLIP_DISCARD,   0, "flip-discard:2" },
        { 2, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, 0, "flip-sequential:2" },
    };
    bool composition_fallback = false;
    {
        IDXGIFactory2* f2 = nullptr;
        if (FAILED(factory5->QueryInterface(IID_PPV_ARGS(&f2))) || !f2) {
            *fail_reason = "IDXGIFactory2 unavailable";
            Rel(device); Rel(factory5); DestroyWindow(hwnd);
            return 1;
        }
        for (const auto& v : variants) {
            sd.BufferCount = v.buffers;
            sd.SwapEffect = v.effect;
            sd.Flags = v.flags;
            HRESULT last_hr = f2->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &sc1);
            sc1 = SUCCEEDED(last_hr) ? sc1 : nullptr;
            if (SUCCEEDED(last_hr) && sc1) {
                r.swapchain_created = true;
                r.tearing_swapchain = (v.flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "created %s variant %s", adapter_tag, v.name);
                r.variant = buf;
                break;
            }
            if (last_hr == DXGI_ERROR_ACCESS_LOST || last_hr == DXGI_ERROR_DEVICE_REMOVED ||
                last_hr == DXGI_ERROR_DEVICE_RESET) {
                failure_kind = 3; // adapter-level: more variants won't help
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s %s rejected hr=%s (adapter-level)",
                              adapter_tag, v.name, HrHex(last_hr).c_str());
                r.variant_attempts.push_back(buf);
                break;
            }
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s %s rejected hr=%s",
                          adapter_tag, v.name, HrHex(last_hr).c_str());
            r.variant_attempts.push_back(buf);
        }
        f2->Release();
        if (!sc1 && failure_kind != 3) {
            // HWND path rejected desc-independently: try composition (no HWND).
            IDXGIFactory2* fc = nullptr;
            if (SUCCEEDED(factory5->QueryInterface(IID_PPV_ARGS(&fc))) && fc) {
                sd.BufferCount = 3;
                sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
                HRESULT hc = fc->CreateSwapChainForComposition(queue, &sd, nullptr, &sc1);
                fc->Release();
                if (SUCCEEDED(hc) && sc1) {
                    composition_fallback = true;
                    r.swapchain_created = true;
                    r.tearing_swapchain = true;
                    r.variant = std::string("composition:") + adapter_tag + ":3:tearing";
                } else {
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "%s composition rejected hr=%s",
                                  adapter_tag, HrHex(hc).c_str());
                    r.variant_attempts.push_back(buf);
                    if (hc == DXGI_ERROR_ACCESS_LOST || hc == DXGI_ERROR_DEVICE_REMOVED) {
                        failure_kind = 3;
                    }
                }
            }
        }
        if (!sc1) {
            *fail_reason = "swapchain creation failed";
            Rel(queue); Rel(device); Rel(factory5); DestroyWindow(hwnd);
            return failure_kind;
        }
        if (composition_fallback) {
            r.error = "hwnd swapchain rejected desc-independently (see attempts); composition path used";
        }
    }

    // Present loop: 30 tearing presents + 30 vsync presents, timing each.
    const int kFrames = 30;
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    double tear_ms_total = 0.0;
    double vsync_ms_total = 0.0;
    double min_iv = 1e9, max_iv = 0.0;
    int tear_ok = 0, vsync_ok = 0;
    bool device_lost_mid = false;

    for (int i = 0; i < kFrames * 2; ++i) {
        bool tearing_frame = i < kFrames;
        UINT sync = tearing_frame ? 0 : 1;
        UINT flags = (tearing_frame && r.tearing_swapchain) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        LARGE_INTEGER t0{}, t1{};
        QueryPerformanceCounter(&t0);
        HRESULT hrr = sc1->Present(sync, flags);
        QueryPerformanceCounter(&t1);
        double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
        if (SUCCEEDED(hrr)) {
            if (tearing_frame) { ++tear_ok; tear_ms_total += ms; }
            else { ++vsync_ok; vsync_ms_total += ms; }
            if (i > kFrames) {
                if (ms < min_iv) min_iv = ms;
                if (ms > max_iv) max_iv = ms;
            }
        } else {
            if (hrr == DXGI_ERROR_ACCESS_LOST || hrr == DXGI_ERROR_DEVICE_REMOVED ||
                hrr == DXGI_ERROR_DEVICE_RESET) {
                device_lost_mid = true;
            }
            if (tearing_frame && hrr == DXGI_ERROR_INVALID_CALL) {
                r.error = "tearing present rejected (DXGI_ERROR_INVALID_CALL)";
            }
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) i = kFrames * 2;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(8);
    }
    if (tear_ok > 0 && r.tearing_swapchain) r.present_tearing_ok = true;
    if (vsync_ok > 0) r.present_vsync_ok = true;
    if (tear_ok > 0) r.avg_present_ms = tear_ms_total / tear_ok;
    if (max_iv > 0.0 && min_iv < 1e9) {
        r.stats_available = true;
        r.min_refresh_hz = 1000.0 / max_iv;
        r.max_refresh_hz = 1000.0 / (min_iv > 0.0 ? min_iv : 1.0);
    }

    bool proven = r.present_tearing_ok && r.present_vsync_ok;
    r.proven_adapter = proven ? adapter_tag : "";

    Rel(sc1);
    Rel(queue);
    Rel(device);
    Rel(factory5);
    DestroyWindow(hwnd);

    if (device_lost_mid && !proven) {
        *fail_reason = "adapter-level failure mid-present";
        return 3;
    }
    *fail_reason = proven ? "" : "present loop failed";
    return proven ? 0 : 1;
}

void PrintReport(const ProbeResult& r) {
    std::printf("==============================================================\n");
    std::printf(" dc-displayprobe — presentation-path qualification (DK0-M1I)\n");
    std::printf("==============================================================\n");
    std::printf(" d3d12 device            : %s\n", r.device_ok ? "ok" : "failed");
    std::printf(" [1] VRR environment     : factory tearing support = %s\n", r.factory_tearing ? "yes" : "no");
    std::printf(" [2] flip-model path     : swap chain created = %s (ALLOW_TEARING flag = %s)\n",
                r.swapchain_created ? "yes" : "no", r.tearing_swapchain ? "yes" : "no");
    std::printf(" [3] active proof        : Present(0, ALLOW_TEARING) = %s, Present(1) = %s\n",
                r.present_tearing_ok ? "accepted" : "not proven",
                r.present_vsync_ok ? "accepted" : "failed");
    std::printf(" proven adapter          : %s\n",
                r.proven_adapter.empty() ? "(none)" : r.proven_adapter.c_str());
    std::printf(" presentation timing     : stats=%s avg_present=%.3f ms, observed interval range %.1f-%.1f Hz\n",
                r.stats_available ? "available" : "unavailable",
                r.avg_present_ms, r.min_refresh_hz, r.max_refresh_hz);
    if (!r.error.empty()) std::printf(" notes                   : %s\n", r.error.c_str());
    std::printf("--------------------------------------------------------------\n");
    if (r.factory_tearing && r.present_tearing_ok) {
        std::printf(" VERDICT: VRR-compatible presentation path ACTIVELY PROVEN on '%s'.\n",
                    r.proven_adapter.c_str());
        std::printf(" Per-display VRR range requires OS VRR metadata (pending) and\n");
        std::printf(" sustained monitoring — not claimed from this probe alone.\n");
    } else if (r.factory_tearing) {
        std::printf(" VERDICT: capable environment, path constructed, but active\n");
        std::printf(" proof failed. VRR must NOT be claimed (C10).\n");
    } else {
        std::printf(" VERDICT: environment does not expose tearing support.\n");
        std::printf(" VRR path_compatible = false, actively_proven = false.\n");
    }
    std::printf("==============================================================\n");
}

std::string ResultJson(const ProbeResult& r) {
    std::string s = "{\n";
    auto q = [](const std::string& v) { return "\"" + v + "\""; };
    auto b = [](bool v) { return v ? "true" : "false"; };
    s += "  \"schema\": \"dc.display-probe/1\",\n";
    s += "  \"device_ok\": " + std::string(b(r.device_ok)) + ",\n";
    s += "  \"vrr_capable_environment\": " + std::string(b(r.factory_tearing)) + ",\n";
    s += "  \"vrr_path_compatible\": " + std::string(b(r.swapchain_created && r.tearing_swapchain)) + ",\n";
    s += "  \"vrr_actively_proven\": " + std::string(b(r.present_tearing_ok)) + ",\n";
    s += "  \"present_vsync_ok\": " + std::string(b(r.present_vsync_ok)) + ",\n";
    s += "  \"stats_available\": " + std::string(b(r.stats_available)) + ",\n";
    s += "  \"proven_adapter\": " + q(r.proven_adapter) + ",\n";
    s += "  \"avg_present_ms\": " + std::to_string(r.avg_present_ms) + ",\n";
    s += "  \"observed_min_refresh_hz\": " + std::to_string(r.min_refresh_hz) + ",\n";
    s += "  \"observed_max_refresh_hz\": " + std::to_string(r.max_refresh_hz) + ",\n";
    s += "  \"variant\": " + q(r.variant) + ",\n";
    {
        std::string attempts;
        for (size_t i = 0; i < r.variant_attempts.size(); ++i) {
            attempts += q(r.variant_attempts[i]);
            if (i + 1 < r.variant_attempts.size()) attempts += ",";
        }
        s += "  \"variant_attempts\": [" + attempts + "],\n";
    }
    s += "  \"note\": " + q(r.error) + "\n";
    s += "}\n";
    return s;
}

} // namespace

int main(int argc, char** argv) {
    bool want_json = false;
    std::string out_path;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json")) want_json = true;
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out_path = argv[++i];
        else {
            std::printf("usage: dc-displayprobe [--json] [--out FILE]\n");
            return 2;
        }
    }

    ProbeResult r;

    // Environment-level tearing capability, checked once up front.
    {
        IDXGIFactory5* f5 = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f5))) && f5) {
            BOOL tearing = FALSE;
            if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                  &tearing, sizeof(tearing)))) {
                r.factory_tearing = tearing == TRUE;
            }
            Rel(f5);
        }
    }

    // Presentation candidates: max-VRAM dGPU first (preferred when healthy),
    // then the default adapter — the path the session shell uses. Each attempt
    // is self-contained (own factory). First proven path wins; every rejection
    // is recorded.
    int rc = 1;
    std::string fail;
    std::string fail_log;
    const struct { bool max_vram; const char* tag; } order[] = {
        { true,  "max-vram" },
        { false, "default" },
    };
    for (const auto& c : order) {
        int ar = AttemptPresentation(c.max_vram, c.tag, r, &fail);
        if (!fail.empty()) fail_log += std::string(c.tag) + ": " + fail + "; ";
        if (ar == 0) { rc = 0; break; }
        rc = ar; // 1 or 3 — either way, the next candidate is a valid remedy
    }
    if (rc != 0 && r.error.empty() && !fail_log.empty()) r.error = fail_log;

    if (!out_path.empty()) {
        FILE* f = std::fopen(out_path.c_str(), "wb");
        if (f) {
            std::string j = ResultJson(r);
            std::fwrite(j.data(), 1, j.size(), f);
            std::fclose(f);
            std::fprintf(stderr, "wrote %s\n", out_path.c_str());
        }
    }
    if (want_json) {
        std::fputs(ResultJson(r).c_str(), stdout);
    } else {
        PrintReport(r);
    }
    return rc;
}
