// native-minimal — the smallest real native Digital Console title (DK0-M2 §29).
// It renders a continuous D3D12 scene, consumes the typed launch context
// (canon §16 embryo: DC_TITLE_CONTEXT/1 envelope) and semantic console
// input, honors WM_CLOSE (the clean-exit path the supervisor uses), and
// returns an exit code the supervisor classifies. This is the seed of the
// .11g contract: the runtime launch path, not the packaging, is what
// DK0-M2 proves.
#include "dc/input.hpp"
#include "dc/title_context.hpp"
#include "dc/title_context_json.hpp"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using Microsoft::WRL::ComPtr;

namespace dc {
IInputRouter* CreateGameInputRouter();   // hosts/windows (GameInput primary)
void DestroyGameInputRouter(IInputRouter*);
} // namespace dc

namespace {

// Exit codes the title supervisor classifies: 0 = clean, 1 = user requested
// immediate exit, nonzero abnormal → crash classification.
constexpr int kExitClean = 0;

// Parse the supervisor-provided launch context. A title MUST tolerate an
// absent envelope (direct-run/development), but MUST reject a foreign
// schema — never guess at an unknown contract.
dc::TitleLaunchContext LoadLaunchContext(bool* received) {
    *received = false;
    dc::TitleLaunchContext ctx;
    const char* env = std::getenv("DC_TITLE_CONTEXT");
    if (!env || !*env) return ctx;
    std::string err;
    dc::TitleLaunchContext parsed;
    if (dc::ParseTitleContext(env, parsed, err)) {
        *received = true;
        return parsed;
    }
    std::fprintf(stderr,
                 "native-minimal: rejecting launch context: %s\n", err.c_str());
    return ctx;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

struct D3D12Title {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> cmdlist;
    ComPtr<IDXGISwapChain3> swapchain;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    UINT rtv_size = 0;
    static constexpr UINT kFrameCount = 2;
    ComPtr<ID3D12Resource> backbufs[kFrameCount];
    UINT frame_index = 0;
    UINT64 fence_value = 0;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event = nullptr;
    UINT width = 1280;
    UINT height = 720;

    bool Init(HWND hwnd) {
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
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

        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            return false;
        ComPtr<IDXGISwapChain1> sc1;
        if (FAILED(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &sd, nullptr,
                                                   nullptr, &sc1)))
            return false;
        if (FAILED(sc1.As(&swapchain)))
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC rhd{};
        rhd.NumDescriptors = kFrameCount;
        rhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(device->CreateDescriptorHeap(&rhd, IID_PPV_ARGS(&rtv_heap))))
            return false;
        rtv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT i = 0; i < kFrameCount; ++i) {
            if (FAILED(swapchain->GetBuffer(i, IID_PPV_ARGS(&backbufs[i]))))
                return false;
            D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap->GetCPUDescriptorHandleForHeapStart();
            h.ptr += i * rtv_size;
            device->CreateRenderTargetView(backbufs[i].Get(), nullptr, h);
        }

        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
            return false;
        fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return fence_event != nullptr;
    }

    void Render(float pulse = 0.0f) {
        frame_index = swapchain->GetCurrentBackBufferIndex();
        allocator->Reset();
        cmdlist->Reset(allocator.Get(), nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backbufs[frame_index].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cmdlist->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += frame_index * rtv_size;
        const float clear[] = {0.06f + 0.10f * pulse, 0.10f + 0.15f * pulse,
                               0.20f + 0.55f * pulse, 1.0f};  // console blue, input-lit
        cmdlist->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        cmdlist->ClearRenderTargetView(rtv, clear, 0, nullptr);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cmdlist->ResourceBarrier(1, &barrier);
        cmdlist->Close();

        queue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(cmdlist.GetAddressOf()));
        swapchain->Present(1, 0);  // VSync'd; presentation contract lands in DK0-M2+

        fence_value++;
        queue->Signal(fence.Get(), fence_value);
        if (fence->GetCompletedValue() < fence_value) {
            fence->SetEventOnCompletion(fence_value, fence_event);
            WaitForSingleObject(fence_event, INFINITE);
        }
    }

    ~D3D12Title() {
        if (fence_event) CloseHandle(fence_event);
    }
};

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Typed launch contract (canon §16 embryo): parse the supervisor's
    // versioned envelope. The ConsoleServices pipe (§16.2) replaces the
    // env transport in DK0-M3 without changing these types.
    bool ctx_received = false;
    dc::TitleLaunchContext ctx = LoadLaunchContext(&ctx_received);
    if (ctx_received) {
        std::printf("native-minimal: session=%s title=%s v%s user=%llu\n",
                    ctx.session_id.c_str(), ctx.title_id.c_str(),
                    ctx.title_version.c_str(),
                    static_cast<unsigned long long>(ctx.user_id));
        std::printf("native-minimal: host evidence %zu bytes, DCX claims %zu\n",
                    ctx.host_capability_json.size(),
                    ctx.active_session_profiles.size());
        std::fflush(stdout);
        if (ctx.guide_owned_by_platform == false) {
            // Contract violation: the platform always owns the Guide (§34).
            std::fprintf(stderr,
                         "native-minimal: refusing context without Guide ownership\n");
            return 1;
        }
    }

    // Semantic console input (platform-owned router). Titles never see raw
    // backend types, and system actions never reach this path (§19).
    dc::IInputRouter* input = dc::CreateGameInputRouter();

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DC_NativeMinimalTitle";
    wc.hCursor = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);

    char title_text[160];
    if (ctx_received && ctx.display_width) {
        std::snprintf(title_text, sizeof(title_text),
                      "Digital Console — Native Title (%ux%u @ %u/%u)",
                      ctx.display_width, ctx.display_height,
                      ctx.refresh_numerator, ctx.refresh_denominator);
    } else {
        std::snprintf(title_text, sizeof(title_text),
                      "Digital Console — Native Title (dev run)");
    }

    // Sized to the console safe area; the shell drives the real presentation
    // contract. DK0-M2: an app-surface window demonstrates the runtime path.
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, title_text,
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
                                nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        if (input) dc::DestroyGameInputRouter(input);
        return 2;
    }
    ShowWindow(hwnd, SW_SHOW);

    D3D12Title title;
    if (!title.Init(hwnd)) {
        std::fprintf(stderr, "native-minimal: D3D12 init failed\n");
        if (input) dc::DestroyGameInputRouter(input);
        return 3;
    }

    // Render loop: runs until the supervisor's WM_CLOSE arrives (clean exit).
    // The clear color responds to semantic controller input — proof that
    // console input reaches the title through the platform contract.
    float pulse = 0.0f;
    MSG msg{};
    while (true) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                if (input) dc::DestroyGameInputRouter(input);
                return kExitClean;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (input) {
            input->Poll(dc::ConsoleAction::None);
            const dc::GamepadState gp = input->StateFor(0);
            pulse = 0.5f + 0.5f * gp.left_y;             // stick drives pulse
            if (gp.buttons & (1u << static_cast<uint32_t>(dc::GamepadButton::South)))
                pulse = 1.0f;                             // accept = full bright
        }
        title.Render(pulse);
        Sleep(16);  // ~60 Hz cadence for the sample; not the platform loop
    }
}