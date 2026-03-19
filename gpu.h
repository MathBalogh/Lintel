#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dwrite.h>
#include <d2d1_1.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// GpuContext
// ---------------------------------------------------------------------------
//
// Holds all Direct3D 11 / Direct2D / DirectWrite device objects.
// Created once by Core and shared via the GPU macro.
//
struct GpuContext {
    ComPtr<ID3D11Device>        d3d_device;
    ComPtr<ID3D11DeviceContext> d3d_context;
    ComPtr<ID2D1Factory1>       d2d_factory;
    ComPtr<ID2D1Device>         d2d_device;
    ComPtr<ID2D1DeviceContext>  d2d_context;
    ComPtr<IDWriteFactory>      dwrite_factory;

    // Returns false if any device object failed to initialise.
    bool initialize();
};

// Evaluate an HRESULT; return false from the enclosing function on failure.
#define HR_RET_F(expr) do { if (FAILED(expr)) { return false; } } while (0)
