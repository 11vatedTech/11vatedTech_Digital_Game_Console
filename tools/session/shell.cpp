// dc-session --shell — the interactive controller-first console shell
// (DK0-M2 §31–§35). A real D3D12 flip-model surface owning the session:
// HOME / LIBRARY / SYSTEM / GUIDE views, deterministic focus-graph
// navigation (GameInput primary, keyboard as an explicit development
// fallback), live title launch/supervision with crash recovery, and full
// state-machine journaling.
//
// Presentation: the shell composes a fixed 1920×1080 BGRA frame (GDI text +
// software fills) that is uploaded and GPU-copied into a flip-model swapchain
// every frame. The window/present path is genuine D3D12; the 2D composition
// is deliberately replaceable (canon: isolate shell presentation from the
// runtime contracts so the renderer can be upgraded without touching them).
//
// Self-test: `dc-session --shell --selftest-frames N` drives the full boot →
// render → teardown path without user interaction (CI-friendly).
#include "../session/journal.hpp"
#include "../../runtime/core/dc/session.hpp"
#include "../../runtime/core/dc/title_registry.hpp"
#include "../../runtime/core/dc/title_context.hpp"
#include "../../runtime/core/dc/title_context_json.hpp"
#include "../../runtime/core/dc/capability.hpp"
#include "../../runtime/core/dc/json.hpp"
#include "../../runtime/core/dc/focus_graph.hpp"
#include "../../runtime/core/dc/input.hpp"
#include "../../runtime/host/dc/title_supervisor.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
using namespace dc;

namespace dc {
IInputRouter* CreateGameInputRouter();   // GameInput-backed (hosts/windows)
void DestroyGameInputRouter(IInputRouter*);
} // namespace dc

namespace dcwin {
// Live display re-enumeration (hosts/windows/probe_display.cpp) — used by the
// shell's §39 topology reaction. Deliberately not part of the core API.
void ProbeDisplays(std::vector<dc::DisplayDeviceInfo>& displays);
} // namespace dcwin

namespace {

// ---------------------------------------------------------------------------
// Fixed UI coordinate space; the flip-model swapchain scales to the window.
// 1920*4 = 7680 → row pitch satisfies D3D12's 256-byte buffer alignment.
// ---------------------------------------------------------------------------
constexpr UINT kW = 1920, kH = 1080;
constexpr LONG kSafeX = 96, kSafeY = 54;   // 5% safe area (§32)

// ---------------------------------------------------------------------------
// Frame composer: one top-down BGRA DIB is the frame. GDI draws text into
// it; software rects fill it; the whole frame is GPU-uploaded each present.
// ---------------------------------------------------------------------------
class Frame {
public:
    bool Init(HWND hwnd) {
        hwnd_ = hwnd;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = kW;
        bi.bmiHeader.biHeight = -(LONG)kH;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        hdc_ = CreateCompatibleDC(nullptr);
        void* bits = nullptr;
        bmp_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hdc_ || !bmp_) return false;
        bits_ = (uint32_t*)bits;
        old_ = (HGDIOBJ)SelectObject(hdc_, bmp_);
        SetBkMode(hdc_, TRANSPARENT);
        Clear(0xFF141820);
        return true;
    }
    ~Frame() {
        if (bmp_) { SelectObject(hdc_, old_); DeleteObject(bmp_); }
        if (hdc_) DeleteDC(hdc_);
    }

    uint32_t* bits() { return bits_; }

    void Clear(uint32_t argb) {
        for (UINT i = 0; i < kW * kH; ++i) bits_[i] = argb;
    }

    void FillRect(int x, int y, int w, int h, uint32_t argb) {
        int x0 = std::max(0, x), y0 = std::max(0, y);
        int x1 = std::min((int)kW, x + w), y1 = std::min((int)kH, y + h);
        for (int yy = y0; yy < y1; ++yy)
            for (int xx = x0; xx < x1; ++xx) bits_[yy * kW + xx] = argb;
    }

    SIZE TextSize(const std::string& s, int px) {
        HFONT f = Font(px);
        HFONT of = (HFONT)SelectObject(hdc_, f);
        RECT rc{0, 0, 0, 0};
        DrawTextA(hdc_, s.c_str(), -1, &rc, DT_CALCRECT | DT_NOPREFIX);
        SelectObject(hdc_, of);
        return {rc.right - rc.left, rc.bottom - rc.top};
    }

    void Text(int x, int y, const std::string& s, int px, uint32_t argb) {
        if (x >= (int)kW || y >= (int)kH) return;
        HFONT f = Font(px);
        HFONT of = (HFONT)SelectObject(hdc_, f);
        SetTextColor(hdc_, RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF));
        RECT rc{x, y, (LONG)kW, (LONG)kH};
        DrawTextA(hdc_, s.c_str(), -1, &rc, DT_LEFT | DT_NOPREFIX | DT_NOCLIP);
        SelectObject(hdc_, of);
    }

    // Convenience: text in safe-area coordinates.
    void SafeText(int x, int y, const std::string& s, int px, uint32_t argb) {
        Text(kSafeX + x, kSafeY + y, s, px, argb);
    }

private:
    HFONT Font(int px) {
        auto it = fonts_.find(px);
        if (it != fonts_.end()) return it->second;
        HFONT f = CreateFontW(-px, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        fonts_[px] = f;
        return f;
    }

    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HGDIOBJ old_ = nullptr;
    uint32_t* bits_ = nullptr;
    std::unordered_map<int, HFONT> fonts_;
};

