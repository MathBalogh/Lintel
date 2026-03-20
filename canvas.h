#pragma once
// canvas.h
//
// Canvas — the single drawing surface every node renders through.
//
// Design goals
// ------------
//  • Nodes never touch a D2D / DWrite COM pointer directly.
//  • The full Direct2D / DirectWrite API is hidden behind a plain C++ facade.
//  • Swapping the back-end (Skia, software raster, mock) only requires
//    reimplementing Canvas; no node code changes.
//
// Usage
// -----
//  Core::process_default() constructs a Canvas from its GpuContext and passes
//  it by reference into the root INode::draw() call.  Each node forwards the
//  same Canvas& to its children so a single instance is shared for the whole
//  frame.
//
//  For operations that must persist outside the draw pass — specifically the
//  DWrite text formats and layouts in ITextNode — Canvas is also accessible
//  via CORE.canvas so that measure() and event handlers can call the factory
//  methods without receiving a Canvas& parameter.
//

#include "gpu.h"
#include "lintel.h"   // Rect, Color, ComPtr

#include <string_view>

namespace lintel {

class Canvas {
public:
    explicit Canvas(GpuContext& gpu) noexcept;

    // ── Filled shapes ───────────────────────────────────────────────────────

    // Fill a rectangle with an optional uniform corner radius.
    void fill_rect(const Rect& r, Color c, float corner_radius = 0.f);

    // Fill an axis-aligned ellipse whose centre is (cx, cy).
    void fill_ellipse(float cx, float cy, float rx, float ry, Color c);

    // ── Stroked shapes ──────────────────────────────────────────────────────

    // Draw a rectangle outline.  stroke_width > 0 required.
    void stroke_rect(const Rect& r, Color c, float stroke_width,
                     float corner_radius = 0.f);

    // Draw a single line segment.
    // style may be nullptr to use the default (solid, flat caps) stroke.
    void draw_line(float x0, float y0, float x1, float y1,
                   Color c, float width,
                   ID2D1StrokeStyle* style = nullptr);

    // ── Text ────────────────────────────────────────────────────────────────

    // Render a string using an existing DWrite format into layout_box.
    // layout_box uses absolute screen-pixel coordinates (x, y, w, h).
    void draw_text(std::wstring_view     text,
                   IDWriteTextFormat* fmt,
                   const Rect& layout_box,
                   Color                c);

    // ── Clipping ────────────────────────────────────────────────────────────

    // Push an axis-aligned clip rect (ALIASED, i.e. pixel-snapped edges).
    // Every push_clip must be paired with a pop_clip.
    void push_clip(const Rect& r);
    void pop_clip();

    // ── COM-object factories ─────────────────────────────────────────────────
    //
    // All returned objects are owned by the caller.  These are provided so
    // that nodes which need to cache a COM object across frames (e.g.
    // ITextNode caching its IDWriteTextFormat) can create them through the
    // same abstraction rather than reaching into GpuContext directly.
    //

    // Create a solid-colour D2D brush.
    ComPtr<ID2D1SolidColorBrush> make_brush(Color c) const;

    // Create a D2D stroke style from a properties descriptor.
    ComPtr<ID2D1StrokeStyle> make_stroke_style(
        const D2D1_STROKE_STYLE_PROPERTIES& props) const;

    // Create a DWrite text format.
    // Alignment (leading / centered / etc.) must be set by the caller after
    // construction via IDWriteTextFormat::SetTextAlignment().
    ComPtr<IDWriteTextFormat> make_text_format(
        const wchar_t* family,
        float          size,
        bool           bold = false,
        bool           italic = false,
        bool           word_wrap = true) const;

    // Create a DWrite text layout for measurement or hit-testing.
    // max_h may be set to 1e6f when only width / metrics are needed.
    ComPtr<IDWriteTextLayout> make_text_layout(
        const wchar_t* text,
        uint32_t           len,
        IDWriteTextFormat* fmt,
        float              max_w,
        float              max_h) const;

private:
    GpuContext& gpu_;

    ID2D1DeviceContext* dc() const noexcept { return gpu_.d2d_context.Get(); }

    // Convert our Rect {x, y, w, h} into Direct2D's {left, top, right, bottom}.
    static D2D1_RECT_F        to_d2df(const Rect& r)               noexcept;
    static D2D1_ROUNDED_RECT  to_d2d_rr(const Rect& r, float radius) noexcept;
};

} // namespace lintel
