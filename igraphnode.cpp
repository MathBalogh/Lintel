#include "igraphnode.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cwchar>

namespace lintel {

// ===========================================================================
// Tick helpers
// ===========================================================================

float IGraphNode::nice_step(float range, int target) {
    if (range <= 0.f || target <= 0) return 1.f;
    const float rough = range / static_cast<float>(target);
    const float mag = std::pow(10.f, std::floor(std::log10(rough)));
    const float r = rough / mag;
    const float nice = (r < 1.5f) ? 1.f :
        (r < 3.0f) ? 2.f :
        (r < 7.0f) ? 5.f : 10.f;
    return nice * mag;
}

std::vector<float> IGraphNode::make_ticks(float lo, float hi, int target) {
    const float step = nice_step(hi - lo, target);
    const float first = std::ceil(lo / step) * step;

    std::vector<float> ticks;
    ticks.reserve(static_cast<size_t>(target) + 2);

    for (float t = first; t <= hi + step * 0.001f; t += step)
        ticks.push_back(t);

    return ticks;
}

std::wstring IGraphNode::format_tick(float v, float step) {
    if (v == 0.f) v = 0.f; // normalise -0

    int decimals = 0;
    if (step < 1.f) {
        float s = step;
        while (s < 0.9999f && decimals < 8) { s *= 10.f; ++decimals; }
    }

    wchar_t buf[32];
    swprintf(buf, 32, L"%.*f", decimals, static_cast<double>(v));
    return buf;
}

// ===========================================================================
// Bounds
// ===========================================================================

IGraphNode::Bounds IGraphNode::compute_bounds() const {
    float xl = range_x_min, xh = range_x_max;
    float yl = range_y_min, yh = range_y_max;

    const bool auto_x = is_auto(xl) || is_auto(xh);
    const bool auto_y = is_auto(yl) || is_auto(yh);

    if (auto_x || auto_y) {
        constexpr float INF = std::numeric_limits<float>::infinity();
        float dxl = INF, dxh = -INF;
        float dyl = INF, dyh = -INF;

        for (const DataSeries& s : series) {
            for (float x : s.xs) { dxl = std::min(dxl, x); dxh = std::max(dxh, x); }
            for (float y : s.ys) { dyl = std::min(dyl, y); dyh = std::max(dyh, y); }
        }

        if (!std::isfinite(dxl)) { dxl = 0.f; dxh = 1.f; }
        if (!std::isfinite(dyl)) { dyl = 0.f; dyh = 1.f; }
        if (dxl == dxh) { dxl -= 1.f; dxh += 1.f; }
        if (dyl == dyh) { dyl -= 1.f; dyh += 1.f; }

        if (auto_x) { xl = dxl; xh = dxh; }
        if (auto_y) { yl = dyl; yh = dyh; }
    }

    return { xl, xh, yl, yh };
}

// ===========================================================================
// draw_grid
// ===========================================================================

void IGraphNode::draw_grid(
    const Bounds& b,
    float px, float py, float pw, float ph,
    Canvas& canvas) const {
    const Color grid_col = attr.get_or<Color>(
        property::GridColor, Color(0.16f, 0.16f, 0.20f, 1.f));
    const float grid_w = attr.get_or<float>(property::GridWeight, 0.5f);

    const float xl = b.xl, xh = b.xh;
    const float yl = b.yl, yh = b.yh;

    // ── Horizontal grid lines at Y-tick positions ─────────────────────────
    {
        const auto  ticks = make_ticks(yl, yh, 5);
        const float step = ticks.size() > 1
            ? std::abs(ticks[1] - ticks[0]) : 1.f;

        const Color zero_col = Color(
            std::min(1.f, grid_col.r * 2.2f),
            std::min(1.f, grid_col.g * 2.2f),
            std::min(1.f, grid_col.b * 2.4f),
            grid_col.a);

        for (float t : ticks) {
            const float yp = map_to_px(t, yh, yl, py, py + ph);
            if (yp < py - 1.f || yp > py + ph + 1.f) continue;

            const bool is_zero = (std::abs(t) < step * 0.01f);
            canvas.draw_line(px, yp, px + pw, yp,
                             is_zero ? zero_col : grid_col,
                             is_zero ? grid_w * 1.8f : grid_w);
        }
    }

    // ── Vertical grid lines at X-tick positions ───────────────────────────
    {
        const auto  ticks = make_ticks(xl, xh, 6);

        const Color vcol = Color(
            grid_col.r * 0.70f,
            grid_col.g * 0.70f,
            grid_col.b * 0.75f,
            grid_col.a);

        for (float t : ticks) {
            const float xp = map_to_px(t, xl, xh, px, px + pw);
            if (xp < px - 1.f || xp > px + pw + 1.f) continue;
            canvas.draw_line(xp, py, xp, py + ph, vcol, grid_w);
        }
    }
}

// ===========================================================================
// draw_labels
// ===========================================================================

void IGraphNode::draw_labels(
    const Bounds& b,
    float px, float py, float pw, float ph,
    Canvas& canvas) const {
    const Color label_col = attr.get_or<Color>(
        property::LabelColor, Color(0.42f, 0.46f, 0.55f, 1.f));
    const float label_sz = attr.get_or<float>(property::LabelFontSize, 10.5f);

    const float xl = b.xl, xh = b.xh;
    const float yl = b.yl, yh = b.yh;

    auto fmt = canvas.make_text_format(L"Segoe UI", label_sz);
    if (!fmt) return;

    // ── Y-axis labels ─────────────────────────────────────────────────────
    {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const auto  ticks = make_ticks(yl, yh, 5);
        const float step = ticks.size() > 1 ? std::abs(ticks[1] - ticks[0]) : 1.f;
        const float half_h = label_sz + 3.f;
        const float label_right = px - 7.f;

        for (float t : ticks) {
            const float yp = map_to_px(t, yh, yl, py, py + ph);
            if (yp < py - 1.f || yp > py + ph + 1.f) continue;

            const std::wstring text = format_tick(t, step);
            const Rect box{ rect.x, yp - half_h, label_right - rect.x, half_h * 2.f };
            canvas.draw_text(text, fmt.Get(), box, label_col);
        }
    }

    // ── X-axis labels ─────────────────────────────────────────────────────
    {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

        const auto  ticks = make_ticks(xl, xh, 6);
        const float step = ticks.size() > 1 ? std::abs(ticks[1] - ticks[0]) : 1.f;
        const float label_w = 64.f;
        const float top = py + ph + 5.f;
        const float bottom = top + label_sz + 4.f;

        for (float t : ticks) {
            const float xp = map_to_px(t, xl, xh, px, px + pw);
            if (xp < px - 1.f || xp > px + pw + 1.f) continue;

            const std::wstring text = format_tick(t, step);
            const Rect box{ xp - label_w * 0.5f, top, label_w, bottom - top };
            canvas.draw_text(text, fmt.Get(), box, label_col);
        }
    }
}

// ===========================================================================
// draw_series
// ===========================================================================

void IGraphNode::draw_series(
    const Bounds& b,
    float px, float py, float pw, float ph,
    Canvas& canvas) const {
    const float xl = b.xl, xh = b.xh;
    const float yl = b.yl, yh = b.yh;

    canvas.push_clip(Rect{ px, py, pw, ph });

    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties();
    sp.startCap = D2D1_CAP_STYLE_ROUND;
    sp.endCap = D2D1_CAP_STYLE_ROUND;
    sp.lineJoin = D2D1_LINE_JOIN_ROUND;
    auto stroke = canvas.make_stroke_style(sp);

    for (const DataSeries& s : series) {
        if (s.xs.empty() || s.ys.empty()) continue;
        const size_t n = std::min(s.xs.size(), s.ys.size());

        if (n == 1) {
            const float xp = map_to_px(s.xs[0], xl, xh, px, px + pw);
            const float yp = map_to_px(s.ys[0], yh, yl, py, py + ph);
            const float r = s.weight * 2.f;
            canvas.fill_ellipse(xp, yp, r, r, s.color);
            continue;
        }

        for (size_t i = 1; i < n; ++i) {
            const float x0 = map_to_px(s.xs[i - 1], xl, xh, px, px + pw);
            const float y0 = map_to_px(s.ys[i - 1], yh, yl, py, py + ph);
            const float x1 = map_to_px(s.xs[i], xl, xh, px, px + pw);
            const float y1 = map_to_px(s.ys[i], yh, yl, py, py + ph);
            canvas.draw_line(x0, y0, x1, y1, s.color, s.weight, stroke.Get());
        }
    }

    canvas.pop_clip();
}

// ===========================================================================
// IGraphNode::draw
// ===========================================================================

void IGraphNode::draw(Node& self, Canvas& canvas) {
    draw_default(canvas);

    const float px = rect.x + plot_left;
    const float py = rect.y + plot_top;
    const float pw = rect.w - plot_left - plot_right;
    const float ph = rect.h - plot_top - plot_bottom;

    if (pw <= 0.f || ph <= 0.f) return;

    const Bounds b = compute_bounds();

    draw_grid(b, px, py, pw, ph, canvas);
    draw_labels(b, px, py, pw, ph, canvas);
    draw_series(b, px, py, pw, ph, canvas);

    for (Node& child : children)
        child.handle<INode>()->draw(child, canvas);
}

// ===========================================================================
// GraphNode public API
// ===========================================================================

GraphNode::GraphNode(): Node(nullptr) {
    impl_allocate<IGraphNode>();
    handle<IGraphNode>()->attr.set(property::Share, 1.f); // fill parent by default
}

GraphNode& GraphNode::push_series(
    std::wstring_view  name,
    std::vector<float> xs,
    std::vector<float> ys,
    Color              color,
    float              weight) {
    IGraphNode& g = *handle<IGraphNode>();
    DataSeries  s;
    s.name = name;
    s.xs = std::move(xs);
    s.ys = std::move(ys);
    s.color = color;
    s.weight = weight;
    g.series.push_back(std::move(s));
    return *this;
}

GraphNode& GraphNode::x_range(float lo, float hi) {
    IGraphNode& g = *handle<IGraphNode>();
    g.range_x_min = lo;
    g.range_x_max = hi;
    return *this;
}

GraphNode& GraphNode::y_range(float lo, float hi) {
    IGraphNode& g = *handle<IGraphNode>();
    g.range_y_min = lo;
    g.range_y_max = hi;
    return *this;
}

GraphNode& GraphNode::clear_series() {
    handle<IGraphNode>()->series.clear();
    return *this;
}

} // namespace lintel
