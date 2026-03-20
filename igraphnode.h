#pragma once
#include "inode.h"

#include <cmath>
#include <limits>
#include <vector>

namespace lintel {

// ---------------------------------------------------------------------------
// DataSeries
// ---------------------------------------------------------------------------
//
// One set of (x, y) sample pairs drawn as a connected polyline on a GraphNode.
// color and weight are per-series and override any global line-color / line-weight
// attributes set on the node itself.
//
struct DataSeries {
    std::wstring       name;
    std::vector<float> xs;
    std::vector<float> ys;
    Color              color = Color(0.30f, 0.70f, 1.00f, 1.f); // electric blue
    float              weight = 2.f;
};

// ---------------------------------------------------------------------------
// IGraphNode
// ---------------------------------------------------------------------------
//
// Internal implementation of GraphNode.  Renders a two-axis chart with:
//
//   - Axis-aligned grid lines at "nice" tick positions (rounded values).
//   - A brighter zero-line when zero is within the visible range.
//   - Right-aligned Y-axis labels to the left of the plot area.
//   - Centered X-axis labels below the plot area.
//   - One or more data series drawn as anti-aliased polylines with round joins.
//
// Visual style is intentionally minimal / dark-mode:
//   - Grid lines are very faint (override via attribs::grid_color).
//   - Axis labels are muted blue-gray (override via attribs::label_color).
//   - No axis-box border — the grid lines define the extent.
//
// Axis ranges default to auto (derived from data bounding box).  Manual ranges
// are set through GraphNode::x_range() / y_range().
//
// Drawing contract
// ----------------
// All rendering goes through the Canvas& parameter on draw() and the private
// draw_* helpers; this node never calls GPU or D2D APIs directly.
//
class IGraphNode : public INode {
public:
    std::vector<DataSeries> series;

    // Manual axis ranges — NaN means "auto-compute from data each frame".
    float range_x_min = nan_f();
    float range_x_max = nan_f();
    float range_y_min = nan_f();
    float range_y_max = nan_f();

    // Pixel insets reserving space for axis labels around the plot area.
    // Increase plot_left if Y-axis labels are being clipped.
    float plot_left = 56.f;
    float plot_right = 18.f;
    float plot_top = 16.f;
    float plot_bottom = 40.f;

    void draw(Node& self, Canvas& canvas) override;

private:
    // Resolved axis extents used for one draw call.
    struct Bounds { float xl, xh, yl, yh; };

    // Derive axis extents from series data, honouring any manual overrides.
    Bounds compute_bounds() const;

    // Linear map of a data value into pixel space.
    // Safe when vlo == vhi (returns the midpoint of [plo, phi]).
    static float map_to_px(float v, float vlo, float vhi, float plo, float phi) {
        return (vhi == vlo) ? (plo + phi) * 0.5f
            : plo + (v - vlo) / (vhi - vlo) * (phi - plo);
    }

    // ---- Tick-generation helpers ----------------------------------------

    // Return a "nice" step size for `target` divisions across `range`.
    // Rounds to the nearest 1 / 2 / 5 × 10^n.
    static float nice_step(float range, int target);

    // Generate tick positions inside [lo, hi] using nice_step.
    static std::vector<float> make_ticks(float lo, float hi, int target);

    // Format a tick value with the number of decimal places implied by step.
    // Normalises -0 to 0 so axis labels never show a spurious minus sign.
    static std::wstring format_tick(float v, float step);

    // ---- Draw sub-passes — all take Canvas& instead of GPU pointers ----

    // Horizontal grid lines at Y-tick positions; fainter vertical lines at
    // X-tick positions; a slightly brighter line at y == 0 when in range.
    void draw_grid(const Bounds& b,
                   float px, float py, float pw, float ph,
                   Canvas& canvas) const;

    // Axis labels: right-aligned on the Y axis, centred on the X axis.
    void draw_labels(const Bounds& b,
                     float px, float py, float pw, float ph,
                     Canvas& canvas) const;

    // Each DataSeries rendered as a smooth anti-aliased polyline, clipped to
    // the plot rectangle.
    void draw_series(const Bounds& b,
                     float px, float py, float pw, float ph,
                     Canvas& canvas) const;
};

} // namespace lintel
