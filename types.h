#pragma once

#include <array>
#include <bitset>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace lintel {

using Property = uint32_t;

namespace property {

enum : uint32_t {
    Null,

    BackgroundColor,
    BorderColor,
    BorderWeight,
    CornerRadius,
    TextColor,
    FontSize,
    FontFamily,

    Width,
    Height,
    PaddingTop,
    PaddingRight,
    PaddingBottom,
    PaddingLeft,
    MarginTop,
    MarginRight,
    MarginBottom,
    MarginLeft,
    Gap,
    Share,

    Direction,
    AlignItems,
    JustifyItems,

    k_hot_count,

    // Text
    Bold,
    Italic,
    Wrap,
    TextAlign,
    Editable,

    // Graph / chart
    GridColor,
    GridWeight,
    LabelColor,
    LabelFontSize,

    // Misc
    Opacity,
};

}

// Returns true for properties that belong to the box-model or layout-behavior
// categories.  Attributes::set() sets layout_dirty when this returns true.
inline bool is_layout_prop(uint32_t p) noexcept {
    return p < property::k_hot_count;
}

// ---------------------------------------------------------------------------
// Easing
// ---------------------------------------------------------------------------

enum class Easing {
    Linear,
    EaseIn,    // Quadratic acceleration.
    EaseOut,   // Quadratic deceleration (default for UI transitions).
    EaseInOut, // Symmetric S-curve.
    Spring,    // Critically-damped overshoot; best for spatial movement.
};

// ---------------------------------------------------------------------------
// TransitionSpec
// ---------------------------------------------------------------------------

struct TransitionSpec {
    float  duration = 0.15f;
    Easing easing = Easing::EaseOut;
};

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------

struct Color {
    float r, g, b, a;

    Color() noexcept;
    Color(float red, float green, float blue, float alpha = 1.0f) noexcept;

    static Color rgb(float r, float g, float b, float a = 1.0f) noexcept;
    static Color hsl(float h, float s, float l, float a = 1.0f) noexcept;
    static Color hsv(float h, float s, float v, float a = 1.0f) noexcept;
    static Color hex(uint32_t rgba);

    void clamp() noexcept;

    static Color black() noexcept { return { 0.f, 0.f, 0.f, 1.f }; }
    static Color white() noexcept { return { 1.f, 1.f, 1.f, 1.f }; }

    static Color lighten(const Color& o, float f) noexcept;
};

// ---------------------------------------------------------------------------
// AnimateDescriptor
// ---------------------------------------------------------------------------

struct AnimateDescriptor {
    std::variant<float, Color> target;
    float  duration = 0.15f;
    Easing easing = Easing::EaseOut;
};

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------

using AttribValue = std::variant<
    float,
    Color,
    bool,
    std::wstring,
    AnimateDescriptor // per-event animation override; created by animate()
>;

static constexpr uint32_t k_hot_count = static_cast<uint32_t>(property::k_hot_count);

class Attributes {
    // Hot storage: array of variants + populated bitset.
    // Default-constructed AttribValue holds float(0.f) (first variant type).
    std::array<AttribValue, k_hot_count> hot_;
    std::bitset<k_hot_count>             hot_set_;

    // Cold storage: map from Prop integer to value.
    std::unordered_map<uint32_t, AttribValue> cold_;

public:
    // True after any box-model or layout-behavior prop is set.
    // Read and cleared by INode::measure().
    bool layout_dirty = true;

    // ── Mutation ─────────────────────────────────────────────────────────

    Attributes& set(Property p, AttribValue value) {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (idx < k_hot_count) {
            hot_[idx] = std::move(value);
            hot_set_.set(idx);
        }
        else {
            cold_[idx] = std::move(value);
        }
        if (is_layout_prop(p)) layout_dirty = true;
        return *this;
    }

    Attributes& unset(Property p) {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (idx < k_hot_count) hot_set_.reset(idx);
        else                   cold_.erase(idx);
        return *this;
    }

    // ── Query ─────────────────────────────────────────────────────────────

