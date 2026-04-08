#include "ihistogramnode.h"
#include "plot_common.h"

namespace lintel {

// ---------------------------------------------------------------------------
// HistogramNode public API (mirrors GraphNode exactly)
// ---------------------------------------------------------------------------

HistogramNode::HistogramNode(): Node(nullptr) {
    allocate<IHistogramNode>();
    // shrink-wrap by default
    handle<IHistogramNode>()->props.set(Key::Share, 0.f);
}

void HistogramNode::remove_series(const std::string& name) {
    IHistogramNode& n = *handle<IHistogramNode>();
    n.series.erase(name);
    // ranges or data changed -> redraw
    iptr_->props.make_dirty();
}

DataSeries& HistogramNode::series(const std::string& name) {
    IHistogramNode& n = *handle<IHistogramNode>();
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

HistogramNode& HistogramNode::x_range(float lo, float hi) {
    IHistogramNode& n = *handle<IHistogramNode>();
    n.range_x_min = UIValue::px(lo);
    n.range_x_max = UIValue::px(hi);
    n.props.make_dirty();
    return *this;
}

HistogramNode& HistogramNode::y_range(float lo, float hi) {
    IHistogramNode& n = *handle<IHistogramNode>();
    n.range_y_min = UIValue::px(lo);
    n.range_y_max = UIValue::px(hi);
    n.props.make_dirty();
    return *this;
}

// ---------------------------------------------------------------------------
// IHistogramNode implementation
// ---------------------------------------------------------------------------

void IHistogramNode::draw(Node& self, Canvas& canvas) {
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

    PlotBounds b = compute_plot_bounds(series, range_x_min, range_x_max, range_y_min, range_y_max);

    draw_plot_grid(b, px, py, pw, ph, canvas);
    draw_series(b, px, py, pw, ph, canvas);
    draw_plot_labels(b, px, py, pw, ph, canvas);
}

void IHistogramNode::draw_series(const PlotBounds& b, float px, float py, float pw, float ph, Canvas& canvas) const {
    for (const auto& [name, s] : series) {
        if (!s.display || s.xs.empty() || s.ys.size() != s.xs.size()) continue;

        // derive bar width from sorted bin centers
        std::vector<float> sorted_xs = s.xs;
        std::sort(sorted_xs.begin(), sorted_xs.end());

        float bin_width;
        if (sorted_xs.size() <= 1) {
            bin_width = (b.xh - b.xl) * 0.8f;
        }
        else {
            float min_spacing = std::numeric_limits<float>::max();
            for (size_t i = 1; i < sorted_xs.size(); ++i) {
                float d = sorted_xs[i] - sorted_xs[i - 1];
                if (d > 0.f && d < min_spacing) min_spacing = d;
            }
            bin_width = (min_spacing == std::numeric_limits<float>::max())
                ? (b.xh - b.xl) / static_cast<float>(sorted_xs.size())
                : min_spacing * 0.75f;   // leave small gaps between bars
        }

        Color bar_color = s.color;

        for (size_t i = 0; i < s.xs.size(); ++i) {
            float xc = s.xs[i];
            float yc = s.ys[i];

            float x0 = xc - bin_width * 0.5f;
            float x1 = xc + bin_width * 0.5f;

            // bars always run from y=0 to the data value (handles negative bars too)
            float y_bottom = 0.f;
            float y_top = yc;

            float bar_x = map_to_px(x0, b.xl, b.xh, px, px + pw);
            float bar_rx = map_to_px(x1, b.xl, b.xh, px, px + pw);
            float bar_w = std::fabs(bar_rx - bar_x);

            float bar_y0 = map_to_px(y_bottom, b.yl, b.yh, py + ph, py);
            float bar_y1 = map_to_px(y_top, b.yl, b.yh, py + ph, py);
            float bar_y = std::min(bar_y0, bar_y1);
            float bar_h = std::fabs(bar_y0 - bar_y1);

            if (bar_w > 0.5f && bar_h > 0.5f) {
                canvas.fill_rect({ bar_x, bar_y, bar_w, bar_h }, bar_color, 3.0f);
            }
        }
    }
}

} // namespace lintel
