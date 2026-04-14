#include "canvas.h"

namespace lintel {

Canvas& Canvas::get() {
    static Canvas instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Private geometry converters (unchanged)
// ---------------------------------------------------------------------------

D2D1_RECT_F Canvas::to_d2df(const Rect& r) noexcept {
    return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h);
}

D2D1_ROUNDED_RECT Canvas::to_d2d_rr(const Rect& r, float radius) noexcept {
    return D2D1::RoundedRect(to_d2df(r), radius, radius);
}

// ---------------------------------------------------------------------------
// Filled shapes (Brush core + Color convenience)
// ---------------------------------------------------------------------------

void Canvas::fill_rect(const Rect& r, Color c, float corner_radius) {
    auto brush = make_brush(c);
    if (brush) fill_rect(r, brush.Get(), corner_radius);
}

void Canvas::fill_rect(const Rect& r, ID2D1Brush* brush, float corner_radius) {
    if (!brush || !dc()) return;
    if (corner_radius > 0.f)
        dc()->FillRoundedRectangle(to_d2d_rr(r, corner_radius), brush);
    else
        dc()->FillRectangle(to_d2df(r), brush);
}

void Canvas::fill_rect_linear_gradient(const Rect& r, const D2D1_POINT_2F& startPoint, const D2D1_POINT_2F& endPoint, const std::vector<GradientStop>& stops, float corner_radius) {
    auto stop_coll = make_gradient_stops(stops);
    if (!stop_coll) return;
    auto brush = make_linear_gradient_brush(startPoint, endPoint, stop_coll.Get());
    if (brush) fill_rect(r, brush.Get(), corner_radius);
}

void Canvas::fill_rect_radial_gradient(const Rect& r, const D2D1_POINT_2F& center, float radiusX, float radiusY, const std::vector<GradientStop>& stops, const D2D1_POINT_2F& gradientOriginOffset, float corner_radius) {
    auto stop_coll = make_gradient_stops(stops);
    if (!stop_coll) return;
    auto brush = make_radial_gradient_brush(center, radiusX, radiusY, gradientOriginOffset, stop_coll.Get());
    if (brush) fill_rect(r, brush.Get(), corner_radius);
}

void Canvas::fill_ellipse(float cx, float cy, float rx, float ry, Color c) {
    auto brush = make_brush(c);
    if (brush) fill_ellipse(cx, cy, rx, ry, brush.Get());
}

void Canvas::fill_ellipse(float cx, float cy, float rx, float ry, ID2D1Brush* brush) {
    if (!brush || !dc()) return;
    dc()->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), brush);
}

void Canvas::fill_triangle(float x0, float y0, float x1, float y1,
                           float x2, float y2, Color c) {
    auto brush = make_brush(c);
    if (brush) fill_triangle(x0, y0, x1, y1, x2, y2, brush.Get());
}

void Canvas::fill_triangle(float x0, float y0, float x1, float y1,
                           float x2, float y2, ID2D1Brush* brush) {
    if (!brush || !dc()) return;

    ComPtr<ID2D1PathGeometry> geo;
    GPU.d2d_factory->CreatePathGeometry(&geo);

    ComPtr<ID2D1GeometrySink> sink;
    geo->Open(&sink);

    sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(x1, y1));
    sink->AddLine(D2D1::Point2F(x2, y2));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

    sink->Close();

    dc()->FillGeometry(geo.Get(), brush);
}

void Canvas::fill_geometry(ComPtr<ID2D1PathGeometry>& geo, Color c) {
    auto brush = make_brush(c);
    if (brush) fill_geometry(geo, brush.Get());
}

void Canvas::fill_geometry(ComPtr<ID2D1PathGeometry>& geo, ID2D1Brush* brush) {
    if (!geo || !brush || !dc()) return;
    dc()->FillGeometry(geo.Get(), brush);
}

// ---------------------------------------------------------------------------
// Stroked shapes
// ---------------------------------------------------------------------------

void Canvas::stroke_rect(const Rect& r, Color c, float stroke_width, float corner_radius) {
    if (stroke_width <= 0.f) return;
    auto brush = make_brush(c);
    if (brush) stroke_rect(r, brush.Get(), stroke_width, corner_radius);
}

void Canvas::stroke_rect(const Rect& r, ID2D1Brush* brush, float stroke_width, float corner_radius) {
    if (stroke_width <= 0.f || !brush || !dc()) return;
    if (corner_radius > 0.f)
        dc()->DrawRoundedRectangle(to_d2d_rr(r, corner_radius), brush, stroke_width);
    else
        dc()->DrawRectangle(to_d2df(r), brush, stroke_width);
}

void Canvas::draw_line(float x0, float y0, float x1, float y1, Color c, float width, ID2D1StrokeStyle* style) {
    auto brush = make_brush(c);
    if (brush) draw_line(x0, y0, x1, y1, brush.Get(), width, style);
}

