#include "core.h"

#include <algorithm>
#include <cmath>

namespace lintel {

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------

Color::Color() noexcept
    : r(0.f), g(0.f), b(0.f), a(1.f) {}

Color::Color(float red, float green, float blue, float alpha) noexcept
    : r(red), g(green), b(blue), a(alpha) {}

Color Color::rgb(float r, float g, float b, float a) noexcept {
    return Color(r, g, b, a);
}

Color Color::hsl(float h, float s, float l, float a) noexcept {
    h = std::fmod(h, 360.f);
    if (h < 0.f) h += 360.f;

    s = std::clamp(s, 0.f, 1.f);
    l = std::clamp(l, 0.f, 1.f);

    const float c = (1.f - std::fabs(2.f * l - 1.f)) * s;
    const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = l - c / 2.f;

    float r1 = 0.f, g1 = 0.f, b1 = 0.f;
    if (h < 60.f) { r1 = c; g1 = x; }
    else if (h < 120.f) { r1 = x; g1 = c; }
    else if (h < 180.f) { g1 = c; b1 = x; }
    else if (h < 240.f) { g1 = x; b1 = c; }
    else if (h < 300.f) { r1 = x;          b1 = c; }
    else { r1 = c;          b1 = x; }

    return Color(r1 + m, g1 + m, b1 + m, a);
}

Color Color::hsv(float h, float s, float v, float a) noexcept {
    h = std::fmod(h, 360.f);
    if (h < 0.f) h += 360.f;

    s = std::clamp(s, 0.f, 1.f);
    v = std::clamp(v, 0.f, 1.f);

    const float c = v * s;
    const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    const float m = v - c;

    float r1 = 0.f, g1 = 0.f, b1 = 0.f;
    if (h < 60.f) { r1 = c; g1 = x; }
    else if (h < 120.f) { r1 = x; g1 = c; }
    else if (h < 180.f) { g1 = c; b1 = x; }
    else if (h < 240.f) { g1 = x; b1 = c; }
    else if (h < 300.f) { r1 = x;          b1 = c; }
    else { r1 = c;          b1 = x; }

    return Color(r1 + m, g1 + m, b1 + m, a);
}

Color Color::hex(uint32_t rgba) {
    const float r = ((rgba >> 24) & 0xFFu) / 255.f;
    const float g = ((rgba >> 16) & 0xFFu) / 255.f;
    const float b = ((rgba >> 8) & 0xFFu) / 255.f;
    const float a = ((rgba) & 0xFFu) / 255.f;
    return Color(r, g, b, a);
}

void Color::clamp() noexcept {
    r = std::clamp(r, 0.f, 1.f);
    g = std::clamp(g, 0.f, 1.f);
    b = std::clamp(b, 0.f, 1.f);
    a = std::clamp(a, 0.f, 1.f);
}

Color Color::lighten(const Color& o, float f) noexcept {
    return Color(o.r + f, o.g + f, o.b + f, o.a);
}

// ---------------------------------------------------------------------------
// Rect
// ---------------------------------------------------------------------------

Rect::Rect(float w_, float h_) noexcept
    : x(0.f), y(0.f), w(w_), h(h_) {}

Rect::Rect(float x_, float y_, float w_, float h_) noexcept
    : x(x_), y(y_), w(w_), h(h_) {}

// ---------------------------------------------------------------------------
// Edges
// ---------------------------------------------------------------------------

Edges::Edges() noexcept
    : top(0.f), right(0.f), bottom(0.f), left(0.f) {}

Edges::Edges(float all) noexcept
    : top(all), right(all), bottom(all), left(all) {}

Edges::Edges(float x_axis, float y_axis) noexcept
    : top(y_axis), right(x_axis), bottom(y_axis), left(x_axis) {}

float Edges::horizontal() const { return left + right; }
float Edges::vertical()   const { return top + bottom; }

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

Node& root() { return CORE.root; }

float mouse_x() { return CORE.input.mouse_screen_x; }
float mouse_y() { return CORE.input.mouse_screen_y; }

MouseButton held_button() { return CORE.input.held; }
Modifiers   modifiers() { return CORE.input.modifiers; }
int         key_vkey() { return CORE.input.key_vkey; }
bool        key_repeat() { return CORE.input.key_repeat; }
wchar_t     key_char() { return CORE.input.key_char; }
float       scroll_dx() { return CORE.input.scroll_dx; }
float       scroll_dy() { return CORE.input.scroll_dy; }

WeakNode find(const char* name) { return CORE.get_named(name); }

} // namespace lintel
