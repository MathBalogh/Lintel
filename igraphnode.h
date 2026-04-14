#pragma once
#include "inode.h"
#include "plot_common.h"

#include <vector>
#include <map>

namespace lintel {

// ---------------------------------------------------------------------------
// Cached per-series draw data (rebuilt whenever series data changes)
// ---------------------------------------------------------------------------

struct SeriesCache {
    // Pixel-space polyline / point list, valid for a specific (pw, ph, bounds).
    std::vector<float> screen_xs;   // parallel to DataSeries::xs
    std::vector<float> screen_ys;
    bool               dirty = true;
};

// ---------------------------------------------------------------------------
// IGraphNode — internal implementation node
// ---------------------------------------------------------------------------

struct PillRect {
    std::string key;
    float x, y, w, h;
};

struct IGraphNode : public INode {
    IGraphNode();

    // --- data ---
    std::unordered_map<std::string, DataSeries>    series;
    std::unordered_map<std::string, SeriesCache>   cache;
    std::vector<Marker>                            markers;

    UIValue range_x_min, range_x_max;
    UIValue range_y_min, range_y_max;

    // Mutable because rebuilt during const draw pass
    mutable std::vector<PillRect> pill_rects_;

    // Text format handle
    ComPtr<IDWriteTextFormat> fmt_;

    // --- INode overrides ---
    void draw(Node& self, Canvas& canvas) override;

    // --- drawing helpers ---
    PlotBounds compute_bounds() const;

    void rebuild_cache(const PlotBounds& b,
                       float px, float py, float pw, float ph);

    void draw_toggle_pills(float cx, float cy, float cw,
                           Canvas& canvas) const;

    void draw_plot_grid(const PlotBounds& b,
                        float px, float py, float pw, float ph,
                        Canvas& canvas) const;

    void draw_series_cached(float px, float py, float pw, float ph,
                            Canvas& canvas) const;

    void draw_markers(const PlotBounds& b,
                      float px, float py, float pw, float ph,
                      Canvas& canvas) const;

    void draw_plot_labels(const PlotBounds& b,
                          float px, float py, float pw, float ph,
                          Canvas& canvas) const;

    void draw_hover_label(const PlotBounds& b,
                          float px, float py, float pw, float ph,
                          float mx, float my,
                          Canvas& canvas) const;

    bool handle_click(float mx, float my);
    bool toggle_series(const std::string& key);

    // --- geometry constants ---
    static constexpr float plot_left = 46.f;
    static constexpr float plot_right = 12.f;
    static constexpr float plot_top = 8.f;
    static constexpr float plot_bottom = 28.f;
    static constexpr float pill_strip_h = 26.f;
    static constexpr float pill_pad_x = 6.f;
    static constexpr float pill_pad_y = 3.f;
    static constexpr float pill_gap = 6.f;
};

} // namespace lintel
