#include "canvas.h"

namespace lintel {

Canvas& Canvas::get() {
    static Canvas instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Private geometry converters
// ---------------------------------------------------------------------------

D2D1_RECT_F Canvas::to_d2df(const Rect& r) noexcept {
    return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h);
}

D2D1_ROUNDED_RECT Canvas::to_d2d_rr(const Rect& r, float radius) noexcept {
    return D2D1::RoundedRect(to_d2df(r), radius, radius);
}

// ---------------------------------------------------------------------------
// Filled shapes
// ---------------------------------------------------------------------------

void Canvas::fill_rect(const Rect& r, Color c, float corner_radius) {
    auto brush = make_brush(c);
    if (!brush) return;
    if (corner_radius > 0.f)
        dc()->FillRoundedRectangle(to_d2d_rr(r, corner_radius), brush.Get());
    else
        dc()->FillRectangle(to_d2df(r), brush.Get());
}

void Canvas::fill_ellipse(float cx, float cy, float rx, float ry, Color c) {
    auto brush = make_brush(c);
    if (!brush) return;
    dc()->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), brush.Get());
}

// ---------------------------------------------------------------------------
// Stroked shapes
// ---------------------------------------------------------------------------

void Canvas::stroke_rect(const Rect& r, Color c, float stroke_width, float corner_radius) {
    if (stroke_width <= 0.f) return;
    auto brush = make_brush(c);
    if (!brush) return;
    if (corner_radius > 0.f)
        dc()->DrawRoundedRectangle(to_d2d_rr(r, corner_radius),
                                   brush.Get(), stroke_width);
    else
        dc()->DrawRectangle(to_d2df(r), brush.Get(), stroke_width);
}

void Canvas::draw_line(float x0, float y0, float x1, float y1, Color c, float width, ID2D1StrokeStyle* style) {
    auto brush = make_brush(c);
    if (!brush) return;
    dc()->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1),
                   brush.Get(), width, style);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

void Canvas::draw_text(std::wstring_view text, IDWriteTextFormat* fmt,
                       const Rect& layout_box, Color c) {
    if (text.empty() || !fmt) return;
    auto brush = make_brush(c);
    if (!brush) return;
    dc()->DrawText(text.data(), static_cast<UINT32>(text.size()), fmt, to_d2df(layout_box), brush.Get());
}

// ---------------------------------------------------------------------------
// WIC
// ---------------------------------------------------------------------------

ComPtr<ID2D1Bitmap> Canvas::load_bitmap(std::string_view path) const {
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

void Canvas::draw_image(ID2D1Bitmap* bmp, const Rect& dest_rect) {
    if (!bmp) return;
    dc()->DrawBitmap(bmp, to_d2df(dest_rect), 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
}


// ---------------------------------------------------------------------------
// Clipping
// ---------------------------------------------------------------------------

void Canvas::push_clip(const Rect& r) {
    dc()->PushAxisAlignedClip(to_d2df(r), D2D1_ANTIALIAS_MODE_ALIASED);
}

void Canvas::pop_clip() {
    dc()->PopAxisAlignedClip();
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------

ComPtr<ID2D1SolidColorBrush> Canvas::make_brush(Color c) const {
    ComPtr<ID2D1SolidColorBrush> b;
    if (dc())
        dc()->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, c.a), &b);
    return b;
}

ComPtr<ID2D1StrokeStyle> Canvas::make_stroke_style(
    const D2D1_STROKE_STYLE_PROPERTIES& props) const {
    ComPtr<ID2D1StrokeStyle> style;
    if (GPU.d2d_factory)
        GPU.d2d_factory->CreateStrokeStyle(props, nullptr, 0, &style);
    return style;
}

ComPtr<IDWriteTextFormat> Canvas::make_text_format(
    const wchar_t* family, float size,
    bool bold, bool italic, bool word_wrap) const {
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
    IDWriteTextFormat* fmt, float max_w, float max_h) const {
    if (!GPU.dwrite_factory || !fmt || len == 0) return {};

    ComPtr<IDWriteTextLayout> layout;
    GPU.dwrite_factory->CreateTextLayout(text, len, fmt, max_w, max_h, &layout);
    return layout;
}

} // namespace lintel
