#pragma once
#include "inode.h"
#include "plot_common.h"

#include <vector>
#include <map>

namespace lintel {

class IGraphNode : public INode {
public:
    IGraphNode();

    std::map<std::string, DataSeries> series;

    UIValue range_x_min;
    UIValue range_x_max;
    UIValue range_y_min;
    UIValue range_y_max;

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
    // Returns true if the click toggled a series; call props.make_dirty() after.
    bool handle_click(float mx, float my);

private:
    void draw_series(const PlotBounds& b,
                     float px, float py, float pw, float ph,
                     Canvas& canvas) const;
    void draw_toggle_pills(float cx, float cy, float cw,
                           Canvas& canvas) const;
    void draw_hover_label(const PlotBounds& b,
                          float px, float py, float pw, float ph,
                          float mx, float my,
                          Canvas& canvas) const;

    // Pill hit-rects rebuilt each draw, consumed by handle_click.
    struct PillRect { std::string key; float x, y, w, h; };
    mutable std::vector<PillRect> pill_rects_;
    mutable ComPtr<IDWriteTextFormat> fmt_;

    const PillRect* find_pill(const std::string& key) const {
        for (const auto& pr : pill_rects_)
            if (pr.key == key) return &pr;
        return nullptr;
    }

    bool toggle_series(const std::string& key) {
        auto it = series.find(key);
        if (it == series.end()) return false;
        it->second.display = !it->second.display;
        return true;
    }
};

} // namespace lintel
