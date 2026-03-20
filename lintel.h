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

#include "handle.h"

namespace lintel {

// ---------------------------------------------------------------------------
// Prop — unified property key enum
// ---------------------------------------------------------------------------
//
// Every addressable property on a node has a single Prop enumerator.  Values
// in [0, k_hot_count) are "hot": stored in a flat std::array for O(1)
// indexed access with no hashing.  Values >= k_hot_count are "cold": stored
// in an overflow unordered_map and used only for node-specific or rarely-read
// properties.
//
// Three categories of hot properties (layout-affecting ones marked with ★):
//
//   Visual       BackgroundColor … FontFamily        indices  0–6
//   Box model ★  Width … Share                       indices  7–18
//   Layout beh ★ Direction … JustifyItems            indices 19–21
//
// Cold properties cover text formatting and graph-specific styling that do
// not affect the base layout pipeline.
//
enum class Prop : uint32_t {
    // ── Visual (hot) ──────────────────────────────────────────────────────
    BackgroundColor = 0,
    BorderColor = 1,
    BorderWeight = 2,
    CornerRadius = 3,
    TextColor = 4,
    FontSize = 5,
    FontFamily = 6,

    // ── Box model (hot, layout-affecting) ─────────────────────────────────
    Width = 7,
    Height = 8,
    PaddingTop = 9,
    PaddingRight = 10,
    PaddingBottom = 11,
    PaddingLeft = 12,
    MarginTop = 13,
    MarginRight = 14,
    MarginBottom = 15,
    MarginLeft = 16,
    Gap = 17,
    Share = 18,

    // ── Layout behavior (hot, layout-affecting) ───────────────────────────
    Direction = 19,
    AlignItems = 20,
    JustifyItems = 21,

    // ── Sentinel: first index that is cold ────────────────────────────────
    k_hot_count = 22,

    // ── Cold (node-specific / rarely read) ────────────────────────────────
    // Text
    Bold = 22,
    Italic = 23,
    Wrap = 24,
    TextAlign = 25,
    Editable = 26,
    // Graph / chart
    GridColor = 27,
    GridWeight = 28,
    LabelColor = 29,
    LabelFontSize = 30,
    // Misc
    Opacity = 31,
};

// Returns true for properties that belong to the box-model or layout-behavior
// categories.  Attributes::set() sets layout_dirty when this returns true.
inline bool is_layout_prop(Prop p) noexcept {
    switch (p) {
        case Prop::Width:
        case Prop::Height:
        case Prop::PaddingTop:    case Prop::PaddingRight:
        case Prop::PaddingBottom: case Prop::PaddingLeft:
        case Prop::MarginTop:     case Prop::MarginRight:
        case Prop::MarginBottom:  case Prop::MarginLeft:
        case Prop::Gap:
        case Prop::Share:
        case Prop::Direction:
        case Prop::AlignItems:
        case Prop::JustifyItems:
            return true;
        default:
            return false;
    }
}

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
// Attributes
// ---------------------------------------------------------------------------
//
// Unified property store.  Hot properties (index < k_hot_count) are read
// and written through a flat std::array with a companion std::bitset that
// tracks populated slots — no hashing, no cache-miss on every frame.  Cold
// properties use an unordered_map keyed by the raw Prop integer value.
//
// layout_dirty is set automatically by set() whenever a box-model or layout-
// behavior property changes.  INode::measure() reads and clears this flag to
// skip subtrees that have not changed since the last layout pass.
//

using AttribValue = std::variant<float, Color, bool, std::wstring>;

static constexpr uint32_t k_hot_count = static_cast<uint32_t>(Prop::k_hot_count);

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

