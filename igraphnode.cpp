#include "igraphnode.h"
#include "plot_common.h"

#include <cmath>
#include <limits>
#include <algorithm>
#include <numeric>

namespace lintel {

// ---------------------------------------------------------------------------
// DataSeries helpers
// ---------------------------------------------------------------------------

void DataSeries::sort_by_x() {
    if (sorted || xs.empty()) return;

    // Build an index permutation, sort by xs value, then apply.
    std::vector<size_t> idx(xs.size());
    std::iota(idx.begin(), idx.end(), 0u);
    std::sort(idx.begin(), idx.end(),
              [&] (size_t a, size_t b) { return xs[a] < xs[b]; });

    std::vector<float> sx(xs.size()), sy(ys.size());
    for (size_t i = 0; i < idx.size(); ++i) {
        sx[i] = xs[idx[i]];
        sy[i] = ys[idx[i]];
    }
    xs = std::move(sx);
    ys = std::move(sy);
    sorted = true;
}

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
    n.cache.erase(name);
    n.props.make_dirty();
}

DataSeries& GraphNode::series(const std::string& name) {
    IGraphNode& n = *handle<IGraphNode>();
    auto it = n.series.find(name);
    if (it == n.series.end()) {
        DataSeries ds;
        ds.name = to_wstring(name);
        ds.color = Color::hsl(
            static_cast<float>(n.series.size() * 137 % 360), 0.75f, 0.65f);
        it = n.series.emplace(name, std::move(ds)).first;
        n.cache[name].dirty = true;
    }
    return it->second;
}

DataSeries& GraphNode::line_series(const std::string& name) {
    DataSeries& s = series(name);
    s.kind = SeriesKind::Line;
    return s;
}

DataSeries& GraphNode::scatter_series(const std::string& name) {
    DataSeries& s = series(name);
    s.kind = SeriesKind::Scatter;
    return s;
}

GraphNode& GraphNode::add_marker(Marker m) {
    handle<IGraphNode>()->markers.push_back(std::move(m));
    handle<IGraphNode>()->props.make_dirty();
    return *this;
}

