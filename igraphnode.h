#pragma once
#include "inode.h"

#include <cmath>
#include <limits>
#include <vector>
#include <map>

namespace lintel {

// ---------------------------------------------------------------------------
// IGraphNode
// ---------------------------------------------------------------------------

class IGraphNode : public INode {
public:
    std::map<std::string, DataSeries> series;

    // Manual axis ranges
    UIValue range_x_min;
    UIValue range_x_max;
    UIValue range_y_min;
    UIValue range_y_max;

    // Pixel insets reserving space for axis labels around the plot area.
    // Increase plot_left if Y-axis labels are being clipped.
    float plot_left   = 56.f;
    float plot_right  = 18.f;
    float plot_top    = 16.f;
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

    // ---- Draw sub-passes - all take Canvas& instead of GPU pointers ----

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
