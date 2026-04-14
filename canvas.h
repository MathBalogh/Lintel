#pragma once
// canvas.h
//
// Canvas - the single drawing surface every node renders through.
// Enhanced abstraction of the D2D backend with:
//   • Transform stack (push/pop with hierarchical matrix multiplication)
//   • Gradient brushes for color variation across shapes (linear/radial)
//   • PathBuilder utility for complex general-purpose shapes
//   • Brush overloads on all major fill/stroke methods for performance
//   • Additional shape utilities while staying fully D2D-native and performant

#include "lintel.h"   // Rect, Color, ComPtr
#include "gpu.h"

#include <string_view>
#include <vector>

namespace lintel {

struct GradientStop {
    float offset;   // 0.0f .. 1.0f
    Color color;
};

class Canvas {
public:
    // -- Filled shapes (Color convenience + full Brush overloads) -----------

    void fill_rect(const Rect& r, Color c, float corner_radius = 0.f);
    void fill_rect(const Rect& r, ID2D1Brush* brush, float corner_radius = 0.f);

    void fill_rect_linear_gradient(const Rect& r, const D2D1_POINT_2F& startPoint, const D2D1_POINT_2F& endPoint, const std::vector<GradientStop>& stops, float corner_radius = 0.f);
    void fill_rect_radial_gradient(const Rect& r, const D2D1_POINT_2F& center, float radiusX, float radiusY, const std::vector<GradientStop>& stops, const D2D1_POINT_2F& gradientOriginOffset = D2D1::Point2F(0.f, 0.f), float corner_radius = 0.f);

    void fill_ellipse(float cx, float cy, float rx, float ry, Color c);
    void fill_ellipse(float cx, float cy, float rx, float ry, ID2D1Brush* brush);

    void fill_triangle(float x0, float y0,
                       float x1, float y1,
                       float x2, float y2,
                       Color c);
    void fill_triangle(float x0, float y0,
                       float x1, float y1,
                       float x2, float y2,
                       ID2D1Brush* brush);

    void fill_geometry(ComPtr<ID2D1PathGeometry>& geo, Color c);
    void fill_geometry(ComPtr<ID2D1PathGeometry>& geo, ID2D1Brush* brush);

    // -- Stroked shapes ------------------------------------------------------

    void stroke_rect(const Rect& r, Color c, float stroke_width, float corner_radius = 0.f);
    void stroke_rect(const Rect& r, ID2D1Brush* brush, float stroke_width, float corner_radius = 0.f);

    void draw_line(float x0, float y0, float x1, float y1, Color c, float width, ID2D1StrokeStyle* style = nullptr);
    void draw_line(float x0, float y0, float x1, float y1, ID2D1Brush* brush, float width, ID2D1StrokeStyle* style = nullptr);
    void draw_line_dashed(float x0, float y0, float x1, float y1, Color c, float width);

    void draw_triangle(float x0, float y0,
                       float x1, float y1,
                       float x2, float y2,
                       Color c, float stroke_width);
    void draw_triangle(float x0, float y0,
                       float x1, float y1,
                       float x2, float y2,
                       ID2D1Brush* brush, float stroke_width);

    void stroke_geometry(ComPtr<ID2D1PathGeometry>& geo, Color c,
                         float stroke_width, ID2D1StrokeStyle* style = nullptr);
    void stroke_geometry(ComPtr<ID2D1PathGeometry>& geo, ID2D1Brush* brush,
                         float stroke_width, ID2D1StrokeStyle* style = nullptr);

    // -- Text ----------------------------------------------------------------

    void draw_text(std::wstring_view     text,
                   IDWriteTextFormat* fmt,
                   const Rect& layout_box,
                   Color                c);

    // -- Images --------------------------------------------------------------
    ComPtr<ID2D1Bitmap> load_bitmap(std::string_view path) const;
    void draw_image(ID2D1Bitmap* bmp, const Rect& dest_rect);

    // -- Clipping ------------------------------------------------------------
    void push_clip(const Rect& r);
    void pop_clip();

    // -- Transforms (stack + helpers) ----------------------------------------
    // Push applies an *additional local* transform (current * additional).
    // Perfect for hierarchical node transforms (translate/rotate/scale in local space).
    void push_transform(const D2D1_MATRIX_3X2_F& additional);
    void pop_transform();

    D2D1_MATRIX_3X2_F get_transform() const noexcept;
    void set_transform(const D2D1_MATRIX_3X2_F& transform);

    // Convenience helpers (multiply onto current transform)
    void translate(float dx, float dy);
    void rotate(float angle_degrees, float cx = 0.f, float cy = 0.f);
    void scale(float sx, float sy);

    // -- Gradients (enables different-colored "vertices" via stops) ----------
    ComPtr<ID2D1GradientStopCollection> make_gradient_stops(
        const std::vector<GradientStop>& stops,
        D2D1_GAMMA gamma = D2D1_GAMMA_2_2,
        D2D1_EXTEND_MODE extendMode = D2D1_EXTEND_MODE_CLAMP) const;

    ComPtr<ID2D1LinearGradientBrush> make_linear_gradient_brush(
        const D2D1_POINT_2F& startPoint,
        const D2D1_POINT_2F& endPoint,
        ID2D1GradientStopCollection* stops) const;

    ComPtr<ID2D1RadialGradientBrush> make_radial_gradient_brush(
        const D2D1_POINT_2F& center,
        float radiusX,
        float radiusY,
        const D2D1_POINT_2F& gradientOriginOffset = D2D1::Point2F(0.f, 0.f),
        ID2D1GradientStopCollection* stops = nullptr) const;

    // -- General shape utilities ---------------------------------------------
    
    ComPtr<ID2D1SolidColorBrush> make_brush(Color c) const;
    ComPtr<ID2D1StrokeStyle> make_stroke_style(const D2D1_STROKE_STYLE_PROPERTIES& props) const;
    ComPtr<ID2D1StrokeStyle> make_stroke_style(const D2D1_STROKE_STYLE_PROPERTIES& props, const float* dashes, UINT32 dashesCount) const;

    ComPtr<IDWriteTextFormat> make_text_format(
        const wchar_t* family,
        float          size,
        bool           bold = false,
        bool           italic = false,
        bool           word_wrap = true) const;

    ComPtr<IDWriteTextLayout> make_text_layout(
        const wchar_t* text,
        uint32_t           len,
        IDWriteTextFormat* fmt,
        float              max_w,
        float              max_h) const;

    static Canvas& get();

private:
    ID2D1DeviceContext* dc() const noexcept { return GPU.d2d_context.Get(); }

    static D2D1_RECT_F        to_d2df(const Rect& r)               noexcept;
    static D2D1_ROUNDED_RECT  to_d2d_rr(const Rect& r, float radius) noexcept;

    // Transform stack (managed manually because D2D has no built-in stack)
    std::vector<D2D1_MATRIX_3X2_F> m_transformStack;
};

#define CANVAS (Canvas::get())

} // namespace lintel