// ---------------------------------------------------------------------------
// D3D12 shell surface: flip-model swapchain, per-frame GPU upload of the
// composed frame. Same proven fence pattern as the native sample title.
// ---------------------------------------------------------------------------
class Surface {
public:
    bool Init(HWND hwnd) {
        auto fail = [](const char* step, HRESULT hr) {
            std::fprintf(stderr, "[surface] %s failed: hr=0x%08lX\n", step,
                         (unsigned long)hr);
            return false;
        };
        HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                       IID_PPV_ARGS(&device_));
        if (FAILED(hr)) return fail("D3D12CreateDevice", hr);
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(hr = device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_))))
            return fail("CreateCommandQueue", hr);
        if (FAILED(hr = device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc_))))
            return fail("CreateCommandAllocator", hr);
        if (FAILED(hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   alloc_.Get(), nullptr,
                                                   IID_PPV_ARGS(&cmd_))))
            return fail("CreateCommandList", hr);
        cmd_->Close();

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = kW; sd.Height = kH;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGIFactory4> factory;
        if (FAILED(hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            return fail("CreateDXGIFactory1", hr);
        ComPtr<IDXGISwapChain1> sc1;
        if (FAILED(hr = factory->CreateSwapChainForHwnd(queue_.Get(), hwnd, &sd,
                                                        nullptr, nullptr, &sc1)))
            return fail("CreateSwapChainForHwnd", hr);
        if (FAILED(hr = sc1.As(&sc_))) return fail("swapchain cast", hr);

        D3D12_DESCRIPTOR_HEAP_DESC rhd{};
        rhd.NumDescriptors = 2;
        rhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(hr = device_->CreateDescriptorHeap(&rhd, IID_PPV_ARGS(&rtv_))))
            return fail("CreateDescriptorHeap", hr);
        UINT rtv_step = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT i = 0; i < 2; ++i) {
            if (FAILED(hr = sc_->GetBuffer(i, IID_PPV_ARGS(&bb_[i]))))
                return fail("GetBuffer", hr);
            D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_->GetCPUDescriptorHandleForHeapStart();
            h.ptr += i * rtv_step;
            device_->CreateRenderTargetView(bb_[i].Get(), nullptr, h);
        }

        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = (UINT64)kW * kH * 4;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(hr = device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&upload_))))
            return fail("CreateCommittedResource(upload)", hr);

        if (FAILED(hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(&fence_))))
            return fail("CreateFence", hr);
        fence_ev_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return fence_ev_ != nullptr;
    }

    ~Surface() { if (fence_ev_) CloseHandle(fence_ev_); }

    void Present(Frame& f) {
        void* data = nullptr;
        if (FAILED(upload_->Map(0, nullptr, &data))) return;
        memcpy(data, f.bits(), (size_t)kW * kH * 4);
        upload_->Unmap(0, nullptr);

        UINT idx = sc_->GetCurrentBackBufferIndex();
        alloc_->Reset();
        cmd_->Reset(alloc_.Get(), nullptr);

        D3D12_RESOURCE_BARRIER bar{};
        bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bar.Transition.pResource = bb_[idx].Get();
        bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        cmd_->ResourceBarrier(1, &bar);

        D3D12_TEXTURE_COPY_LOCATION dst{bb_[idx].Get(),
            D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {0}};
        D3D12_TEXTURE_COPY_LOCATION src{upload_.Get(),
            D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        src.PlacedFootprint.Offset = 0;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = kW;
        src.PlacedFootprint.Footprint.Height = kH;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = kW * 4;
        cmd_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        std::swap(bar.Transition.StateBefore, bar.Transition.StateAfter);
        cmd_->ResourceBarrier(1, &bar);
        cmd_->Close();

        ID3D12CommandList* cl = cmd_.Get();
        queue_->ExecuteCommandLists(1, &cl);
        sc_->Present(1, 0);

        ++fence_val_;
        queue_->Signal(fence_.Get(), fence_val_);
        if (fence_->GetCompletedValue() < fence_val_) {
            fence_->SetEventOnCompletion(fence_val_, fence_ev_);
            WaitForSingleObject(fence_ev_, INFINITE);
        }
    }

private:
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> alloc_;
    ComPtr<ID3D12GraphicsCommandList> cmd_;
    ComPtr<IDXGISwapChain3> sc_;
    ComPtr<ID3D12DescriptorHeap> rtv_;
    ComPtr<ID3D12Resource> bb_[2];
    ComPtr<ID3D12Resource> upload_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_ev_ = nullptr;
    UINT64 fence_val_ = 0;
};

// ---------------------------------------------------------------------------
// Host truth snapshot: the shell DISPLAYS capability from immutable evidence
// and never computes it (§40). Missing evidence renders as an honest absence.
// ---------------------------------------------------------------------------
struct HostView {
    bool loaded = false;
    std::string gpu, driver, cpu;
    std::string vram, ram;
    double raster = 0, compute = 0, rt = 0, cpu_gt = 0, cpu_sust = 0;
    std::string qual_mode = "none";
    std::string qual_time;
    bool qual_sustained_valid = false;
    std::vector<std::string> dcp, dcp_rej;
    std::vector<std::string> dcx, dcx_rej;
    struct Disp {
        std::string name; uint32_t w = 0, h = 0; double hz = 0;
        bool primary = false, active = false;
        std::string hdr_kind; bool hdr_active = false;
        std::string vrr_state;
    };
    std::vector<Disp> displays;
    std::vector<std::string> audio, storage, inputs;
    std::string build_hash, runtime_ver, build_cfg;
};

std::string GiB(double b) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.1f GB", b / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

std::string Score(double s) {
    if (s <= 0.0) return "unmeasured";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f", s);
    return buf;
}

