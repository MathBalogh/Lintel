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

    void draw(Node& self, Canvas& canvas) override;

private:
    // Each DataSeries rendered as filled vertical bars (histogram style),
    // clipped to the plot rectangle. Bars extend from the x-axis (y = 0)
    // up to the data y-value. xs are treated as bin centers; bar width is
    // derived from the spacing of the sorted xs (or a fixed fraction of
    // the x-range when only a single bin is present). Multiple series
    // are overlaid with per-series colors.
    void draw_series(const PlotBounds& b,
                     float px, float py, float pw, float ph,
                     Canvas& canvas) const;
};

} // namespace lintel
