#include "igraphnode.h"
#include "plot_common.h"

#include <cmath>
#include <limits>
#include <algorithm>

namespace lintel {

// ---------------------------------------------------------------------------
// GraphNode public API
// ---------------------------------------------------------------------------

GraphNode::GraphNode(): Node(nullptr) {
    allocate<IGraphNode>();
    handle<IGraphNode>()->props.set(Key::Share, 0.f);

    on(Event::Click, [] (NodeView weak) {
        auto self = weak.handle<IGraphNode>();

        float mx = self->doc_->input.mouse_screen_x;
        float my = self->doc_->input.mouse_screen_y;

        if (self->handle_click(mx, my))
            self->self_dirty();
    });
}

void GraphNode::remove_series(const std::string& name) {
    IGraphNode& n = *handle<IGraphNode>();
    n.series.erase(name);
    n.props.make_dirty();
}

DataSeries& GraphNode::series(const std::string& name) {
    IGraphNode& n = *handle<IGraphNode>();
    auto it = n.series.find(name);
    if (it == n.series.end()) {
        DataSeries ds;
        ds.name = to_wstring(name);
        ds.color = Color::hsl(static_cast<float>(n.series.size() * 137 % 360), 0.75f, 0.65f);
        it = n.series.emplace(name, std::move(ds)).first;
    }
    return it->second;
}

GraphNode& GraphNode::x_range(float lo, float hi) {
    IGraphNode& n = *handle<IGraphNode>();
    n.range_x_min = UIValue::px(lo);
    n.range_x_max = UIValue::px(hi);
    n.props.make_dirty();
    return *this;
}

GraphNode& GraphNode::y_range(float lo, float hi) {
    IGraphNode& n = *handle<IGraphNode>();
    n.range_y_min = UIValue::px(lo);
    n.range_y_max = UIValue::px(hi);
    n.props.make_dirty();
    return *this;
}


IGraphNode::IGraphNode() {
    fmt_ = CANVAS.make_text_format(L"Segoe UI", 11.f);
}

// ---------------------------------------------------------------------------
// IGraphNode::draw
// ---------------------------------------------------------------------------

void IGraphNode::draw(Node& self, Canvas& canvas) {
    draw_default(canvas);

    const float cx = content_x();
    const float cy = content_y();
    const float cw = inner_w();
    const float ch = inner_h();
    if (cw <= 0.f || ch <= 0.f) return;

    // Toggle pill strip sits at the top of the content rect.
    draw_toggle_pills(cx, cy, cw, canvas);

    // Plot rect sits below the pill strip.
    const float px = cx + plot_left;
    const float py = cy + pill_strip_h + plot_top;
    const float pw = cw - plot_left - plot_right;
    const float ph = ch - pill_strip_h - plot_top - plot_bottom;
    if (pw <= 0.f || ph <= 0.f) return;

    const PlotBounds b = compute_plot_bounds(series,
                                             range_x_min, range_x_max,
                                             range_y_min, range_y_max);

    // Clip strictly to the plot rect — labels and pills live outside it.
    canvas.push_clip({ px, py, pw, ph });
    draw_plot_grid(b, px, py, pw, ph, canvas);
    draw_series(b, px, py, pw, ph, canvas);
    canvas.pop_clip();

    // Labels are drawn unclipped so they can bleed into the inset margins.
    draw_plot_labels(b, px, py, pw, ph, canvas);

    // Hover readout — also unclipped so the label never gets cut off at edges.
    const float mx = self->doc_->input.mouse_screen_x;
    const float my = self->doc_->input.mouse_screen_y;
    if (mx >= px && mx <= px + pw && my >= py && my <= py + ph)
        draw_hover_label(b, px, py, pw, ph, mx, my, canvas);
}

// ---------------------------------------------------------------------------
// draw_series
// ---------------------------------------------------------------------------

void IGraphNode::draw_series(const PlotBounds& b,
                             float px, float py, float pw, float ph,
                             Canvas& canvas) const {
    for (const auto& [name, s] : series) {
        if (!s.display || s.xs.empty() || s.ys.size() != s.xs.size()) continue;

        for (size_t i = 1; i < s.xs.size(); ++i) {
            const float px0 = map_to_px(s.xs[i - 1], b.xl, b.xh, px, px + pw);
            const float py0 = map_to_px(s.ys[i - 1], b.yl, b.yh, py + ph, py);
            const float px1 = map_to_px(s.xs[i], b.xl, b.xh, px, px + pw);
            const float py1 = map_to_px(s.ys[i], b.yl, b.yh, py + ph, py);
            canvas.draw_line(px0, py0, px1, py1, s.color, s.weight);
        }
    }
}

// ---------------------------------------------------------------------------
// draw_toggle_pills
// ---------------------------------------------------------------------------

void IGraphNode::draw_toggle_pills(float cx, float cy, float cw,
                                   Canvas& canvas) const {
    pill_rects_.clear();

    const float strip_y = cy;
    const float strip_cy = cy + pill_strip_h * 0.5f;   // vertical centre

    float cursor = cx + plot_left;   // start flush with plot left edge

    for (const auto& [key, s] : series) {
        // Measure approximate text width: ~7 px per character at default size.
        const float text_w = static_cast<float>(s.name.size()) * 7.f;
        const float pill_w = text_w + pill_pad_x * 2.f;
        const float pill_h = 14.f + pill_pad_y * 2.f;
        const float pill_y = strip_cy - pill_h * 0.5f;

        const bool on = s.display && s.color.a > 0.f;

        // Background
        const Color bg = on ? Color(s.color.r, s.color.g, s.color.b, 0.18f)
            : Color(0.f, 0.f, 0.f, 0.f);
        const Color border = on ? Color(s.color.r, s.color.g, s.color.b, 0.55f)
            : Color(1.f, 1.f, 1.f, 0.12f);
        const Color text_c = on ? Color(s.color.r, s.color.g, s.color.b, 1.f)
            : Color(1.f, 1.f, 1.f, 0.28f);

        canvas.fill_rect({ cursor, pill_y, pill_w, pill_h }, bg, 3.f);
        canvas.stroke_rect({ cursor, pill_y, pill_w, pill_h }, border, 3.f, 1.f);

        canvas.draw_text(s.name, fmt_.Get(),
                         { cursor + pill_pad_x, pill_y, text_w, pill_h },
                         text_c);

        pill_rects_.push_back({ key, cursor, pill_y, pill_w, pill_h });
        cursor += pill_w + pill_gap;
    }
}

// ---------------------------------------------------------------------------
// draw_hover_label
// ---------------------------------------------------------------------------

void IGraphNode::draw_hover_label(const PlotBounds& b,
                                  float px, float py, float pw, float ph,
                                  float mx, float my,
                                  Canvas& canvas) const {
    // Map mouse pixel position back to data coordinates.
    const float data_x = b.xl + (mx - px) / pw * (b.xh - b.xl);
    const float data_y = b.yl + (py + ph - my) / ph * (b.yh - b.yl);

    // Find the nearest point across all visible series.
    float   best_dist_sq = std::numeric_limits<float>::max();
    float   best_y = 0.f;
    bool    found = false;

    for (const auto& [name, s] : series) {
        if (!s.display || s.color.a < 0.01f || s.xs.empty()) continue;

        // Binary search to the closest x in this series.
        const auto it = std::lower_bound(s.xs.begin(), s.xs.end(), data_x);
        for (auto candidate = it; candidate != s.xs.end()
             && candidate != s.xs.begin() + 1; --candidate) {
            const size_t idx = static_cast<size_t>(candidate - s.xs.begin());
            if (idx >= s.ys.size()) continue;

            // Normalise distance in pixel space so both axes are comparable.
            const float dx = map_to_px(s.xs[idx], b.xl, b.xh, 0.f, pw) -
                map_to_px(data_x, b.xl, b.xh, 0.f, pw);
            const float dy = map_to_px(s.ys[idx], b.yl, b.yh, ph, 0.f) -
                map_to_px(data_y, b.yl, b.yh, ph, 0.f);
            const float d2 = dx * dx + dy * dy;
            if (d2 < best_dist_sq) {
                best_dist_sq = d2;
                best_y = s.ys[idx];
                found = true;
            }
        }
    }

    if (!found) return;

    // Only show when the cursor is within ~24 px of a point.
    if (std::sqrt(best_dist_sq) > 24.f) return;

    // Format to 2 decimal places.
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%.2f", best_y);

    // Offset the label slightly above-right of the cursor so it never
    // sits under the hotspot and obscures the data line.
    const float lx = mx + 10.f;
    const float ly = my - 14.f;

    const Color bg_c = Color(0.06f, 0.06f, 0.06f, 0.82f);
    const Color text_c = Color(1.f, 1.f, 1.f, 0.90f);

    const float font_size = 11.0f;
    const wchar_t* font_family = L"Segoe UI";
    auto fmt = canvas.make_text_format(font_family, font_size, false, false, false);

    const float label_w = static_cast<float>(std::wcslen(buf)) * 7.f + 10.f;
    canvas.fill_rect({ lx - 5.f, ly - 11.f, label_w, 18.f }, bg_c, 3.f);
    canvas.draw_text(buf, fmt_.Get(),
                     { lx, ly - 11.f, label_w, 18.f },
                     text_c);
}

bool IGraphNode::handle_click(float mx, float my) {
    for (const auto& pr : pill_rects_) {
        if (mx >= pr.x && mx <= pr.x + pr.w &&
            my >= pr.y && my <= pr.y + pr.h)
            return toggle_series(pr.key);
    }
    return false;
}

} // namespace lintel
