#pragma once

#include "inode.h"   // Canvas, Rect, etc.

#include <vector>
#include <map>
#include <cmath>
#include <limits>
#include <string>
#include <algorithm>
#include <cwchar>
#include <dwrite.h>  // DWRITE_TEXT_ALIGNMENT_*, DWRITE_PARAGRAPH_ALIGNMENT_*

namespace lintel {

// ---------------------------------------------------------------------------
// Common plot infrastructure (shared by GraphNode and HistogramNode)
// ---------------------------------------------------------------------------

struct PlotBounds {
    float xl, xh, yl, yh;
};

// Derive axis extents from series data, honouring any manual overrides.
// If a range is Auto it is computed from the visible series; otherwise the
// supplied .value is used (the UIUnit is ignored — the value is a data coordinate).
inline PlotBounds compute_plot_bounds(
    const std::map<std::string, DataSeries>& series,
    const UIValue& range_x_min,
    const UIValue& range_x_max,
    const UIValue& range_y_min,
    const UIValue& range_y_max) {
    float xl = std::numeric_limits<float>::max();
    float xh = std::numeric_limits<float>::lowest();
    float yl = std::numeric_limits<float>::max();
    float yh = std::numeric_limits<float>::lowest();
    bool has_data = false;

    for (const auto& [name, s] : series) {
        if (!s.display) continue;
        for (float x : s.xs) {
            if (x < xl) xl = x;
            if (x > xh) xh = x;
            has_data = true;
        }
        for (float y : s.ys) {
            if (y < yl) yl = y;
            if (y > yh) yh = y;
        }
    }

    if (!has_data) {
        xl = xh = 0.f;
        yl = yh = 1.f;
    }

    if (!range_x_min.is_auto()) xl = range_x_min.value;
    if (!range_x_max.is_auto()) xh = range_x_max.value;
    if (!range_y_min.is_auto()) yl = range_y_min.value;
    if (!range_y_max.is_auto()) yh = range_y_max.value;

    // Prevent degenerate ranges (map_to_px handles equality safely)
    if (xh <= xl) {
        float mid = (xl + xh) * 0.5f;
        xl = mid - 0.5f;
        xh = mid + 0.5f;
    }
    if (yh <= yl) {
        float mid = (yl + yh) * 0.5f;
        yl = mid - 0.5f;
        yh = mid + 0.5f;
    }

    return { xl, xh, yl, yh };
}

// Linear map of a data value into pixel space.
// Safe when vlo == vhi (returns the midpoint of [plo, phi]).
inline float map_to_px(float v, float vlo, float vhi, float plo, float phi) {
    return (vhi == vlo) ? (plo + phi) * 0.5f
        : plo + (v - vlo) / (vhi - vlo) * (phi - plo);
}

// Return a "nice" step size for `target` divisions across `range`.
// Rounds to the nearest 1 / 2 / 5 × 10^n.
inline float nice_step(float range, int target) {
    if (range <= 0.f) return 1.f;
    float rough = range / static_cast<float>(target);
    float exp = std::floor(std::log10(rough));
    float frac = rough / std::pow(10.f, exp);
    float nice;
    if (frac < 1.5f)      nice = 1.f;
    else if (frac < 3.f)  nice = 2.f;
    else if (frac < 7.f)  nice = 5.f;
    else                  nice = 10.f;
    return nice * std::pow(10.f, exp);
}

// Generate tick positions inside [lo, hi] using nice_step (target ≈ 5 divisions).
inline std::vector<float> make_ticks(float lo, float hi, int target) {
    std::vector<float> ticks;
    if (lo >= hi) return ticks;
    float step = nice_step(hi - lo, target);
    float start = std::ceil(lo / step) * step;
    for (float t = start; t <= hi + step * 0.01f; t += step) {  // tolerance for floating-point
        if (t >= lo) ticks.push_back(t);
    }
    return ticks;
}

// Format a tick value with the number of decimal places implied by step.
// Normalises -0 to 0 and strips unnecessary trailing zeros.
inline std::wstring format_tick(float v, float step) {
    if (v == 0.f || (v == -0.f)) return L"0";

    int decimals = 0;
    float abs_step = std::fabs(step);
    if (abs_step >= 1.f) {
        decimals = 0;
    }
    else {
        float temp = abs_step;
        while (temp < 1.f && decimals < 10) {
            temp *= 10.f;
            ++decimals;
        }
    }

    wchar_t buf[64];
    swprintf(buf, 64, L"%.*f", decimals, v);
    std::wstring str = buf;

    // strip trailing zeros after decimal
    auto dot = str.find(L'.');
    if (dot != std::wstring::npos) {
        while (!str.empty() && str.back() == L'0') str.pop_back();
        if (!str.empty() && str.back() == L'.') str.pop_back();
    }
    if (str == L"-0") str = L"0";
    return str;
}

// Horizontal grid lines at Y-tick positions (faint); fainter vertical lines at
// X-tick positions; a slightly brighter line at y == 0 when in range.
// Uses fixed plot colours (GridColor / GridWeight from props can be wired later
// if desired; the original design kept them simple).
inline void draw_plot_grid(const PlotBounds& b,
                           float px, float py, float pw, float ph,
                           Canvas& canvas) {
    const Color grid_color{ 0.22f, 0.22f, 0.22f, 0.35f };
    const Color vgrid_color{ 0.22f, 0.22f, 0.22f, 0.20f };
    const Color zero_color{ 0.22f, 0.22f, 0.22f, 0.70f };
    const float weight = 1.0f;

    // Y-grid (horizontal)
    auto y_ticks = make_ticks(b.yl, b.yh, 5);
    for (float yt : y_ticks) {
        float py_pos = map_to_px(yt, b.yl, b.yh, py + ph, py);
        canvas.draw_line(px, py_pos, px + pw, py_pos, grid_color, weight);
    }

    // X-grid (vertical, fainter)
    auto x_ticks = make_ticks(b.xl, b.xh, 5);
    for (float xt : x_ticks) {
        float px_pos = map_to_px(xt, b.xl, b.xh, px, px + pw);
        canvas.draw_line(px_pos, py, px_pos, py + ph, vgrid_color, weight);
    }

    // Zero line (if in range)
    if (b.yl <= 0.f && 0.f <= b.yh) {
        float py0 = map_to_px(0.f, b.yl, b.yh, py + ph, py);
        canvas.draw_line(px, py0, px + pw, py0, zero_color, weight);
    }
}

// Axis labels: right-aligned on the Y axis (just left of plot), centred on the
// X axis (just below plot). Font size and colour are fixed for the classic
// look; the Label* properties can be respected in a future extension.
inline void draw_plot_labels(const PlotBounds& b,
                             float px, float py, float pw, float ph,
                             Canvas& canvas) {
    const Color label_color{ 0.25f, 0.25f, 0.25f, 1.0f };
    const float font_size = 11.0f;
    const wchar_t* font_family = L"Segoe UI";

    // One-time formats for the two alignments we need
    auto fmt_y = canvas.make_text_format(font_family, font_size);
    fmt_y->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);      // right-aligned
    fmt_y->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    auto fmt_x = canvas.make_text_format(font_family, font_size);
    fmt_x->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt_x->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const float y_label_w = 48.0f;
    const float y_label_h = 20.0f;
    const float x_label_w = 60.0f;
    const float x_label_h = 20.0f;

    // Y labels (right-aligned, anchored inside the left margin)
    auto y_ticks = make_ticks(b.yl, b.yh, 5);
    float y_step = nice_step(b.yh - b.yl, 5);
    for (float yt : y_ticks) {
        std::wstring txt = format_tick(yt, y_step);
        float py_pos = map_to_px(yt, b.yl, b.yh, py + ph, py);

        Rect box(px - y_label_w - 8.0f,
                 py_pos - y_label_h * 0.5f,
                 y_label_w,
                 y_label_h);

        canvas.draw_text(txt, fmt_y.Get(), box, label_color);
    }

    // X labels (centred, below the plot area)
    auto x_ticks = make_ticks(b.xl, b.xh, 5);
    float x_step = nice_step(b.xh - b.xl, 5);
    for (float xt : x_ticks) {
        std::wstring txt = format_tick(xt, x_step);
        float px_pos = map_to_px(xt, b.xl, b.xh, px, px + pw);

        Rect box(px_pos - x_label_w * 0.5f,
                 py + ph + 6.0f,
                 x_label_w,
                 x_label_h);

        canvas.draw_text(txt, fmt_x.Get(), box, label_color);
    }
}

} // namespace lintel