void Canvas::draw_line(float x0, float y0, float x1, float y1, ID2D1Brush* brush, float width, ID2D1StrokeStyle* style) {
    if (!brush || !dc()) return;
    dc()->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1),
                   brush, width, style);
}

void Canvas::draw_line_dashed(float x0, float y0, float x1, float y1, Color c, float width) {
    if (width <= 0.f) return;
    auto brush = make_brush(c);
    if (!brush || !dc()) return;

    const float dashes[] = { 5.0f, 3.0f };

    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_FLAT,      // start cap
        D2D1_CAP_STYLE_FLAT,      // end cap
        D2D1_CAP_STYLE_ROUND,     // dash caps (rounded for polished look)
        D2D1_LINE_JOIN_MITER,
        10.0f,                    // miter limit
        D2D1_DASH_STYLE_CUSTOM,
        0.0f                      // dash offset
    );

    auto style = make_stroke_style(props, dashes, 2);
    draw_line(x0, y0, x1, y1, brush.Get(), width, style.Get());
}

void Canvas::draw_triangle(float x0, float y0, float x1, float y1,
                           float x2, float y2, Color c, float stroke_width) {
    if (stroke_width <= 0.f) return;
    auto brush = make_brush(c);
    if (brush) draw_triangle(x0, y0, x1, y1, x2, y2, brush.Get(), stroke_width);
}

void Canvas::draw_triangle(float x0, float y0, float x1, float y1,
                           float x2, float y2, ID2D1Brush* brush, float stroke_width) {
    if (stroke_width <= 0.f || !brush || !dc()) return;

    ComPtr<ID2D1PathGeometry> geo;
    GPU.d2d_factory->CreatePathGeometry(&geo);

    ComPtr<ID2D1GeometrySink> sink;
    geo->Open(&sink);

    sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine(D2D1::Point2F(x1, y1));
    sink->AddLine(D2D1::Point2F(x2, y2));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

    sink->Close();

    dc()->DrawGeometry(geo.Get(), brush, stroke_width);
}

void Canvas::stroke_geometry(ComPtr<ID2D1PathGeometry>& geo, Color c,
                             float stroke_width, ID2D1StrokeStyle* style) {
    auto brush = make_brush(c);
    if (brush) stroke_geometry(geo, brush.Get(), stroke_width, style);
}

void Canvas::stroke_geometry(ComPtr<ID2D1PathGeometry>& geo, ID2D1Brush* brush,
                             float stroke_width, ID2D1StrokeStyle* style) {
    if (!geo || !brush || stroke_width <= 0.f || !dc()) return;
    dc()->DrawGeometry(geo.Get(), brush, stroke_width, style);
}

// ---------------------------------------------------------------------------
// Text (unchanged)
// ---------------------------------------------------------------------------

void Canvas::draw_text(std::wstring_view text, IDWriteTextFormat* fmt,
                       const Rect& layout_box, Color c) {
    if (text.empty() || !fmt) return;
    auto brush = make_brush(c);
    if (!brush) return;
    dc()->DrawText(text.data(), static_cast<UINT32>(text.size()), fmt,
                   to_d2df(layout_box), brush.Get());
}

// ---------------------------------------------------------------------------
// Images, Clipping (unchanged)
// ---------------------------------------------------------------------------

ComPtr<ID2D1Bitmap> Canvas::load_bitmap(std::string_view path) const { /* unchanged */
    if (path.empty()) return {};

    std::wstring wpath(path.begin(), path.end());

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(GPU.wic_factory->CreateDecoderFromFilename(
        wpath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder)))
        return {};

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return {};

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(GPU.wic_factory->CreateFormatConverter(&converter))) return {};

    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut)))
        return {};

    ComPtr<ID2D1Bitmap> bmp;
    if (FAILED(dc()->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bmp)))
        return {};

    return bmp;
}

