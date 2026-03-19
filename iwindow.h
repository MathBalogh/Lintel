#pragma once
#include "core.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace lintel {

// ===========================================================================
// IWindow
// ===========================================================================
//
// Implementation layer for the public Window handle.  One IWindow is created
// per Window and stored inside the Impl<IWindow> owner.  Core holds a WeakImpl
// pointing at the same allocation; that weak reference must be cleared in the
// Window destructor to avoid dangling pointer use by the worker thread.
//
// Resource lifetime
// -----------------
//  - swapchain, rtv, d2d_target are created in Window::Window() and
//    freed by COM reference counting when the ComPtr members are Reset().
//  - release_targets() / rebuild_targets() are called on every WM_SIZE.
//    release_targets() must be called before ResizeBuffers(), and must set
//    the D2D render target to nullptr so D2D releases its reference to the
//    back buffer surface.
//
// Thread safety
// -------------
//  The worker thread calls resize() and the draw loop; the Win32 message
//  thread calls WndProc.  All accesses that mutate shared state (width,
//  height, swap-chain targets) are serialised through the message queue:
//  WM_SIZE is enqueued and processed on the worker thread only.
//

class IWindow {
public:
    HWND hwnd = nullptr;

    ComPtr<IDXGISwapChain>         swapchain;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID2D1Bitmap1>           d2d_target;

    unsigned int width = 0;
    unsigned int height = 0;

    // True while a TrackMouseEvent request is outstanding for this window.
    // Reset to false when WM_MOUSELEAVE fires; set again on the next
    // WM_MOUSEMOVE when tracking_mouse is false.
    bool tracking_mouse = false;

    // Reference-counts mouse buttons currently held.  SetCapture is called
    // on the first button-down and ReleaseCapture on the last button-up so
    // that drag operations continue to receive events even when the cursor
    // leaves the client area.
    int buttons_held = 0;

    // -----------------------------------------------------------------------
    // Geometry
    // -----------------------------------------------------------------------

    unsigned int client_width() const {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        return static_cast<unsigned int>(rc.right - rc.left);
    }
    unsigned int client_height() const {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        return static_cast<unsigned int>(rc.bottom - rc.top);
    }

    // -----------------------------------------------------------------------
    // Target management
    // -----------------------------------------------------------------------

    /**
     * @brief Release all per-frame render targets.
     *
     * Must be called before ResizeBuffers() so that every reference to the
     * old back buffer is dropped.  Also clears the D2D device-context target
     * so D2D releases its internal back-buffer reference.
     */
    void release_targets() {
        rtv.Reset();
        d2d_target.Reset();
        if (GPU.d2d_context)
            GPU.d2d_context->SetTarget(nullptr);
    }

    /**
     * @brief (Re)create render targets from the current swap-chain back buffer.
     *
     * Called once at window creation and after every successful ResizeBuffers().
     * Assumes swapchain is valid and width / height are already updated.
     * Returns false on any unrecoverable D3D / D2D error.
     */
    bool rebuild_targets() {
        release_targets();

        // D3D11 render-target view over the swap-chain back buffer.
        ComPtr<ID3D11Texture2D> back_buffer;
        HR_RET_F(swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)));
        HR_RET_F(GPU.d3d_device->CreateRenderTargetView(back_buffer.Get(), nullptr, &rtv));

        // D2D bitmap bound to the same DXGI surface so D2D can draw directly
        // into the swap-chain back buffer without an extra copy.
        if (GPU.d2d_context) {
            ComPtr<IDXGISurface> surface;
            if (SUCCEEDED(back_buffer.As(&surface))) {
                D2D1_BITMAP_PROPERTIES1 bp{};
                bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
                bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET
                    | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

                if (SUCCEEDED(GPU.d2d_context->CreateBitmapFromDxgiSurface(
                    surface.Get(), &bp, &d2d_target))) {
                    GPU.d2d_context->SetTarget(d2d_target.Get());
                }
            }
        }

        return true;
    }

    /**
     * @brief Handle WM_SIZE: resize the swap chain and rebuild render targets.
     *
     * Silently skips zero-dimension sizes (minimised window) and no-op resizes.
     * Returns false on unrecoverable error; the caller should tear down.
     */
    bool resize_swapchain() {
        if (!swapchain) return false;

        const unsigned int new_w = client_width();
        const unsigned int new_h = client_height();

        // Zero dimensions occur when the window is minimised — nothing to do.
        if (new_w == 0 || new_h == 0) return true;

        // Skip if the size hasn't actually changed.
        if (new_w == width && new_h == height) return true;

        width = new_w;
        height = new_h;

        release_targets();
        HR_RET_F(swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0));
        return rebuild_targets();
    }
};

// Explicit instantiation — keeps Impl<IWindow> specialisation in this TU.
template class Impl<IWindow>;

} // namespace lintel