HostView LoadHostView() {
    HostView hv;
    fs::path cap = "evidence/host-capability.json";
    if (const char* e = std::getenv("DC_HOSTCAP_PATH")) cap = e;
    std::ifstream f(cap, std::ios::binary);
    if (!f) return hv;
    std::string text((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    std::string err;
    auto v = json::Parse(text, err);
    if (!v) return hv;
    hv.loaded = true;

    auto str = [](const json::Value* o, const char* k) -> std::string {
        if (!o) return {};
        auto* x = o->find(k);
        return x ? x->as_string() : std::string();
    };
    auto num = [](const json::Value* o, const char* k) -> double {
        if (!o) return 0.0;
        auto* x = o->find(k);
        return x ? x->as_number() : 0.0;
    };

    if (auto* g = v->find("gpu")) {
        hv.gpu = str(g, "model_name");
        hv.driver = str(g, "driver_version");
        hv.vram = GiB(num(g, "vram_bytes"));
        hv.raster = num(g, "raster_score");
        hv.compute = num(g, "compute_score");
        hv.rt = num(g, "rt_score");
    }
    if (auto* c = v->find("cpu")) {
        hv.cpu = str(c, "model_name");
        hv.cpu_gt = num(c, "game_thread_score");
        hv.cpu_sust = num(c, "sustained_score");
    }
    if (auto* m = v->find("memory")) hv.ram = GiB(num(m, "system_bytes"));
    if (auto* q = v->find("qualification")) {
        hv.qual_mode = str(q, "mode");
        hv.qual_time = str(q, "timestamp_utc");
        hv.qual_sustained_valid = q->find("sustained_valid") &&
                                  q->find("sustained_valid")->as_bool();
    }
    auto ids = [&](const char* key, std::vector<std::string>& out,
                   std::vector<std::string>* rej = nullptr) {
        if (auto* a = v->find(key)) {
            for (const auto& e : a->as_array()) {
                std::string id = str(&e, "id");
                if (id.empty()) continue;
                if (rej) {
                    std::string reasons;
                    if (auto* rs = e.find("reasons")) {
                        for (const auto& r : rs->as_array())
                            reasons += (reasons.empty() ? "" : ",") + r.as_string();
                    }
                    rej->push_back(id + " (" + reasons + ")");
                } else {
                    out.push_back(id);
                }
            }
        }
    };
    ids("profiles_claimed", hv.dcp);
    ids("profiles_rejected", hv.dcp_rej, &hv.dcp_rej);
    ids("session_profiles_claimed", hv.dcx);
    ids("session_profiles_rejected", hv.dcx_rej, &hv.dcx_rej);
    if (auto* d = v->find("display")) {
        for (const auto& e : d->as_array()) {
            HostView::Disp dd;
            dd.name = str(&e, "name");
            dd.w = (uint32_t)num(&e, "width");
            dd.h = (uint32_t)num(&e, "height");
            dd.hz = num(&e, "desktop_refresh_hz");
            dd.primary = e.find("primary") && e.find("primary")->as_bool();
            dd.active = e.find("active") && e.find("active")->as_bool();
            if (auto* h = e.find("hdr")) {
                dd.hdr_kind = str(h, "advanced_color_kind");
                dd.hdr_active = h->find("advanced_color_active") &&
                                h->find("advanced_color_active")->as_bool();
            }
            if (auto* vr = e.find("vrr")) {
                // Three-state truth (C10): never collapse VRR to one boolean.
                bool sup = vr->find("supported") && vr->find("supported")->as_bool();
                bool pat = vr->find("path_compatible") &&
                           vr->find("path_compatible")->as_bool();
                bool prov = vr->find("actively_proven") &&
                            vr->find("actively_proven")->as_bool();
                dd.vrr_state = prov ? "actively proven"
                              : pat  ? "presentation compatible"
                              : sup  ? "supported"
                                     : "unavailable";
            }
            hv.displays.push_back(dd);
        }
    }
    if (auto* a = v->find("audio")) {
        for (const auto& e : a->as_array()) {
            std::string n = str(&e, "name");
            if (e.find("default_output") && e.find("default_output")->as_bool())
                n += "  [default]";
            hv.audio.push_back(n);
        }
    }
    if (auto* s = v->find("storage")) {
        for (const auto& e : s->as_array()) {
            std::string klass = str(&e, "class");
            if (klass.empty()) klass = str(&e, "klass");
            std::string bus = str(&e, "bus_type");
            hv.storage.push_back(klass + (bus.empty() ? "" : " / " + bus) +
                                 "  " + GiB(num(&e, "capacity_bytes")));
        }
    }
    if (auto* i = v->find("input")) {
        for (const auto& e : i->as_array()) {
            hv.inputs.push_back(str(&e, "name") + "  [" + str(&e, "backend") + "]");
        }
    }
    if (auto* b = v->find("build")) {
        hv.build_hash = str(b, "git_hash");
        hv.runtime_ver = str(b, "runtime_version");
        hv.build_cfg = str(b, "build_config");
    }
    return hv;
}

// ---------------------------------------------------------------------------
// Shell application
// ---------------------------------------------------------------------------
class ShellApp;  // fwd: WndProc routes topology events to the session app
void ShellRouteTopology(ShellApp* app);  // defined after ShellApp

LRESULT CALLBACK ShellWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE: PostQuitMessage(0); return 0;
        case WM_ERASEBKGND: return 1;  // no flicker; D3D12 owns the surface
        // §39 display-topology reaction: a topology event must never leave
        // stale DCX-* session claims standing. Keep this handler non-blocking;
        // mutation happens on the main loop (ProcessTopologyChange).
        case WM_DISPLAYCHANGE:
            if (ShellApp* app =
                    reinterpret_cast<ShellApp*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA)))
                ShellRouteTopology(app);
            break;  // DefWindowProc broadcasts further
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

enum class View { Home, Library, System, Guide };

constexpr int kGuideItems = 6;
const char* kGuideLabels[kGuideItems] = {
    "Resume", "Home", "Close Game", "Controller", "Audio",
    "Exit Console Session"};

class ShellApp {
public:
    ShellApp(Frame& f, Surface& s, const TitleRegistry& reg, HostView hv,
             dcsess::SessionJournal& j, SessionStateMachine& sm, uint64_t& corr)
        : frame_(f), surface_(s), reg_(reg), host_(hv), journal_(j), sm_(sm),
          corr_(corr) {}

    bool Init();
    int Run(int selftest_frames);

private:
    // Session plumbing -------------------------------------------------------
    uint64_t NowNs() {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return uint64_t(double(c.QuadPart - qpc0_.QuadPart) /
                        double(qpf_.QuadPart) * 1e9);
    }
    bool Trans(SessionState to, TransitionReason r) {
        TransitionResult res = sm_.Transition(to, r, NowNs(), corr_);
        if (res == TransitionResult::Ok) {
            journal_.Append(sm_.Journal().back(), session_id_);
            std::printf("[session] %s\n", SessionStateName(sm_.state()));
            return true;
        }
        std::printf("[session] REJECTED %s -> %s\n",
                    SessionStateName(sm_.state()), SessionStateName(to));
        return false;
    }

public:
    // §39 display-topology reaction: invalidate live session claims; defer any
    // close to the main loop (WndProc must never block on the supervisor).
    void OnDisplayTopologyChanged();
    void SetTopologyWindow(HWND h) { hwnd_ = h; }   // selftest injection path

private:
    bool LaunchTitle(const TitleEntry& e);
    void CloseTitle(TransitionReason r);
    void PollTitleExit();
    void ProcessTopologyChange();