void Canvas::draw_image(ID2D1Bitmap* bmp, const Rect& dest_rect) { /* unchanged */
    if (!bmp) return;
    dc()->DrawBitmap(bmp, to_d2df(dest_rect), 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
}

void Canvas::push_clip(const Rect& r) { /* unchanged */
    dc()->PushAxisAlignedClip(to_d2df(r), D2D1_ANTIALIAS_MODE_ALIASED);
}

void Canvas::pop_clip() { /* unchanged */
    dc()->PopAxisAlignedClip();
}

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------

void Canvas::push_transform(const D2D1_MATRIX_3X2_F& additional) {
    if (!dc()) return;
    D2D1_MATRIX_3X2_F current;
    dc()->GetTransform(&current);
    m_transformStack.push_back(current);

    // Local transform semantics: new = current * additional
    D2D1_MATRIX_3X2_F combined = current * additional;
    dc()->SetTransform(combined);
}

void Canvas::pop_transform() {
    if (!dc() || m_transformStack.empty()) return;
    dc()->SetTransform(m_transformStack.back());
    m_transformStack.pop_back();
}

D2D1_MATRIX_3X2_F Canvas::get_transform() const noexcept {
    D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Identity();
    if (dc()) dc()->GetTransform(&m);
    return m;
}

void Canvas::set_transform(const D2D1_MATRIX_3X2_F& transform) {
    if (dc()) dc()->SetTransform(transform);
}

void Canvas::translate(float dx, float dy) {
    if (!dc()) return;
    auto current = get_transform();
    auto t = D2D1::Matrix3x2F::Translation(dx, dy);
    set_transform(current * t);
}

void Canvas::rotate(float angle_degrees, float cx, float cy) {
    if (!dc()) return;
    auto current = get_transform();
    auto r = D2D1::Matrix3x2F::Rotation(angle_degrees, D2D1::Point2F(cx, cy));
    set_transform(current * r);
}

void Canvas::scale(float sx, float sy) {
    if (!dc()) return;
    auto current = get_transform();
    auto s = D2D1::Matrix3x2F::Scale(sx, sy);
    set_transform(current * s);
}

// ---------------------------------------------------------------------------
// Gradients (different-colored vertices via stops)
// ---------------------------------------------------------------------------

ComPtr<ID2D1GradientStopCollection> Canvas::make_gradient_stops(
    const std::vector<GradientStop>& stops,
    D2D1_GAMMA gamma,
    D2D1_EXTEND_MODE extendMode) const {

    if (stops.empty() || !GPU.d2d_factory) return {};

    std::vector<D2D1_GRADIENT_STOP> d2dStops(stops.size());
    for (size_t i = 0; i < stops.size(); ++i) {
        d2dStops[i].position = stops[i].offset;
        d2dStops[i].color = D2D1::ColorF(
            stops[i].color.r, stops[i].color.g,
            stops[i].color.b, stops[i].color.a);
    }

    ComPtr<ID2D1GradientStopCollection> collection;
    GPU.d2d_context->CreateGradientStopCollection(
        d2dStops.data(), static_cast<UINT32>(d2dStops.size()),
        gamma, extendMode, &collection);
    return collection;
}

ComPtr<ID2D1LinearGradientBrush> Canvas::make_linear_gradient_brush(
    const D2D1_POINT_2F& startPoint,
    const D2D1_POINT_2F& endPoint,
    ID2D1GradientStopCollection* stops) const {

    if (!stops || !dc()) return {};
    ComPtr<ID2D1LinearGradientBrush> brush;
    dc()->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(startPoint, endPoint),
        D2D1::BrushProperties(),
        stops, &brush);
    return brush;
}

ComPtr<ID2D1RadialGradientBrush> Canvas::make_radial_gradient_brush(
    const D2D1_POINT_2F& center,
    float radiusX,
    float radiusY,
    const D2D1_POINT_2F& gradientOriginOffset,
    ID2D1GradientStopCollection* stops) const {

    if (!stops || !dc()) return {};
    ComPtr<ID2D1RadialGradientBrush> brush;
    dc()->CreateRadialGradientBrush(
        D2D1::RadialGradientBrushProperties(center, gradientOriginOffset, radiusX, radiusY),
        D2D1::BrushProperties(),
        stops, &brush);
    return brush;
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------

ComPtr<ID2D1SolidColorBrush> Canvas::make_brush(Color c) const { /* unchanged */
    ComPtr<ID2D1SolidColorBrush> b;
    if (dc())
        dc()->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, c.a), &b);
    return b;
}

ComPtr<ID2D1StrokeStyle> Canvas::make_stroke_style(
    const D2D1_STROKE_STYLE_PROPERTIES& props) const {
    return make_stroke_style(props, nullptr, 0);
}
ComPtr<ID2D1StrokeStyle> Canvas::make_stroke_style(
    const D2D1_STROKE_STYLE_PROPERTIES& props,
    const float* dashes,
    UINT32 dashesCount) const {
    ComPtr<ID2D1StrokeStyle> style;
    if (GPU.d2d_factory) GPU.d2d_factory->CreateStrokeStyle(props, dashes, dashesCount, &style);
    return style;
}

ComPtr<IDWriteTextFormat> Canvas::make_text_format(
    const wchar_t* family, float size,
    bool bold, bool italic, bool word_wrap) const { /* unchanged */
    if (!GPU.dwrite_factory) return {};

    ComPtr<IDWriteTextFormat> fmt;
    GPU.dwrite_factory->CreateTextFormat(
        family, nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR,
        italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        size, L"", &fmt);

    if (fmt)
        fmt->SetWordWrapping(word_wrap ? DWRITE_WORD_WRAPPING_WRAP
                             : DWRITE_WORD_WRAPPING_NO_WRAP);
    return fmt;
}

ComPtr<IDWriteTextLayout> Canvas::make_text_layout(
    const wchar_t* text, uint32_t len,
    IDWriteTextFormat* fmt, float max_w, float max_h) const { /* unchanged */
    if (!GPU.dwrite_factory || !fmt || len == 0) return {};

    ComPtr<IDWriteTextLayout> layout;
    GPU.dwrite_factory->CreateTextLayout(text, len, fmt, max_w, max_h, &layout);
    return layout;
}

} // namespace lintel
