#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "handle.h"

namespace lintel {

// ---------------------------------------------------------------------------
// Standard property keys
// ---------------------------------------------------------------------------
//
// Use these string constants with Attributes::set() / get() / has() to avoid
// typo-prone bare string literals at call sites.
//
namespace attribs {

// Box model
constexpr char background_color[] = "background-color"; // Color
constexpr char border_color[] = "border-color";     // Color
constexpr char border_weight[] = "border-weight";    // float (px)
constexpr char corner_radius[] = "corner-radius";    // float (px)
constexpr char opacity[] = "opacity";          // float [0, 1]

// Text
constexpr char editable[] = "editable";         // bool
constexpr char font_size[] = "font-size";        // float (pt)
constexpr char font_family[] = "font-family";      // std::wstring
constexpr char text_color[] = "text-color";       // Color
constexpr char bold[] = "bold";             // bool
constexpr char italic[] = "italic";           // bool
constexpr char wrap[] = "wrap";             // bool
constexpr char text_align[] = "text-align";       // TextAlign

// Graph / chart
constexpr char line_color[] = "line-color";       // Color
constexpr char line_weight[] = "line-weight";      // float (px)
constexpr char grid_color[] = "grid-color";       // Color
constexpr char grid_weight[] = "grid-weight";      // float (px)
constexpr char label_color[] = "label-color";      // Color
constexpr char label_font_size[] = "label-font-size";  // float (pt)

} // namespace attribs

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

using AttribValue = std::variant<float, Color, bool, std::wstring>;

class Attributes {
    std::unordered_map<std::string, AttribValue> props_;

public:

    // Insert or replace a value.
    Attributes& set(std::string_view key, AttribValue value) {
        props_[std::string(key)] = std::move(value);
        return *this;
    }

    // Remove a key; subsequent has() calls return false.
    Attributes& unset(std::string_view key) {
        props_.erase(std::string(key));
        return *this;
    }

    // True when the key has a stored entry.
    bool has(std::string_view key) const {
        return props_.count(std::string(key)) != 0;
    }

    // Pointer to the stored value as type T, or nullptr on type/key mismatch.
    template<typename T>
    T* get(std::string_view key) {
        auto it = props_.find(std::string(key));
        if (it == props_.end()) return nullptr;
        return std::get_if<T>(&it->second);
    }

    template<typename T>
    const T* get(std::string_view key) const {
        auto it = props_.find(std::string(key));
        if (it == props_.end()) return nullptr;
        return std::get_if<T>(&it->second);
    }

    // Stored value as type T, or fallback when the key is absent or mistyped.
    template<typename T>
    T get_or(std::string_view key, const T& fallback) const {
        if (const T* v = get<T>(key)) return *v;
        return fallback;
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
// Controls how text runs are positioned within the layout box on the
// horizontal axis.  Applied to an ITextNode via TextNode::text_align() or
// through Attributes::set(attribs::text_align, TextAlign::Center).
//
// Maps 1-to-1 onto DWRITE_TEXT_ALIGNMENT so that HitTestPoint coordinates
// remain consistent with what is visually rendered.
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
    Focus, // Gained keyboard focus (via click or Tab).
    Blur,  // Lost keyboard focus.
    // Keyboard - routed to the focused node, then bubbled.
    KeyDown, KeyUp, Char,
    // Drag - fired exclusively on the drag-source node.
    DragStart, Drag, DragEnd,
    // Receives every event type.
    Any,
};

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------
//
// Public handle to a UI tree node.  Lightweight, move-only.  The actual tree
// state lives in INode; this class is just a typed owning pointer.
//

class Node : public Impl<class INode> {
public:
    using EventHandler = std::function<void(Node&)>;

    Node();                         // Allocate a default INode.
    explicit Node(std::nullptr_t);  // Null handle; no allocation.
    ~Node();

    Node(Node&&) noexcept;
    Node& operator=(Node&&) noexcept;

    // -- Style -----------------------------------------------------------

    Attributes& attr();
    Node& attr(const Attributes& s);

    // -- Tree ------------------------------------------------------------

    // Transfer ownership of child into this node, detaching it from its
    // current parent first if necessary.  Returns a reference to the child.
    Node& push(Node&& child);

    // Allocate and append a new default child.  Returns a reference to it.
    Node& push();

    // Remove child from this node and return ownership to the caller.
    Node remove(Node& child);

    // Indexed child access.  Returns nullptr when index is out of range.
    Node* child(size_t index);

    // -- Layout ----------------------------------------------------------

    // Proportion of the parent's remaining main-axis space claimed by this
    // node relative to the sum of all siblings' shares.  Ignored when an
    // explicit pixel size is set on that axis.
    Node& share(float s = 1.f);
    Node& width(float w);
    Node& height(float h);

    Node& padding(Edges e);
    Node& margin(Edges e);
    Node& row();
    Node& column();

    // Cross-axis alignment of children.  Default: Stretch.
    Node& align(Align a);

    // Main-axis distribution of remaining space once share allocation is
    // done.  Only meaningful when all children have explicit sizes.
    Node& justify(Justify j);

    // Pixel gap between children on the main axis.
    Node& gap(float g);

    // -- Behaviour -------------------------------------------------------

    // Mark this node as keyboard-focusable (participates in Tab-order cycling).
    Node& focusable(bool f = true);

    // Enable drag events.  Once the cursor exceeds the drag threshold after
    // MouseDown, DragStart / Drag / DragEnd replace the normal Click sequence.
    Node& draggable(bool d = true);

    // -- Events ----------------------------------------------------------

    // Append a handler for type.  Multiple handlers accumulate in order.
    Node& on(Event type, EventHandler handler);

    // Remove all handlers for type on this node.
    void clear_on_of(Event type);

    // -- Query -----------------------------------------------------------

    Rect  rect()    const;
    // Relative to this node's content origin.
    float mouse_x() const;
    float mouse_y() const;
};
using WeakNode = WeakImpl<Node>;

// ---------------------------------------------------------------------------
// TextNode
// ---------------------------------------------------------------------------
//
// A node that renders and (optionally) edits a wstring.  Supports:
//   - Keyboard editing  (editable == true)
//   - Click-to-position cursor placement
//   - Click-drag and Shift+arrow text selection with highlight rendering
//   - Ctrl+Left / Ctrl+Right word-boundary movement
//   - Shift+Home / Shift+End to extend selection to line boundaries
//   - Ctrl+A to select all
//   - Text alignment (left / center / right / justify)
//
class TextNode : public Node {
public:
    TextNode();
    explicit TextNode(std::wstring_view content);

    // -- Content ---------------------------------------------------------

    TextNode& content(std::wstring_view c);

    // -- Text alignment --------------------------------------------------

    // Set the horizontal alignment of text runs within the layout box.
    // Wraps attribs::text_align; invalidates the DWrite format so that the
    // next draw picks up the change immediately.
    TextNode& text_align(TextAlign a);

    // -- Selection -------------------------------------------------------

    // Select all text (equivalent to Ctrl+A).
    TextNode& select_all();

    // Clear the selection without moving the caret.
    TextNode& deselect();

    // Return a copy of the currently selected substring.
    // Returns an empty wstring when there is no active selection.
    std::wstring selected_text() const;
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

    /** @brief Enter the Win32 message loop. Blocks until the window is closed. */
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

void animate(float* property, float begin, float end, float duration);

} // namespace lintel
