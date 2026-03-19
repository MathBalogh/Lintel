#include "gpu.h"

bool GpuContext::initialize() {
    // -----------------------------------------------------------------------
    // D3D11 device + immediate context
    // -----------------------------------------------------------------------

    UINT device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    #if defined(_DEBUG)
    device_flags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

    const D3D_FEATURE_LEVEL requested_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    HR_RET_F(D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        device_flags,
        requested_levels, _countof(requested_levels),
        D3D11_SDK_VERSION,
        &d3d_device,
        nullptr,
        &d3d_context
    ));

    // -----------------------------------------------------------------------
    // D2D factory
    // -----------------------------------------------------------------------
    //
    // MULTI_THREADED is required because the D2D device context is created on
    // the main thread (inside Window::Window) but drawn on the worker thread
    // inside Core::process_default.  Rendering is serialised by the worker
    // loop, so there is no concurrent access, but D2D still needs the
    // multi-threaded flag to permit cross-thread usage.
    //

    D2D1_FACTORY_OPTIONS factory_opts = {};
    #if defined(_DEBUG)
    factory_opts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
    #endif

    HR_RET_F(D2D1CreateFactory(
        D2D1_FACTORY_TYPE_MULTI_THREADED,
        __uuidof(ID2D1Factory1),
        &factory_opts,
        &d2d_factory
    ));

    // -----------------------------------------------------------------------
    // D2D device and device context — interop'd from the D3D11 device
    // -----------------------------------------------------------------------

    ComPtr<IDXGIDevice> dxgi_device;
    HR_RET_F(d3d_device.As(&dxgi_device));
    HR_RET_F(d2d_factory->CreateDevice(dxgi_device.Get(), &d2d_device));
    HR_RET_F(d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_context));
    d2d_context->SetUnitMode(D2D1_UNIT_MODE_PIXELS);

    // -----------------------------------------------------------------------
    // DWrite factory
    // -----------------------------------------------------------------------

    HR_RET_F(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        &dwrite_factory
    ));

    return true;
}