    bool has(Property p) const noexcept {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (idx < k_hot_count) return hot_set_.test(idx);
        return cold_.count(idx) != 0;
    }

    template<typename T>
    const T* get(Property p) const {
        const uint32_t idx = p;
        if (idx < k_hot_count) {
            if (!hot_set_.test(idx)) return nullptr;
            return std::get_if<T>(&hot_[idx]);
        }
        auto it = cold_.find(idx);
        return (it == cold_.end()) ? nullptr : std::get_if<T>(&it->second);
    }

    template<typename T>
    T* get(uint32_t p) {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (idx < k_hot_count) {
            if (!hot_set_.test(idx)) return nullptr;
            return std::get_if<T>(&hot_[idx]);
        }
        auto it = cold_.find(idx);
        return (it == cold_.end()) ? nullptr : std::get_if<T>(&it->second);
    }

    template<typename T>
    T get_or(uint32_t p, const T& fallback) const {
        if (const T* v = get<T>(p)) return *v;
        return fallback;
    }

    // ── Live pointer for animation targets ───────────────────────────────
    //
    // Returns a stable pointer to a float-valued slot, initialising it to 0
    // if not yet set.  Used by Node::margin_bottom() and similar escape
    // hatches that hand a raw pointer to an animation system.
    //
    float* float_ref(uint32_t p) {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (idx < k_hot_count) {
            if (!hot_set_.test(idx)) {
                hot_[idx] = 0.f;
                hot_set_.set(idx);
            }
            return std::get_if<float>(&hot_[idx]);
        }
        // Cold path: emplace if absent, then return pointer.
        auto [it, inserted] = cold_.emplace(idx, 0.f);
        if (!inserted && !std::holds_alternative<float>(it->second))
            it->second = 0.f;
        return std::get_if<float>(&it->second);
    }
};

struct Rect {
    float x, y, w, h;

    Rect(float w_ = 0.f, float h_ = 0.f) noexcept;
    Rect(float x_, float y_, float w_, float h_) noexcept;
};

struct Edges {
    float top, right, bottom, left;

    Edges() noexcept;
    Edges(float all) noexcept;
    Edges(float x_axis, float y_axis) noexcept;
    // Individual-side constructor used by layout accessors.
    Edges(float top_, float right_, float bottom_, float left_) noexcept
        : top(top_), right(right_), bottom(bottom_), left(left_) {}

    float horizontal() const; // left + right
    float vertical()   const; // top  + bottom
};

enum class Direction {
    Row,    // Children laid out left to right.
    Column, // Children laid out top to bottom (default).
};

enum class Align {
    Start,   // Leading cross-axis edge.
    Center,  // Centred on the cross axis.
    End,     // Trailing cross-axis edge.
    Stretch, // Fill the cross axis (default).
};

enum class Justify {
    Start,        // Pack at the main-axis start (default).
    Center,       // Centre along the main axis.
    End,          // Pack at the main-axis end.
    SpaceBetween, // Free space between children.
    SpaceAround,  // Free space around each child.
};

// ---------------------------------------------------------------------------
// Text alignment
// ---------------------------------------------------------------------------

enum class TextAlign {
    Left,
    Center,
    Right,
    Justify,
};

// ---------------------------------------------------------------------------
// Input types
// ---------------------------------------------------------------------------

enum class MouseButton {
    None,
    Left,
    Right,
    Middle
};

struct Modifiers {
    bool shift = false;
    bool ctrl  = false;
    bool alt   = false;
};

// ---------------------------------------------------------------------------
// Event enumeration
// ---------------------------------------------------------------------------

enum class Event {
    Null,
    // Pointer
    MouseEnter, MouseLeave, MouseMove,
    MouseDown, MouseUp,
    Click, DoubleClick, RightClick,
    Scroll,
    // Focus
    Focus,
    Blur,
    // Keyboard
    KeyDown, KeyUp, Char,
    // Drag
    DragStart, Drag, DragEnd,
    // Wildcard
    Any
};

} // namespace lintel
