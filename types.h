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

// ---------------------------------------------------------------------------
// Property IDs
// ---------------------------------------------------------------------------
//
// Layout is critical to memory size.  The property IDs are deliberately ordered
// so that Attributes can use two compact typed arrays instead of one giant
// std::variant array:
//
//   Hot floats  [ 1,  k_hot_float_end )  ← 18 properties × 4 B  =  72 B
//   Hot colors  [ k_hot_float_end, k_hot_end )  ← 3 × 16 B      =  48 B
//   Cold (map)  [ k_hot_end, … )         ← strings, bools, rare floats
//
// Total hot storage: 72 + 48 + masks + dirty flag ≈ 130 B.
// Previous layout (array of std::variant<…wstring…>): 23 × 40 = 920 B.
//
// Rule: is_layout_prop(p) is true for the Width…JustifyItems range only.
// Changing a color or string no longer spuriously marks layout dirty.

namespace property {

enum : uint32_t {

    Null = 0,

    // ── Hot floats [1, k_hot_float_end) ─────────────────────────────────
    // Stored in Attributes::hot_f_[p - 1].  All are plain float or
    // enum-cast-to-float.  Keep this block contiguous; do not insert
    // non-float properties inside it.

    BorderWeight = 1,   // float
    CornerRadius,       // float

    FontSize,           // float  (moved before layout props deliberately —
    //         not a layout prop, but very commonly set)

// Box-model: is_layout_prop() covers Width … JustifyItems.
Width,              // float, NaN = auto
Height,             // float, NaN = auto
PaddingTop,         // float
PaddingRight,       // float
PaddingBottom,      // float
PaddingLeft,        // float
MarginTop,          // float
MarginRight,        // float
MarginBottom,       // float
MarginLeft,         // float
Gap,                // float
Share,              // float  (flex share weight)
Direction,          // float (cast of Direction enum)
AlignItems,         // float (cast of Align enum)
JustifyItems,       // float (cast of Justify enum)

k_hot_float_end,    // ← sentinel; value = 19

// ── Hot colors [k_hot_float_end, k_hot_end) ─────────────────────────
// Stored in Attributes::hot_c_[p - k_hot_float_end].

BackgroundColor = k_hot_float_end,  // Color
BorderColor,                        // Color
TextColor,                          // Color

k_hot_end,          // ← sentinel; value = 22

// ── Cold properties [k_hot_end, …) ──────────────────────────────────
// Stored in Attributes::cold_ (unordered_map) — allocated only when set.
// Strings, bools, and less-frequently-touched props belong here.

FontFamily = k_hot_end,  // std::wstring

Bold,           // bool
Italic,         // bool
Wrap,           // bool
TextAlign,      // float (cast of TextAlign enum)
Editable,       // bool

GridColor,      // Color
GridWeight,     // float
LabelColor,     // Color
LabelFontSize,  // float

Opacity,        // float
};

} // namespace property

// True for the box-model / flex props that affect layout geometry.
// Attributes::set() sets layout_dirty when this returns true.
inline bool is_layout_prop(uint32_t p) noexcept {
    return p >= property::Width && p <= property::JustifyItems;
}

// ---------------------------------------------------------------------------
// Easing
// ---------------------------------------------------------------------------

enum class Easing {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    Spring,
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
//
// Kept as a standalone struct for future use by an animate() API.
// Removed from PropValue — passing animation overrides through the attribute
// map was the wrong abstraction: it inflated every stored value and made the
// hot array unnecessarily large.  Per-call overrides are now passed as
// separate arguments to INode::animate_prop().

struct AnimateDescriptor {
    std::variant<float, Color> target;
    float  duration = 0.15f;
    Easing easing = Easing::EaseOut;
};

// ---------------------------------------------------------------------------
// PropValue  (was AttribValue)
// ---------------------------------------------------------------------------
//
// The value type stored in Attributes and StyleSheet.
// AnimateDescriptor is no longer a variant member — it lived here only to
// support the (still-commented-out) animate() value function, and its 24-byte
// footprint was the single largest contributor to Attributes bloat.
//
// AttribValue is kept as an alias so callers written against the old name
// continue to compile without modification.

using PropValue = std::variant<float, Color, bool, std::wstring>;
using AttribValue = PropValue;   // backward-compat alias

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------
//
// Per-node property bag with two-tier hot storage:
//
//   Tier 1 — hot floats:  float hot_f_[18]   +  uint32_t bitmask   = 76 B
//   Tier 2 — hot colors:  Color hot_c_[3]    +  uint8_t  bitmask   = 49 B
//   Tier 3 — cold map:    unordered_map (heap-allocated only when used)
//
// Total object size (cold map empty) ≈ 130 B vs the previous 920 B.
//
// API surface is unchanged from the caller's perspective.

namespace detail {
static constexpr uint32_t k_hot_float_first = 1u;
static constexpr uint32_t k_hot_float_count =
property::k_hot_float_end - k_hot_float_first;   // 18
static constexpr uint32_t k_hot_color_first = property::k_hot_float_end; // 19
static constexpr uint32_t k_hot_color_count =
property::k_hot_end - property::k_hot_float_end; // 3
static_assert(k_hot_float_count <= 32, "hot_f_mask_ must be uint32_t");
static_assert(k_hot_color_count <= 8, "hot_c_mask_ must be uint8_t");
} // namespace detail

class Attributes {
    // ── Hot float storage ────────────────────────────────────────────────
    float    hot_f_[detail::k_hot_float_count] = {};
    uint32_t hot_f_mask_ = 0; // bit i = hot_f_[i] has been set