    // Input ------------------------------------------------------------------
    void PollInput(bool& quit);
    uint32_t KbdButtons();   // development fallback → semantic button mask

    // Views ------------------------------------------------------------------
    void Navigate(uint32_t pressed);
    void Confirm();
    void GuideItem(int sel);
    void Draw();

    Frame& frame_;
    Surface& surface_;
    const TitleRegistry& reg_;
    HostView host_;
    dcsess::SessionJournal& journal_;
    SessionStateMachine& sm_;
    uint64_t& corr_;

    std::string session_id_;
    LARGE_INTEGER qpf_{}, qpc0_{};
    uint32_t last_title_pid_ = 0;

    IInputRouter* router_ = nullptr;
    ITitleSupervisor* supervisor_ = nullptr;
    std::string active_title_;
    bool title_active_ = false;

    View view_ = View::Home;
    FocusGraph fg_home_, fg_lib_, fg_sys_;
    int guide_sel_ = 0;
    bool quit_requested_ = false;
    std::string toast_;
    ULONGLONG toast_until_ = 0;
    uint32_t prev_buttons_ = 0;
    uint32_t prev_kbd_ = 0;

    // §39 topology-reaction state (set in WndProc, consumed on main loop)
    std::atomic<bool> topo_pending_{false};
    std::vector<DisplayDeviceInfo> topo_displays_;
    HWND hwnd_ = nullptr;
    bool topo_injected_ = false;
};

// WndProc side: capture live display truth immediately, defer all mutation.
void ShellApp::OnDisplayTopologyChanged() {
    topo_displays_.clear();
    dcwin::ProbeDisplays(topo_displays_);
    topo_pending_.store(true, std::memory_order_release);
}

void ShellRouteTopology(ShellApp* app) { app->OnDisplayTopologyChanged(); }

// Main-loop side: journal the reaction, invalidate stale DCX claims only in
// the live view (evidence files are immutable per ADR-0023), and close a
// superseded title through the supervisor's journaled path.
void ShellApp::ProcessTopologyChange() {
    if (!topo_pending_.exchange(false, std::memory_order_acq_rel)) return;

    std::string mode;
    for (const auto& d : topo_displays_) {
        if (d.active && d.width) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%ux%u@%g", d.width, d.height,
                          d.desktop_refresh_hz);
            if (!mode.empty()) mode += ", ";
            mode += buf;
            if (d.primary) break;
        }
    }
    std::printf("[session] topology change: displays=%s\n",
                mode.empty() ? "none" : mode.c_str());

    if (sm_.state() == SessionState::TitleActive) {
        std::printf("[session] active title superseded by display change\n");
        CloseTitle(TransitionReason::TopologyChanged);  // journal through supervisor
    } else {
        // Steady shell: ShellActive→ShellActive is illegal by design; journal
        // the topology reaction as a note entry instead (from == to).
        SessionTransition t{};
        t.from = t.to = sm_.state();
        t.reason = TransitionReason::TopologyChanged;
        t.monotonic_ns = NowNs();
        t.correlation_id = corr_;
        journal_.Append(t, session_id_);
    }

    // Truthful invalidation: drop session claims whose display prerequisite no
    // longer holds against LIVE enumerated truth. Host claims (DCP-*) are
    // untouched — ADR-0022: host capability ≠ session experience.
    auto stale = [&](const std::string& id) -> const char* {
        if (id.find("UHD") != std::string::npos) {
            bool any_uhd = false;
            for (const auto& d : topo_displays_)
                for (const auto& m : d.modes)
                    if (m.width >= 3840 && m.height >= 2160) any_uhd = true;
            if (!any_uhd) return "DISPLAY_MODE_UNAVAILABLE";
        }
        if (id.find("HDR") != std::string::npos) {
            bool hdr_active = false;
            for (const auto& d : topo_displays_)
                if (d.hdr.advanced_color_active) hdr_active = true;
            if (!hdr_active) return "HDR_INACTIVE";
        }
        if (id.find("VRR") != std::string::npos) {
            // A prior active proof does not survive a topology change; a fresh
            // displayprobe run is required to re-prove the new path (C10).
            bool proven = false;
            for (const auto& d : topo_displays_)
                if (d.vrr.actively_proven) proven = true;
            if (!proven) return "VRR_NOT_ACTIVELY_PROVEN";
        }
        return nullptr;
    };
    std::vector<std::string> keep, rej;
    for (const auto& id : host_.dcx) {
        if (const char* why = stale(id)) {
            rej.push_back(id + " (invalidated: " + why + ")");
            std::printf("[session] invalidated %s: %s\n", id.c_str(), why);
        } else {
            keep.push_back(id);
        }
    }
    host_.dcx = std::move(keep);
    host_.dcx_rej = std::move(rej);

    toast_ = "Display change — session capabilities invalidated";
    toast_until_ = GetTickCount64() + 5000;
}

bool ShellApp::Init() {
    QueryPerformanceFrequency(&qpf_);
    QueryPerformanceCounter(&qpc0_);

    router_ = CreateGameInputRouter();
    if (router_ && router_->DeviceCount() == 0) {
        // GameInput runtime lives but no pad attached: keep it (hotplug is
        // live via its callback plumbing) — keyboard remains the fallback.
    }
    if (!router_) {
        std::printf("[input] GameInput router unavailable; keyboard fallback\n");
    }

    // Focus graphs (§33): explicit topology, no implicit order.
    fg_home_.AddNode({"home.play", "", "", "", "home.library"});
    fg_home_.AddNode({"home.library", "", "", "home.play", "home.system"});
    fg_home_.AddNode({"home.system", "", "", "home.library", ""});
    fg_home_.SetCurrent("home.play");
    {
        const auto& titles = reg_.Entries();
        std::string prev;
        for (const auto& t : titles) {
            std::string id = "lib." + t.title_id;
            fg_lib_.AddNode({id, "", "", prev, ""});
            if (auto* n = const_cast<FocusNode*>(fg_lib_.Node(prev)))
                n->down = id;
            prev = id;
        }
        if (!titles.empty())
            fg_lib_.SetCurrent("lib." + titles.front().title_id);
    }
    fg_sys_.AddNode({"sys.exit_desktop", "", "", "", ""});
    fg_sys_.SetCurrent("sys.exit_desktop");
    return true;
}

