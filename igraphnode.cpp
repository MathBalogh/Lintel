#pragma once
#include "igraphnode.h"
#include "plot_common.h"

namespace lintel {

// ---------------------------------------------------------------------------
// GraphNode public API
// ---------------------------------------------------------------------------

GraphNode::GraphNode(): Node(nullptr) {
    impl_allocate<IGraphNode>();
    // shrink-wrap by default (same as ImageNode / HistogramNode)
    handle<IGraphNode>()->props.set(Key::Share, 0.f);
}

void GraphNode::remove_series(const std::string& name) {
    IGraphNode& n = *handle<IGraphNode>();
    n.series.erase(name);
    // ranges or data changed → redraw
    n.props.make_dirty();
}

DataSeries& GraphNode::series(const std::string& name) {
    IGraphNode& n = *handle<IGraphNode>();
    auto it = n.series.find(name);
    if (it == n.series.end()) {
        // create with a cycling pleasant colour
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

// ---------------------------------------------------------------------------
// IGraphNode implementation
// ---------------------------------------------------------------------------

void IGraphNode::draw(Node& self, Canvas& canvas) {
    draw_default(canvas);   // background + border (if any)

    float cx = content_x();
    float cy = content_y();
    float cw = inner_w();
    float ch = inner_h();
    if (cw <= 0.f || ch <= 0.f) return;

    // plot area inside the content rect (respecting the fixed insets)
    float px = cx + plot_left;
    float py = cy + plot_top;
    float pw = cw - plot_left - plot_right;
    float ph = ch - plot_top - plot_bottom;
    if (pw <= 0.f || ph <= 0.f) return;

    PlotBounds b = compute_plot_bounds(series,
                                       range_x_min, range_x_max,
                                       range_y_min, range_y_max);

    canvas.push_clip({ cx, cy, cw, ch });

    draw_plot_grid(b, px, py, pw, ph, canvas);
    draw_series(b, px, py, pw, ph, canvas);
    draw_plot_labels(b, px, py, pw, ph, canvas);

    canvas.pop_clip();
}

void IGraphNode::draw_series(const PlotBounds& b, float px, float py, float pw, float ph, Canvas& canvas) const {
    for (const auto& [name, s] : series) {
        if (!s.display || s.xs.empty() || s.ys.size() != s.xs.size()) continue;

        Color line_color = s.color;
        float weight = s.weight;

        // connect consecutive points with anti-aliased line segments
        // (preserves original data order; no sorting)
        for (size_t i = 1; i < s.xs.size(); ++i) {
            float x0 = s.xs[i - 1];
            float y0 = s.ys[i - 1];
            float x1 = s.xs[i];
            float y1 = s.ys[i];

            float px0 = map_to_px(x0, b.xl, b.xh, px, px + pw);
            float py0 = map_to_px(y0, b.yl, b.yh, py + ph, py);
            float px1 = map_to_px(x1, b.xl, b.xh, px, px + pw);
            float py1 = map_to_px(y1, b.yl, b.yh, py + ph, py);

            canvas.draw_line(px0, py0, px1, py1, line_color, weight);
        }
    }
}

} // namespace lintel