    // ── Hot color storage ────────────────────────────────────────────────
    Color   hot_c_[detail::k_hot_color_count] = {};
    uint8_t hot_c_mask_ = 0;  // bit i = hot_c_[i] has been set

    // ── Cold storage ─────────────────────────────────────────────────────
    std::unordered_map<uint32_t, PropValue> cold_;

    // ── Internal routing helpers ─────────────────────────────────────────

    bool in_hot_float(uint32_t idx) const noexcept {
        return idx >= detail::k_hot_float_first &&
            idx < property::k_hot_float_end;
    }
    bool in_hot_color(uint32_t idx) const noexcept {
        return idx >= detail::k_hot_color_first &&
            idx < property::k_hot_end;
    }

public:
    // True after any layout-affecting property is written.
    // Read and cleared by INode::measure().
    bool layout_dirty = true;

    // ── Mutation ──────────────────────────────────────────────────────────

    Attributes& set(Property p, PropValue value) {
        const uint32_t idx = static_cast<uint32_t>(p);

        if (in_hot_float(idx)) {
            // Only float values belong in the float slots; any other type
            // (e.g. a Color written to a float property) goes to cold so the
            // type error is visible rather than silently dropped.
            if (const float* f = std::get_if<float>(&value)) {
                hot_f_[idx - detail::k_hot_float_first] = *f;
                hot_f_mask_ |= 1u << (idx - detail::k_hot_float_first);
            }
            else {
                cold_[idx] = std::move(value);
            }
        }
        else if (in_hot_color(idx)) {
            if (const Color* c = std::get_if<Color>(&value)) {
                hot_c_[idx - detail::k_hot_color_first] = *c;
                hot_c_mask_ |= 1u << (idx - detail::k_hot_color_first);
            }
            else {
                cold_[idx] = std::move(value);
            }
        }
        else {
            cold_[idx] = std::move(value);
        }

        if (is_layout_prop(p)) layout_dirty = true;
        return *this;
    }

    Attributes& unset(Property p) {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (in_hot_float(idx))
            hot_f_mask_ &= ~(1u << (idx - detail::k_hot_float_first));
        else if (in_hot_color(idx))
            hot_c_mask_ &= ~(1u << (idx - detail::k_hot_color_first));
        else
            cold_.erase(idx);
        return *this;
    }

    // ── Query ─────────────────────────────────────────────────────────────

    bool has(Property p) const noexcept {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (in_hot_float(idx))
            return (hot_f_mask_ >> (idx - detail::k_hot_float_first)) & 1u;
        if (in_hot_color(idx))
            return (hot_c_mask_ >> (idx - detail::k_hot_color_first)) & 1u;
        return cold_.count(idx) != 0;
    }

    // Returns a typed pointer into the stored value, or nullptr on miss/mismatch.
    template<typename T>
    const T* get(Property p) const {
        const uint32_t idx = static_cast<uint32_t>(p);

        if (in_hot_float(idx)) {
            uint32_t slot = idx - detail::k_hot_float_first;
            if (!((hot_f_mask_ >> slot) & 1u)) return nullptr;
            // if constexpr: the dead branch is removed by the compiler.
            if constexpr (std::is_same_v<T, float>)
                return &hot_f_[slot];
            return nullptr; // type mismatch for this slot
        }

        if (in_hot_color(idx)) {
            uint32_t slot = idx - detail::k_hot_color_first;
            if (!((hot_c_mask_ >> slot) & 1u)) return nullptr;
            if constexpr (std::is_same_v<T, Color>)
                return &hot_c_[slot];
            return nullptr;
        }

        // Cold path
        auto it = cold_.find(idx);
        if (it == cold_.end()) return nullptr;
        return std::get_if<T>(&it->second);
    }

    // Non-const version needed by a few internal callers.
    template<typename T>
    T* get(Property p) {
        return const_cast<T*>(std::as_const(*this).get<T>(p));
    }

    template<typename T>
    T get_or(Property p, const T& fallback) const {
        if (const T* v = get<T>(p)) return *v;
        return fallback;
    }

    // ── Live pointer for animation targets ────────────────────────────────
    //
    // Returns a stable pointer to a float slot (initialising it to 0 if not
    // yet set).  Used by Node::margin_bottom() and similar escape hatches
    // that hand a raw pointer to an external animation system.

    float* float_ref(Property p) {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (in_hot_float(idx)) {
            uint32_t slot = idx - detail::k_hot_float_first;
            hot_f_mask_ |= 1u << slot;
            return &hot_f_[slot];
        }
        // Cold: emplace 0.f if absent or wrong type.
        auto& entry = cold_[idx];
        if (!std::holds_alternative<float>(entry)) entry = 0.f;
        return std::get_if<float>(&entry);
    }
};

// ---------------------------------------------------------------------------
// Geometric helpers
// ---------------------------------------------------------------------------

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
    Edges(float top_, float right_, float bottom_, float left_) noexcept
        : top(top_), right(right_), bottom(bottom_), left(left_) {}

    float horizontal() const; // left + right
    float vertical()   const; // top  + bottom
};

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

enum class Direction { Row, Column };
enum class Align { Start, Center, End, Stretch };
enum class Justify { Start, Center, End, SpaceBetween, SpaceAround };
enum class TextAlign { Left, Center, Right, Justify };

enum class MouseButton { None, Left, Right, Middle };

struct Modifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

enum class Event {
    Null,
    MouseEnter, MouseLeave, MouseMove,
    MouseDown, MouseUp,
    Click, DoubleClick, RightClick,
    Scroll,
    Focus, Blur,
    KeyDown, KeyUp, Char,
    DragStart, Drag, DragEnd,
    Any
};

} // namespace lintel