uint32_t ShellApp::KbdButtons() {
    auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    auto bit = [&](GamepadButton b) {
        return (uint32_t)1u << (uint32_t)b;
    };
    uint32_t m = 0;
    if (down(VK_UP)) m |= bit(GamepadButton::DpadUp);
    if (down(VK_DOWN)) m |= bit(GamepadButton::DpadDown);
    if (down(VK_LEFT)) m |= bit(GamepadButton::DpadLeft);
    if (down(VK_RIGHT)) m |= bit(GamepadButton::DpadRight);
    if (down(VK_RETURN)) m |= bit(GamepadButton::South);
    if (down(VK_BACK)) m |= bit(GamepadButton::East);
    if (down('S')) m |= bit(GamepadButton::Start);
    return m;
}

void ShellApp::PollInput(bool& quit) {
    ConsoleAction acts = router_ ? router_->Poll(ConsoleAction::None)
                                 : ConsoleAction::None;
    uint32_t pressed = 0;
    if (router_) {
        GamepadState now = router_->StateFor(1);
        pressed = now.buttons & ~prev_buttons_;
        prev_buttons_ = now.buttons;
    }
    uint32_t kb = KbdButtons();
    pressed |= kb & ~prev_kbd_;
    prev_kbd_ = kb;

    // Guide ownership (§34/§17): the system action always wins.
    if (static_cast<uint32_t>(acts) & static_cast<uint32_t>(ConsoleAction::Guide)) {
        if (sm_.state() == SessionState::TitleActive) {
            Trans(SessionState::TitleSuspended, TransitionReason::GuideActivated);
        }
        view_ = View::Guide;
        guide_sel_ = 0;
        return;
    }

    if (view_ == View::Guide) {
        if (pressed & (1u << (uint32_t)GamepadButton::DpadUp)) {
            guide_sel_ = (guide_sel_ + kGuideItems - 1) % kGuideItems;
        } else if (pressed & (1u << (uint32_t)GamepadButton::DpadDown)) {
            guide_sel_ = (guide_sel_ + 1) % kGuideItems;
        } else if (pressed & (1u << (uint32_t)GamepadButton::South)) {
            GuideItem(guide_sel_);
            if (quit_requested_) quit = true;
        } else if (pressed & (1u << (uint32_t)GamepadButton::East)) {
            GuideItem(0);  // B = resume
            if (quit_requested_) quit = true;
        }
        return;
    }

    if (title_active_) return;  // title owns the screen; Guide already handled

    Navigate(pressed);

    if (pressed & (1u << (uint32_t)GamepadButton::East) && view_ != View::Home)
        view_ = View::Home;
    if (pressed & (1u << (uint32_t)GamepadButton::Start))
        view_ = View::System;

    if (GetAsyncKeyState(VK_ESCAPE) & 0x0001) quit = true;  // dev escape only
}

void ShellApp::Navigate(uint32_t pressed) {
    FocusGraph* fg = view_ == View::Home   ? &fg_home_
                     : view_ == View::Library ? &fg_lib_
                     : view_ == View::System  ? &fg_sys_
                                              : nullptr;
    if (!fg) return;
    if (pressed & (1u << (uint32_t)GamepadButton::DpadUp))
        fg->Move(FocusDirection::Up);
    if (pressed & (1u << (uint32_t)GamepadButton::DpadDown))
        fg->Move(FocusDirection::Down);
    if (pressed & (1u << (uint32_t)GamepadButton::DpadLeft))
        fg->Move(FocusDirection::Left);
    if (pressed & (1u << (uint32_t)GamepadButton::DpadRight))
        fg->Move(FocusDirection::Right);
    if (pressed & (1u << (uint32_t)GamepadButton::South)) Confirm();
}

void ShellApp::Confirm() {
    if (view_ == View::Home) {
        std::string cur = fg_home_.Current();
        if (cur == "home.play" && !reg_.Entries().empty()) {
            LaunchTitle(reg_.Entries().front());
        } else if (cur == "home.library") {
            view_ = View::Library;
        } else if (cur == "home.system") {
            view_ = View::System;
        }
    } else if (view_ == View::Library) {
        std::string cur = fg_lib_.Current();
        if (cur.rfind("lib.", 0) == 0) {
            if (const TitleEntry* e = reg_.Find(cur.substr(4))) LaunchTitle(*e);
        }
    } else if (view_ == View::System) {
        if (fg_sys_.Current() == "sys.exit_desktop") quit_requested_ = true;
    }
}

