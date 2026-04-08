#pragma once
#include "inode.h"
#include "plot_common.h"   // unified plot helpers

#include <vector>
#include <map>

namespace lintel {

// ---------------------------------------------------------------------------
// IHistogramNode
// ---------------------------------------------------------------------------

class IHistogramNode : public INode {
public:
    std::map<std::string, DataSeries> series;

    // Manual axis ranges
    UIValue range_x_min;
    UIValue range_x_max;
    UIValue range_y_min;
    UIValue range_y_max;

    // Pixel insets reserving space for axis labels around the plot area.
    float plot_left = 56.f;
    float plot_right = 18.f;
    float plot_top = 16.f;
    float plot_bottom = 40.f;

    // Pixel height reserved at the top of the content rect for toggle segments.
    // Automatically sized to fit one row of pills.
    static constexpr float pill_strip_h = 24.f;
    static constexpr float pill_pad_x = 8.f;  // horizontal text padding
    static constexpr float pill_pad_y = 3.f;  // vertical text padding
    static constexpr float pill_gap = 5.f;  // gap between pills

    void draw(Node& self, Canvas& canvas) override;

private:
    void draw_series(const PlotBounds& b,
                     float px, float py, float pw, float ph,
                     Canvas& canvas) const;

    // Pill hit-rects rebuilt each draw, consumed by handle_click.
    struct PillRect { std::string key; float x, y, w, h; };
    mutable std::vector<PillRect> pill_rects_;
};

} // namespace lintel
