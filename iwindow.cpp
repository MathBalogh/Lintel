#include "iwindow.h"
#include "inode.h"

namespace lintel {

// ===========================================================================
// WndProc
// ===========================================================================
//
// Runs on the Win32 message thread.  All messages except WM_DESTROY and
// WM_ERASEBKGND are forwarded verbatim to the worker thread via Core::push().
// The worker thread then calls Core::process_message() on each queued item
// and Core::process_default() (layout + draw + present) after each flush.
//
// We deliberately do minimal work here: no hit testing, no state mutation.
// This keeps the message thread responsive and all rendering logic on the
// worker thread where it is serialised by the message queue.
//

static IWindow* this_win = nullptr;
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

            // Suppress background erase; the D2D/D3D pipeline owns every pixel.
        case WM_ERASEBKGND:
            return 1;

            // Resize the swap chain and UI texture synchronously on the message
            // thread so the new content fills the window in the same frame that
            // the window edge moves, eliminating the one-frame lag that causes
            // resize jitter.  render_mut_ in resize_now() serialises this against
            // any in-flight draw on the worker thread.
        case WM_SIZE:
            this_win->doc.resize_now();
            return 0;
    }

    this_win->doc.push({ msg, wp, lp });

    return DefWindowProc(hwnd, msg, wp, lp);
}

// ===========================================================================
// Window - construction / destruction
// ===========================================================================

Window::Window() {
    impl_allocate();
    this_win = iptr_;

    // -----------------------------------------------------------------------
    // Win32 window class + HWND
    // -----------------------------------------------------------------------

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"lintel::Window";
    RegisterClass(&wc);

    constexpr unsigned int W = 800, H = 600;

    iptr_->hwnd = CreateWindowW(
        wc.lpszClassName,
        L"",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        W, H,
        nullptr, nullptr,
        wc.hInstance,
        nullptr
    );

    iptr_->width = W;
    iptr_->height = H;

    // -----------------------------------------------------------------------
    // DXGI swap chain
    // -----------------------------------------------------------------------
    //
    // Walk the DXGI chain from the D3D11 device to the IDXGIFactory so we can
    // create a swap chain bound to the HWND we just created.
    //

    ComPtr<IDXGIDevice>  dxgi_device;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory> factory;
    GPU.d3d_device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
    dxgi_device->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = W;
    sd.BufferDesc.Height = H;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = iptr_->hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    factory->CreateSwapChain(GPU.d3d_device.Get(), &sd, &iptr_->swapchain);

    // -----------------------------------------------------------------------
    // Back-buffer render targets
    // -----------------------------------------------------------------------
    // rebuild_targets() creates the D3D RTV and the d2d_target bitmap for
    // the back buffer, but deliberately does NOT set the D2D context target —
    // that is owned by the offscreen texture below.

    iptr_->rebuild_targets();

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(W);
    vp.Height = static_cast<float>(H);
    vp.MinDepth = 0.f;
    vp.MaxDepth = 1.f;
    GPU.d3d_context->RSSetViewports(1, &vp);

    // -----------------------------------------------------------------------
    // Offscreen UI render texture
    // -----------------------------------------------------------------------
    // The D2D context target is set here and stays pointed at this texture
    // for the lifetime of the window (except when briefly redirected to the
    // back buffer for the blit step in process_default).

    iptr_->rebuild_ui_texture(W, H);

    // Size the root node to fill the client area.
    iptr_->doc.root.width(static_cast<float>(W)).height(static_cast<float>(H));
}
Window::~Window() {
    iptr_->doc.shutdown();
}

// ===========================================================================
// Window - accessors
// ===========================================================================

unsigned int Window::width()  const { return iptr_->width; }
unsigned int Window::height() const { return iptr_->height; }

Node& Window::root() { return iptr_->doc.root; }

float Window::mouse_x() { return iptr_->doc.input.mouse_screen_x; }
float Window::mouse_y() { return iptr_->doc.input.mouse_screen_y; }

MouseButton Window::held_button() { return iptr_->doc.input.held; }
Modifiers   Window::modifiers() { return iptr_->doc.input.modifiers; }
int         Window::key_vkey() { return iptr_->doc.input.key_vkey; }
bool        Window::key_repeat() { return iptr_->doc.input.key_repeat; }
wchar_t     Window::key_char() { return iptr_->doc.input.key_char; }
float       Window::scroll_dx() { return iptr_->doc.input.scroll_dx; }
float       Window::scroll_dy() { return iptr_->doc.input.scroll_dy; }

// ===========================================================================
// Window - run loop
// ===========================================================================

int Window::run(std::function<void()> fn) {
    // Enable immersive dark-mode title bar (Windows 10 20H1+).
    DWORD dark = TRUE;
    DwmSetWindowAttribute(
        iptr_->hwnd,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &dark, sizeof(dark)
    );

    ShowWindow(iptr_->hwnd, SW_SHOW);
    SetForegroundWindow(iptr_->hwnd);

    // Kick off the worker thread (layout + draw + present loop).
    iptr_->doc.thread_tick = std::move(fn);
    iptr_->doc.window = WeakImpl<IWindow>(iptr_);
    iptr_->doc.start();

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT)
            return static_cast<int>(msg.wParam);
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

} // namespace lintel
