#pragma once
// canvas.h
//
// Canvas - the single drawing surface every node renders through.
//

#include "lintel.h"   // Rect, Color, ComPtr
#include "gpu.h"

#include <string_view>

namespace lintel {

class Canvas {
public:
    // -- Filled shapes -------------------------------------------------------

    // Fill a rectangle with an optional uniform corner radius.
    void fill_rect(const Rect& r, Color c, float corner_radius = 0.f);

    // Fill an axis-aligned ellipse whose centre is (cx, cy).
    void fill_ellipse(float cx, float cy, float rx, float ry, Color c);

    // Fill triangle
    void fill_triangle(float x0, float y0,
                       float x1, float y1,
                       float x2, float y2,
                       Color c);

    void fill_geometry(ComPtr<ID2D1PathGeometry>& geo, Color c);

    // -- Stroked shapes ------------------------------------------------------

    // Draw a rectangle outline.  stroke_width > 0 required.
    void stroke_rect(const Rect& r, Color c, float stroke_width,
                     float corner_radius = 0.f);

    // Draw a single line segment.
    // style may be nullptr to use the default (solid, flat caps) stroke.
    void draw_line(float x0, float y0, float x1, float y1,
                   Color c, float width,
                   ID2D1StrokeStyle* style = nullptr);

    // Draw triangle outline
    void draw_triangle(float x0, float y0,
                       float x1, float y1,
                       float x2, float y2,
                       Color c, float stroke_width);

    // -- Text ----------------------------------------------------------------

    // Render a string using an existing DWrite format into layout_box.
    // layout_box uses absolute screen-pixel coordinates (x, y, w, h).
    void draw_text(std::wstring_view     text,
                   IDWriteTextFormat* fmt,
                   const Rect& layout_box,
                   Color                c);

    // -- Images ----------------------------------------------------------
    /**
     * Load a bitmap from disk (PNG, JPG, BMP, … - any WIC-supported format).
     * Returns empty ComPtr on any failure.  The bitmap is created on the
     * D2D device and can be cached safely on the node.
     */
    ComPtr<ID2D1Bitmap> load_bitmap(std::string_view path) const;

    /**
     * Draw bitmap stretched to fill dest_rect (source = full bitmap).
     */
    void draw_image(ID2D1Bitmap* bmp, const Rect& dest_rect);

    // -- Clipping ------------------------------------------------------------

    // Push an axis-aligned clip rect (ALIASED, i.e. pixel-snapped edges).
    // Every push_clip must be paired with a pop_clip.
    void push_clip(const Rect& r);
    void pop_clip();

    // -- COM-object factories -------------------------------------------------
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

    static Canvas& get();
private:
    ID2D1DeviceContext* dc() const noexcept { return GPU.d2d_context.Get(); }

    // Convert our Rect {x, y, w, h} into Direct2D's {left, top, right, bottom}.
    static D2D1_RECT_F        to_d2df(const Rect& r)               noexcept;
    static D2D1_ROUNDED_RECT  to_d2d_rr(const Rect& r, float radius) noexcept;
};

#define CANVAS (Canvas::get())

} // namespace lintel