bool ShellApp::LaunchTitle(const TitleEntry& e) {
    Trans(SessionState::TitleRequested, TransitionReason::Requested);
    Trans(SessionState::TitleValidating, TransitionReason::Requested);
    fs::path exe = fs::absolute(e.entrypoint);
    std::error_code ec;
    if (!fs::exists(exe, ec)) {
        std::printf("[title] validation failed: missing %s\n", exe.string().c_str());
        Trans(SessionState::ShellActive, TransitionReason::FatalError);
        return false;
    }
    Trans(SessionState::TitleStarting, TransitionReason::Requested);
    supervisor_ = CreateTitleSupervisor();

    // Typed launch contract (canon §16 embryo): the title receives its
    // runtime context through the versioned DC_TITLE_CONTEXT/1 envelope.
    TitleLaunchContext ctx;
    ctx.title_id = e.title_id;
    ctx.title_version = e.version;
    ctx.session_id = session_id_;
    ctx.user_id = 1;  // offline single-user profile (C9)
    ctx.controller_required = e.controller_required;
    ctx.offline_launch = e.offline_launch;
    ctx.guide_owned_by_platform = true;  // §34: titles never own the Guide
    // Active display truth: the primary display's desktop mode with rational
    // refresh preserved (ADR-0022). The shell displays capability; it never
    // recomputes profiles here (§40) — mode truth only.
    for (const auto& d : host_.displays) {
        if (d.primary && d.w && d.h) {
            ctx.display_width = d.w;
            ctx.display_height = d.h;
            ctx.refresh_numerator = static_cast<uint32_t>(d.hz * 1000.0);
            ctx.refresh_denominator = 1000;
            break;
        }
    }
    // Real host-capability evidence, verbatim (title sees exactly what the
    // platform certified; no shell recombination).
    {
        fs::path cap = "evidence/host-capability.json";
        if (const char* env = std::getenv("DC_HOSTCAP_PATH")) cap = env;
        std::ifstream f(cap, std::ios::binary);
        if (f) {
            std::string text((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
            std::string perr;
            auto parsed = json::Parse(text, perr);
            if (parsed) ctx.host_capability_json = text;  // transport-verified
            else std::printf("[title] host evidence malformed: %s\n", perr.c_str());
        }
    }
    ctx.active_session_profiles = host_.dcx;  // currently proven DCX claims

    auto launch = supervisor_->Launch(exe.string(), exe.parent_path().string(),
                                      "", ctx);
    if (!launch.ok) {
        std::fprintf(stderr, "[title] launch failed: %s\n", launch.error.c_str());
        Trans(SessionState::ShellRecovering, TransitionReason::FatalError);
        Trans(SessionState::ShellActive, TransitionReason::TitleCrash);
        DestroyTitleSupervisor(supervisor_);
        supervisor_ = nullptr;
        return false;
    }
    std::printf("[title] launched pid=%u (%s)\n", launch.process_id,
                e.title_id.c_str());
    Trans(SessionState::TitleActive, TransitionReason::Requested);
    last_title_pid_ = launch.process_id;
    title_active_ = true;
    active_title_ = e.title_id;
    ++corr_;
    return true;
}

void ShellApp::CloseTitle(TransitionReason r) {
    if (!supervisor_) return;
    supervisor_->RequestExit();
    TitleExitReport rep = supervisor_->WaitExit(3000);
    if (rep.kind == TitleExitKind::StillRunning) {
        supervisor_->TerminateTree();
        rep = supervisor_->WaitExit(2000);
    }
    std::printf("[title] exit kind=%s code=%u\n",
                ITitleSupervisor::ExitKindName(rep.kind), rep.exit_code);
    Trans(rep.kind == TitleExitKind::Clean ? SessionState::TitleStopping
                                           : SessionState::TitleCrashed, r);
    Trans(SessionState::ShellRecovering, r);
    Trans(SessionState::ShellActive, r);
    DestroyTitleSupervisor(supervisor_);
    supervisor_ = nullptr;
    title_active_ = false;
    active_title_.clear();
}

void ShellApp::PollTitleExit() {
    if (!title_active_ || !supervisor_) return;
    TitleExitReport rep = supervisor_->WaitExit(0);
    if (rep.kind == TitleExitKind::StillRunning) return;
    TransitionReason r = (rep.kind == TitleExitKind::Clean)
                             ? TransitionReason::TitleExitClean
                             : TransitionReason::TitleCrash;
    std::printf("[title] exit kind=%s code=%u\n",
                ITitleSupervisor::ExitKindName(rep.kind), rep.exit_code);
    Trans(rep.kind == TitleExitKind::Clean ? SessionState::TitleStopping
                                           : SessionState::TitleCrashed, r);
    Trans(SessionState::ShellRecovering, r);
    Trans(SessionState::ShellActive, r);
    DestroyTitleSupervisor(supervisor_);
    supervisor_ = nullptr;
    title_active_ = false;
    active_title_.clear();
    view_ = View::Home;
}

void ShellApp::GuideItem(int sel) {
    switch (sel) {
        case 0:  // Resume
            if (sm_.state() == SessionState::TitleSuspended) {
                Trans(SessionState::TitleResuming, TransitionReason::Requested);
                Trans(SessionState::TitleActive, TransitionReason::Requested);
            }
            view_ = View::Home;
            break;
        case 1:  // Home
            if (title_active_) CloseTitle(TransitionReason::Requested);
            view_ = View::Home;
            break;
        case 2:  // Close Game
            if (title_active_) CloseTitle(TransitionReason::Requested);
            view_ = View::Home;
            break;
        case 3: case 4:  // Controller / Audio: real panels are DK0-M3
            toast_ = sel == 3 ? "Controller panel — DK0-M3"
                              : "Audio panel — DK0-M3";
            toast_until_ = GetTickCount64() + 2500;
            break;
        case 5:  // Exit Console Session
            if (title_active_) CloseTitle(TransitionReason::UserExit);
            quit_requested_ = true;
            break;
    }
}

// ---------------------------------------------------------------------------
// Views (§31). 10-foot type sizes, safe-area coordinates, explicit focus.
// ---------------------------------------------------------------------------
void ShellApp::Draw() {
    frame_.Clear(0xFF141820);
    const auto& titles = reg_.Entries();

    // Header
    frame_.SafeText(40, 28, "11VATED DIGITAL CONSOLE", 42, 0xFFF0F4FF);
    frame_.SafeText(40, 84, view_ == View::Home    ? "HOME"
                            : view_ == View::Library  ? "LIBRARY"
                            : view_ == View::System   ? "SYSTEM"
                                                      : "GUIDE",
                    22, 0xFF38B6FF);

    if (view_ == View::Home) {
        char line[192];
        frame_.SafeText(40, 150, "Player 1   ·   offline profile", 26, 0xFFD8E0F0);
        std::snprintf(line, sizeof(line), "Controllers: %zu    Session: %s",
                      router_ ? router_->DeviceCount() : 0,
                      SessionStateName(sm_.state()));
        frame_.SafeText(40, 196, line, 26,
                        router_ && router_->DeviceCount() ? 0xFF9EE8A0 : 0xFFFFB060);

        frame_.SafeText(40, 268, "HOST CAPABILITY", 20, 0xFF8FB8E8);
        if (!host_.loaded) {
            frame_.SafeText(40, 306,
                "(host evidence not found — run dc-hostprof)", 24, 0xFF8090A8);
        } else {
            frame_.SafeText(40, 306, host_.gpu + "   ·   " + host_.vram +
                                     " VRAM   ·   driver " + host_.driver,
                            24, 0xFFE0E8F8);
            frame_.SafeText(40, 344, host_.cpu + "   ·   " + host_.ram + " RAM",
                            24, 0xFFE0E8F8);
            char sc[220];
            std::snprintf(sc, sizeof(sc),
                          "raster %s · compute %s · RT %s · game-thread %s · sustained %s",
                          Score(host_.raster).c_str(), Score(host_.compute).c_str(),
                          Score(host_.rt).c_str(), Score(host_.cpu_gt).c_str(),
                          Score(host_.cpu_sust).c_str());
            frame_.SafeText(40, 382, sc, 21, 0xFFAAB8D8);
        }

        std::string prof = "Host profiles: ";
        if (host_.dcp.empty()) prof += "(none derived)";
        for (auto& p : host_.dcp) prof += p + "  ";
        frame_.SafeText(40, 428, prof, 23, 0xFF7FD4FF);
        std::string sess = "Session: ";
        if (host_.dcx.empty()) sess += "(none active)";
        for (auto& p : host_.dcx) sess += p + "  ";
        frame_.SafeText(40, 464, sess, 23, 0xFFFFD980);

        // Focus tiles
        struct Tile { const char* id; const char* label; int x; };
        Tile tiles[] = {{"home.play", "Play Recent", 40},
                        {"home.library", "Library", 480},
                        {"home.system", "System", 920}};
        for (auto& t : tiles) {
            bool on = fg_home_.Current() == t.id;
            frame_.FillRect(kSafeX + t.x, kSafeY + 540, 400, 120,
                            on ? 0xFF1E3A5F : 0xFF1A2130);
            if (on) frame_.FillRect(kSafeX + t.x, kSafeY + 540, 400, 6,
                                    0xFF38B6FF);
            frame_.SafeText(t.x + 24, 578, t.label, 30,
                            on ? 0xFFFFFFFF : 0xFF9AAAC8);
        }
        if (!titles.empty())
            frame_.SafeText(40, 700, "Recent: " + titles.front().name +
                                      "  v" + titles.front().version,
                            24, 0xFFC8D4F0);
        frame_.SafeText(40, 930,
            "A confirm · B back · LB/RB-less navigation: arrows · Start System · Guide overlay",
            19, 0xFF6878A0);
    }
    else if (view_ == View::Library) {
        if (titles.empty()) {
            frame_.SafeText(40, 200, "No native titles registered.", 30, 0xFFB0B8C8);
            frame_.SafeText(40, 250,
                "Register one with dc-session --register-title (see CURRENT_STATE.md).",
                22, 0xFF8090A8);
        }
        int y = 150;
        for (const auto& t : titles) {
            std::string id = "lib." + t.title_id;
            bool on = fg_lib_.Current() == id;
            frame_.FillRect(kSafeX + 30, kSafeY + y - 12, 1500, 88,
                            on ? 0xFF1E3A5F : 0xFF1A2130);
            frame_.SafeText(60, y, t.name + "   v" + t.version, 30,
                            on ? 0xFFFFFFFF : 0xFF98A8C8);
            frame_.SafeText(60, y + 42, t.title_id, 19, 0xFF7080A0);
            y += 104;
            if (y > 800) break;
        }
        frame_.SafeText(40, 930, "A launch · B back · Up/Down navigate",
                        19, 0xFF6878A0);
    }
    else if (view_ == View::System) {
        int y = 140;
        auto line = [&](const std::string& k, const std::string& v,
                        uint32_t col = 0xFFE0E8F8) {
            frame_.SafeText(40, y, k, 22, 0xFF8FB8E8);
            frame_.SafeText(360, y, v, 22, col);
            y += 40;
        };
        if (!host_.loaded) {
            frame_.SafeText(40, y, "(host evidence not found — run dc-hostprof)",
                            24, 0xFF8090A8);
            y += 48;
        } else {
            for (size_t i = 0; i < host_.displays.size() && i < 2; ++i) {
                const auto& d = host_.displays[i];
                char buf[256];
                std::snprintf(buf, sizeof(buf), "%s%ux%u @ %.3f Hz · HDR %s%s · VRR %s",
                              d.primary ? "[primary] " : "", d.w, d.h, d.hz,
                              d.hdr_kind.c_str(),
                              d.hdr_active ? " (active)" : "",
                              d.vrr_state.c_str());
                line(i == 0 ? "Display" : "Display 2", buf);
            }
            if (host_.displays.empty()) line("Display", "(none probed)");
            std::string audio = host_.audio.empty() ? "(none probed)"
                                                    : host_.audio.front();
            line("Audio", audio);
            std::string storage = host_.storage.empty() ? "(none probed)"
                                                        : host_.storage.front();
            line("Storage", storage);
            line("Input", host_.inputs.empty() && !(router_ && router_->DeviceCount())
                              ? "(no controllers)"
                              : std::to_string(router_ ? router_->DeviceCount() : 0) +
                                    " controller(s) live");
            line("Qualification",
                 host_.qual_mode + (host_.qual_sustained_valid
                                        ? " · sustained valid"
                                        : " · sustained not valid") +
                     (host_.qual_time.empty() ? "" : " · " + host_.qual_time));
            line("Console runtime",
                 host_.runtime_ver + " · build " +
                     host_.build_hash.substr(0, std::min<size_t>(10, host_.build_hash.size())) +
                     " · " + host_.build_cfg);
        }
        // Focusable exit tile
        bool on = fg_sys_.Current() == "sys.exit_desktop";
        frame_.FillRect(kSafeX + 40, kSafeY + 760, 520, 96,
                        on ? 0xFF3A1E28 : 0xFF1A2130);
        if (on) frame_.FillRect(kSafeX + 40, kSafeY + 760, 520, 6, 0xFFFF5070);
        frame_.SafeText(76, 792, "Exit to Desktop (development escape)", 28,
                        on ? 0xFFFFFFFF : 0xFFB098A8);
        frame_.SafeText(40, 930, "A confirm · B back", 19, 0xFF6878A0);
    }
    else {  // Guide overlay (§34)
        // Dim + panel; when a title is active it keeps rendering behind us.
        frame_.FillRect(0, 0, (int)kW, (int)kH, 0x90101420);
        frame_.FillRect(kSafeX + 480, kSafeY + 60, 960, 620, 0xE8101828);
        frame_.SafeText(520, 92, "GUIDE", 40, 0xFFFFFFFF);
        frame_.SafeText(520, 152,
            title_active_ ? ("Title running: " + active_title_ +
                             "  —  suspended while the Guide is open")
                          : "No title running",
            22, 0xFFB0C0D8);
        int gy = 230;
        for (int i = 0; i < kGuideItems; ++i) {
            bool on = i == guide_sel_;
            frame_.FillRect(kSafeX + 520, kSafeY + gy - 10, 880, 56,
                            on ? 0xFF1E3A5F : 0x00101828);
            frame_.SafeText(560, gy, kGuideLabels[i], 28,
                            on ? 0xFFFFFFFF : 0xFF98A8C8);
            gy += 68;
        }
        if (!toast_.empty() && GetTickCount64() < toast_until_)
            frame_.SafeText(520, gy + 20, toast_, 22, 0xFFFFD980);
        frame_.SafeText(40, 930, "Up/Down select · A confirm · Guide/B resume",
                        19, 0xFF6878A0);
    }

    surface_.Present(frame_);
}

int ShellApp::Run(int selftest_frames) {
    // Selftest path: proves the full launch pipeline automatically — boot,
    // render, launch the first registered native title, supervised shutdown.
    bool do_selftest_launch =
        (selftest_frames > 0) && !reg_.Entries().empty();
    if (do_selftest_launch) {
        LaunchTitle(reg_.Entries().front());
    }
    bool quit = false;
    int frames = 0;
    while (!quit) {
        MSG msg{};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit = true;
            else { TranslateMessage(&msg); DispatchMessageA(&msg); }
        }
        if (quit) break;

        // §39 verification hook (selftest): inject a real WM_DISPLAYCHANGE
        // mid-run and seed a stale DCX-UHD120 claim. Live panel truth (no
        // 3840×2160 mode) must invalidate it through ProcessTopologyChange.
        if (selftest_frames > 0 && !topo_injected_ && frames == 15) {
            std::printf("[session] selftest: injecting WM_DISPLAYCHANGE\n");
            host_.dcx.push_back("DCX-UHD120");   // stale claim seed
            SendMessageA(hwnd_, WM_DISPLAYCHANGE, 0, 0);
            topo_injected_ = true;
        }
        ProcessTopologyChange();   // §39: reacts to WM_DISPLAYCHANGE
        PollTitleExit();
        PollInput(quit);
        if (sm_.CheckTimeout(NowNs())) {
            // Timeout fired → forced RECOVERY (journal it), then restore shell.
            journal_.Append(sm_.Journal().back(), session_id_);
            std::printf("[session] TIMEOUT -> %s\n",
                        SessionStateName(sm_.state()));
            Trans(SessionState::ShellActive, TransitionReason::TimeoutExpired);
        }
        Draw();

        ++frames;
        if (selftest_frames > 0 && frames >= selftest_frames) {
            std::printf("[shell] selftest frames=%d state=%s\n",
                        frames, SessionStateName(sm_.state()));
            break;
        }
        Sleep(8);
    }

    // Titles never outlive the session (§27).
    if (supervisor_) CloseTitle(TransitionReason::UserExit);
    if (router_) DestroyGameInputRouter(router_);
    return 0;
}

} // namespace