    Attributes& set(Prop p, AttribValue value) {
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

    Attributes& unset(Prop p) {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (idx < k_hot_count) hot_set_.reset(idx);
        else                   cold_.erase(idx);
        return *this;
    }

    // ── Query ─────────────────────────────────────────────────────────────

    bool has(Prop p) const noexcept {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (idx < k_hot_count) return hot_set_.test(idx);
        return cold_.count(idx) != 0;
    }

    template<typename T>
    const T* get(Prop p) const {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (idx < k_hot_count) {
            if (!hot_set_.test(idx)) return nullptr;
            return std::get_if<T>(&hot_[idx]);
        }
        auto it = cold_.find(idx);
        return (it == cold_.end()) ? nullptr : std::get_if<T>(&it->second);
    }

    template<typename T>
    T* get(Prop p) {
        const uint32_t idx = static_cast<uint32_t>(p);
        if (idx < k_hot_count) {
            if (!hot_set_.test(idx)) return nullptr;
            return std::get_if<T>(&hot_[idx]);
        }
        auto it = cold_.find(idx);
        return (it == cold_.end()) ? nullptr : std::get_if<T>(&it->second);
    }

    template<typename T>
    T get_or(Prop p, const T& fallback) const {
        if (const T* v = get<T>(p)) return *v;
        return fallback;
    }

    // ── Live pointer for animation targets ───────────────────────────────
    //
    // Returns a stable pointer to a float-valued slot, initialising it to 0
    // if not yet set.  Used by Node::margin_bottom() and similar escape
    // hatches that hand a raw pointer to an animation system.
    //
    float* float_ref(Prop p) {
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

// ---------------------------------------------------------------------------
// Box-model geometry
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
    // Individual-side constructor used by layout accessors.
    Edges(float top_, float right_, float bottom_, float left_) noexcept
        : top(top_), right(right_), bottom(bottom_), left(left_) {}

    float horizontal() const; // left + right
    float vertical()   const; // top  + bottom
};

// ---------------------------------------------------------------------------
// Layout enumerations
// ---------------------------------------------------------------------------

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
//
// Maps 1-to-1 onto DWRITE_TEXT_ALIGNMENT so that HitTestPoint coordinates
// remain consistent with what is rendered.  Stored in attr as a float
// (integer cast) via Prop::TextAlign; sync_style() casts back to this enum.
//
enum class TextAlign {
    Left,    // DWRITE_TEXT_ALIGNMENT_LEADING  (default)
    Center,  // DWRITE_TEXT_ALIGNMENT_CENTER
    Right,   // DWRITE_TEXT_ALIGNMENT_TRAILING
    Justify, // DWRITE_TEXT_ALIGNMENT_JUSTIFIED
};

// ---------------------------------------------------------------------------
// Input types
// ---------------------------------------------------------------------------

enum class MouseButton { None, Left, Right, Middle };

struct Modifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

// ---------------------------------------------------------------------------
// Event enumeration
// ---------------------------------------------------------------------------

enum class Event {
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
    Any,
};

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------

class Node : public Impl<class INode> {
public:
    using EventHandler = std::function<void(WeakImpl<Node>)>;

    Node();
    explicit Node(std::nullptr_t);
    ~Node();

    Node(Node&&) noexcept;
    Node& operator=(Node&&) noexcept;

    // -- Style -----------------------------------------------------------

    Attributes& attr();
    Node& attr(const Attributes& s);

    // -- Tree ------------------------------------------------------------

    Node& push(Node&& child);
    Node& push();
    Node  remove(Node& child);
    Node* child(size_t index);

    // -- Layout ----------------------------------------------------------

    Node& share(float s = 1.f);
    Node& width(float w);
    Node& height(float h);
    Node& padding(Edges e);
    Node& margin(Edges e);
    Node& row();
    Node& column();
    Node& align(Align a);
    Node& justify(Justify j);
    Node& gap(float g);

    // -- Behaviour -------------------------------------------------------

    Node& focusable(bool f = true);
    Node& draggable(bool d = true);

    // -- Events ----------------------------------------------------------

    Node& on(Event type, EventHandler handler);
    void  clear_on_of(Event type);

    // -- Query -----------------------------------------------------------

    float* margin_bottom();
    Rect   rect()    const;
    float  mouse_x() const;
    float  mouse_y() const;
};
using WeakNode = WeakImpl<Node>;

// ---------------------------------------------------------------------------
// TextNode
// ---------------------------------------------------------------------------

class TextNode : public Node {
public:
    TextNode();
    explicit TextNode(std::wstring_view content);

    TextNode& content(std::wstring_view c);
    TextNode& text_align(TextAlign a);
    TextNode& select_all();
    TextNode& deselect();
    std::wstring selected_text() const;
};

// ---------------------------------------------------------------------------
// GraphNode
// ---------------------------------------------------------------------------

class GraphNode : public Node {
public:
    GraphNode();

    GraphNode& push_series(
        std::wstring_view  name,
        std::vector<float> xs,
        std::vector<float> ys,
        Color              color = Color(0.30f, 0.70f, 1.00f, 1.f),
        float              weight = 2.f);

    GraphNode& clear_series();
    GraphNode& x_range(float lo, float hi);
    GraphNode& y_range(float lo, float hi);
};

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

class Window : public Impl<class IWindow> {
public:
    Window();
    ~Window();

    unsigned int width()  const;
    unsigned int height() const;

    int run();
};

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

Node& root();

float       mouse_x();
float       mouse_y();
MouseButton held_button();
Modifiers   modifiers();
int         key_vkey();
bool        key_repeat();
wchar_t     key_char();
float       scroll_dx();
float       scroll_dy();

Node        load(const char* path_to_file);
WeakNode    find(const char* name);

template<typename T>
WeakImpl<T> find(const char* name) {
    if (auto base = find(name)) return WeakImpl<T>(base.handle());
    return WeakImpl<T>(nullptr);
}

} // namespace lintel