GraphNode& GraphNode::remove_markers() {
    handle<IGraphNode>()->markers.clear();
    handle<IGraphNode>()->props.make_dirty();
    return *this;
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

// ---------------------------------------------------------------------------
// IGraphNode
// ---------------------------------------------------------------------------

IGraphNode::IGraphNode() {
    fmt_ = CANVAS.make_text_format(L"Segoe UI", 11.f);
}

// ---------------------------------------------------------------------------
// compute_bounds  (pure, no side-effects)
// ---------------------------------------------------------------------------

PlotBounds IGraphNode::compute_bounds() const {
    return compute_plot_bounds(series,
                               range_x_min, range_x_max,
                               range_y_min, range_y_max);
}

// ---------------------------------------------------------------------------
// rebuild_cache
//
// Maps every data point in every series to pixel space once per frame
// (only when dirty). Subsequent draw calls read pre-projected coordinates.
// ---------------------------------------------------------------------------

void IGraphNode::rebuild_cache(const PlotBounds& b, float px, float py, float pw, float ph) {
    for (const auto& [name, s] : series) {
        SeriesCache& c = cache[name];
        const size_t n = s.xs.size();
        c.screen_xs.resize(n);
        c.screen_ys.resize(n);

        for (size_t i = 0; i < n; ++i) {
            c.screen_xs[i] = map_to_px(s.xs[i], b.xl, b.xh, px, px + pw);
            c.screen_ys[i] = map_to_px(s.ys[i], b.yl, b.yh, py + ph, py);
        }
    }
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void IGraphNode::draw(Node& self, Canvas& canvas) {
    draw_default(canvas);

    const float cx = content_x();
    const float cy = content_y();
    const float cw = inner_w();
    const float ch = inner_h();
    if (cw <= 0.f || ch <= 0.f) return;

    draw_toggle_pills(cx, cy, cw, canvas);

    const float px = cx + plot_left;
    const float py = cy + pill_strip_h + plot_top;
    const float pw = cw - plot_left - plot_right;
    const float ph = ch - pill_strip_h - plot_top - plot_bottom;
    if (pw <= 0.f || ph <= 0.f) return;

    // Single authoritative bounds computation for the whole frame.
    const PlotBounds b = compute_bounds();

    // Rebuild pixel-space cache for any dirty series.
    rebuild_cache(b, px, py, pw, ph);

    canvas.push_clip({ px, py, pw, ph });
    draw_plot_grid(b, px, py, pw, ph, canvas);
    draw_markers(b, px, py, pw, ph, canvas);
    draw_series_cached(px, py, pw, ph, canvas);
    canvas.pop_clip();

    // Labels and hover overlay sit outside the clip rect.
    draw_plot_labels(b, px, py, pw, ph, canvas);

    const float mx = self->doc_->input.mouse_screen_x;
    const float my = self->doc_->input.mouse_screen_y;
    if (mx >= px && mx <= px + pw && my >= py && my <= py + ph)
        draw_hover_label(b, px, py, pw, ph, mx, my, canvas);
}

// ---------------------------------------------------------------------------
// draw_series_cached
// ---------------------------------------------------------------------------

void IGraphNode::draw_series_cached(float /*px*/, float /*py*/,
                                    float /*pw*/, float /*ph*/,
                                    Canvas& canvas) const {
    for (const auto& [name, s] : series) {
        if (!s.display || s.xs.empty() || s.ys.size() != s.xs.size()) continue;

        const SeriesCache& c = cache.at(name);

        if (s.kind == SeriesKind::Line) {
            for (size_t i = 1; i < c.screen_xs.size(); ++i) {
                canvas.draw_line(c.screen_xs[i - 1], c.screen_ys[i - 1],
                                 c.screen_xs[i], c.screen_ys[i],
                                 s.color, s.weight);
            }
        }
        else {
            // Scatter — draw a filled circle at each point.
            const float r = s.weight;   // weight doubles as point radius
            for (size_t i = 0; i < c.screen_xs.size(); ++i) {
                canvas.fill_ellipse(c.screen_xs[i] - r, c.screen_ys[i] - r, r * 2.f, r * 2.f, s.color);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// draw_markers
// ---------------------------------------------------------------------------

void IGraphNode::draw_markers(const PlotBounds& b,
                              float px, float py, float pw, float ph,
                              Canvas& canvas) const {
    for (const Marker& m : markers) {
        if (m.axis == MarkerAxis::X) {
            const float sx = map_to_px(m.value, b.xl, b.xh, px, px + pw);
            if (sx < px || sx > px + pw) continue;
            if (m.dashed) canvas.draw_line_dashed(sx, py, sx, py + ph, m.color, m.weight);
            else canvas.draw_line(sx, py, sx, py + ph, m.color, m.weight);

            if (!m.label.empty())
                canvas.draw_text(m.label, fmt_.Get(), { sx + 3.f, py + 2.f, 60.f, 16.f }, m.color);
        }
        else {
            const float sy = map_to_px(m.value, b.yl, b.yh, py + ph, py);
            if (sy < py || sy > py + ph) continue;
            if (m.dashed) canvas.draw_line_dashed(px, sy, px + pw, sy, m.color, m.weight);
            else canvas.draw_line(px, sy, px + pw, sy, m.color, m.weight);

            if (!m.label.empty())
                canvas.draw_text(m.label, fmt_.Get(), { px + 3.f, sy - 14.f, 60.f, 16.f }, m.color);
        }
    }
}

// ---------------------------------------------------------------------------
// draw_toggle_pills  (unchanged from original except const ref)
// ---------------------------------------------------------------------------

void IGraphNode::draw_toggle_pills(float cx, float cy, float cw,
                                   Canvas& canvas) const {
    pill_rects_.clear();

    const float strip_cy = cy + pill_strip_h * 0.5f;
    float cursor = cx + plot_left;

    for (const auto& [key, s] : series) {
        const float text_w = static_cast<float>(s.name.size()) * 7.f;
        const float pill_w = text_w + pill_pad_x * 2.f;
        const float pill_h = 14.f + pill_pad_y * 2.f;
        const float pill_y = strip_cy - pill_h * 0.5f;

        const bool on = s.display && s.color.a > 0.f;

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
// draw_hover_label  — fixed nearest-point search using pre-sorted xs
// ---------------------------------------------------------------------------

void IGraphNode::draw_hover_label(const PlotBounds& b,
                                  float px, float py, float pw, float ph,
                                  float mx, float my,
                                  Canvas& canvas) const {
    const float data_x = b.xl + (mx - px) / pw * (b.xh - b.xl);
    const float data_y = b.yl + (py + ph - my) / ph * (b.yh - b.yl);

    float        best_dist_sq = std::numeric_limits<float>::max();
    float        best_x = 0.f, best_y = 0.f;
    const Color* best_color = nullptr;

    for (const auto& [name, s] : series) {
        if (!s.display || s.color.a < 0.01f || s.xs.empty()) continue;
        if (s.ys.size() != s.xs.size()) continue;

        // Use binary search when xs are sorted, otherwise linear scan.
        size_t start = 0, end = s.xs.size();
        if (s.sorted) {
            const auto it = std::lower_bound(s.xs.begin(), s.xs.end(), data_x);
            const size_t mid = static_cast<size_t>(it - s.xs.begin());
            start = (mid > 2) ? mid - 2 : 0;
            end = std::min(s.xs.size(), mid + 3);
        }

        for (size_t i = start; i < end; ++i) {
            // Distance in pixel space for a consistent threshold.
            const float spx = map_to_px(s.xs[i], b.xl, b.xh, 0.f, pw);
            const float spy = map_to_px(s.ys[i], b.yl, b.yh, ph, 0.f);
            const float cpx = map_to_px(data_x, b.xl, b.xh, 0.f, pw);
            const float cpy = map_to_px(data_y, b.yl, b.yh, ph, 0.f);
            const float d2 = (spx - cpx) * (spx - cpx) + (spy - cpy) * (spy - cpy);
            if (d2 < best_dist_sq) {
                best_dist_sq = d2;
                best_x = s.xs[i];
                best_y = s.ys[i];
                best_color = &s.color;
            }
        }
    }

    if (!best_color) return;
    if (std::sqrt(best_dist_sq) > 24.f) return;

    wchar_t buf[64];
    std::swprintf(buf, 64, L"x: %.2f  y: %.2f", best_x, best_y);

    const float lx = mx + 12.f;
    const float ly = my - 18.f;
    const float label_w = static_cast<float>(std::wcslen(buf)) * 7.f + 14.f;
    const float label_h = 18.f;

    const Color bg_c = Color(0.06f, 0.06f, 0.06f, 0.88f);
    const Color text_c = Color(1.f, 1.f, 1.f, 0.90f);

    // Colour accent strip using the series colour.
    canvas.fill_rect({ lx - 5.f, ly - 2.f, 3.f, label_h }, *best_color, 1.f);
    canvas.fill_rect({ lx - 5.f, ly - 2.f, label_w, label_h }, bg_c, 3.f);
    canvas.draw_text(buf, fmt_.Get(),
                     { lx + 2.f, ly - 2.f, label_w, label_h }, text_c);
}

// ---------------------------------------------------------------------------
// handle_click / toggle_series
// ---------------------------------------------------------------------------

bool IGraphNode::handle_click(float mx, float my) {
    for (const auto& pr : pill_rects_) {
        if (mx >= pr.x && mx <= pr.x + pr.w &&
            my >= pr.y && my <= pr.y + pr.h)
            return toggle_series(pr.key);
    }
    return false;
}

bool IGraphNode::toggle_series(const std::string& key) {
    auto it = series.find(key);
    if (it == series.end()) return false;
    it->second.display = !it->second.display;
    return true;
}

} // namespace lintel