int RunShell(int selftest_frames) {
    const char* reg_env = std::getenv("DC_REGISTRY_DIR");
    fs::path registry_dir = reg_env ? reg_env : "registry";
    const char* jour_env = std::getenv("DC_JOURNAL_DIR");
    fs::path journal_dir = jour_env ? jour_env : "evidence/sessions";

    TitleRegistry registry;
    std::vector<std::string> errors;
    registry.LoadFromDirectory(registry_dir.string(), &errors);
    for (auto& e : errors) std::fprintf(stderr, "[registry] %s\n", e.c_str());

    std::string session_id = "sess-" + dcsess::NowStamp();
    dcsess::SessionJournal journal(journal_dir / (session_id + ".jsonl"));
    if (!journal.ok()) {
        std::fprintf(stderr, "fatal: cannot open journal\n");
        return 1;
    }
    SessionStateMachine sm;
    uint64_t corr = 1;

    // Window + renderer
    WNDCLASSA wc{};
    wc.lpfnWndProc = ShellWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "DC_ConsoleShell";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName,
                                "11vated Digital Console — DK0-M2",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                1280, 720, nullptr, nullptr,
                                GetModuleHandleA(nullptr), nullptr);
    if (!hwnd) {
        std::fprintf(stderr, "fatal: window creation failed\n");
        return 3;
    }
    ShowWindow(hwnd, SW_SHOW);

    Frame frame;
    if (!frame.Init(hwnd)) {
        std::fprintf(stderr, "fatal: frame init failed\n");
        return 3;
    }
    Surface surface;
    if (!surface.Init(hwnd)) {
        std::fprintf(stderr, "fatal: D3D12 surface init failed\n");
        return 3;
    }

    // Boot (§25) — journaled.
    HostView host = LoadHostView();
    ShellApp app(frame, surface, registry, host, journal, sm, corr);
    if (!app.Init()) return 1;
    // Route window messages (§39 WM_DISPLAYCHANGE) to the session app.
    SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
    app.SetTopologyWindow(hwnd);

    LARGE_INTEGER qpf{};
    QueryPerformanceFrequency(&qpf);
    LARGE_INTEGER qpc0{};
    QueryPerformanceCounter(&qpc0);
    auto now_ns = [&]() -> uint64_t {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return uint64_t(double(c.QuadPart - qpc0.QuadPart) /
                        double(qpf.QuadPart) * 1e9);
    };
    auto boot = [&](SessionState to) {
        TransitionResult r = sm.Transition(to, TransitionReason::BootSequence,
                                           now_ns(), corr);
        if (r == TransitionResult::Ok) {
            journal.Append(sm.Journal().back(), session_id);
            std::printf("[session] %s\n", SessionStateName(sm.state()));
        }
    };
    boot(SessionState::SessionStarting);
    boot(SessionState::HostValidating);
    boot(SessionState::SessionCapabilitiesResolving);
    boot(SessionState::InputAcquiring);
    boot(SessionState::ShellStarting);
    boot(SessionState::ShellActive);

    int rc = app.Run(selftest_frames);

    TransitionResult r = sm.Transition(SessionState::SessionExit,
                                       TransitionReason::UserExit, now_ns(), corr);
    if (r == TransitionResult::Ok) {
        journal.Append(sm.Journal().back(), session_id);
        std::printf("[session] SESSION_EXIT\n");
    }
    std::printf("[session] ended (%s)\n", session_id.c_str());
    return rc;
}
