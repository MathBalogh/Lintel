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
    Document doc;

    ComPtr<IDXGISwapChain>         swapchain;
    ComPtr<ID3D11RenderTargetView> rtv;         // back-buffer RTV (blit target)
    ComPtr<ID2D1Bitmap1>           d2d_target;  // back-buffer D2D surface

    // ------------------------------------------------------------------
    // Offscreen render target
    // ------------------------------------------------------------------
    //
    // The UI is always drawn into this texture rather than directly into
    // the swap-chain back buffer.  Each frame the result is blitted to the
    // back buffer via DrawBitmap.  resize_now() on the message thread
    // recreates the texture synchronously under render_mut_ whenever the
    // window size changes.

    ComPtr<ID3D11Texture2D> ui_texture;
    ComPtr<ID2D1Bitmap1>    ui_d2d_target;
    unsigned int            ui_width = 0;
    unsigned int            ui_height = 0;

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
     * @brief Release the back-buffer render targets.
     *
     * Must be called before ResizeBuffers() so that every reference to the
     * old back buffer is dropped.  The offscreen UI texture is NOT released
     * here — it remains valid across swap-chain resizes.
     */
    void release_targets() {
        rtv.Reset();
        d2d_target.Reset();
        // Unbind the back buffer from D2D, but leave the offscreen target
        // in place so the draw loop can continue uninterrupted.
        if (GPU.d2d_context)
            GPU.d2d_context->SetTarget(nullptr);
    }

    /**
     * @brief (Re)create back-buffer render targets from the current swap chain.
     *
     * Called after every successful ResizeBuffers().  The D2D context target
     * is NOT changed here — it stays pointed at the offscreen surface.
     * Returns false on any unrecoverable D3D / D2D error.
     */
    bool rebuild_targets() {
        release_targets();

        ComPtr<ID3D11Texture2D> back_buffer;
        HR_RET_F(swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)));
        HR_RET_F(GPU.d3d_device->CreateRenderTargetView(back_buffer.Get(), nullptr, &rtv));

        if (GPU.d2d_context) {
            ComPtr<IDXGISurface> surface;
            if (SUCCEEDED(back_buffer.As(&surface))) {
                D2D1_BITMAP_PROPERTIES1 bp{};
                bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
                bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
                bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET
                    | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
                GPU.d2d_context->CreateBitmapFromDxgiSurface(
                    surface.Get(), &bp, &d2d_target);
                // d2d_target is only used as the blit destination during
                // present; the draw-loop target remains ui_d2d_target.
            }
        }

        return true;
    }

    /**
     * @brief Create (or recreate) the offscreen UI render texture.
     *
     * No-ops if the requested size matches the current size.
     * Called from resize_now() on the message thread under render_mut_.
     * Returns false on failure; the old target is left intact.
     */
    bool rebuild_ui_texture(unsigned int w, unsigned int h) {
        if (!GPU.d2d_context || !GPU.d3d_device) return false;
        if (w == 0 || h == 0) return false;
        if (w == ui_width && h == ui_height) return true;

        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> new_tex;
        if (FAILED(GPU.d3d_device->CreateTexture2D(&td, nullptr, &new_tex)))
            return false;

        ComPtr<IDXGISurface> surface;
        if (FAILED(new_tex.As(&surface))) return false;

        D2D1_BITMAP_PROPERTIES1 bp{};
        bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;

        GPU.d2d_context->SetTarget(nullptr);

        ComPtr<ID2D1Bitmap1> new_bmp;
        if (FAILED(GPU.d2d_context->CreateBitmapFromDxgiSurface(
            surface.Get(), &bp, &new_bmp)))
            return false;

        ui_texture = std::move(new_tex);
        ui_d2d_target = std::move(new_bmp);
        ui_width = w;
        ui_height = h;

        GPU.d2d_context->SetTarget(ui_d2d_target.Get());
        return true;
    }

    /**
     * @brief Resize the swap chain to match the current client area.
     *
     * Called from resize_now() under render_mut_.  Only responsible for
     * the swap-chain back buffer; the offscreen UI texture is handled
     * separately by rebuild_ui_texture().
     * Returns false on unrecoverable error.
     */
    bool resize_swapchain() {
        if (!swapchain) return false;

        const unsigned int new_w = client_width();
        const unsigned int new_h = client_height();

        if (new_w == 0 || new_h == 0) return true;

        width = new_w;
        height = new_h;

        release_targets();
        HR_RET_F(swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0));
        return rebuild_targets();
    }
};

// Explicit instantiation - keeps Impl<IWindow> specialisation in this TU.
template class Impl<IWindow>;

} // namespace lintel
