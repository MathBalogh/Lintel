#pragma once

#include "handle.h"
#include "types.h"

namespace lintel {

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

    // -- Tree (extended) -------------------------------------------------

    // Destroy all children.  Equivalent to repeatedly calling remove() but
    // O(n) rather than O(n²) and flushes every child's CORE weak-refs.
    Node& clear_children();

    // -- Animation -------------------------------------------------------
    //
    // transition() installs a per-property spec so that subsequent animate()
    // calls without explicit timing use it.  Mirrors the .lintel `transition`
    // declaration; both paths write to the same INode::transitions_ map.
    //
    // animate(p, target)
    //   If a transition spec exists for p, starts (or restarts from the
    //   current live value) a tween towards target.  Otherwise snaps.
    //   Use this when the animation parameters are declared at authoring time.
    //
    // animate(p, target, duration, easing)
    //   Always creates a tween with the supplied timing, ignoring any installed
    //   spec.  Use this when the parameters are computed at call time.

    Node& transition(Property p, float duration, Easing easing = Easing::EaseOut);

    Node& animate(Property p, float  target);
    Node& animate(Property p, Color  target);
    Node& animate(Property p, float  target, float duration, Easing easing = Easing::EaseOut);
    Node& animate(Property p, Color  target, float duration, Easing easing = Easing::EaseOut);

    // -- Events ----------------------------------------------------------

    Node& on(Event type, EventHandler handler);
    void  clear_on_of(Event type);

    // -- Query -----------------------------------------------------------

    float mouse_x() const;
    float mouse_y() const;
    Rect     rect() const;
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
    TextNode& scrollbar(bool);
    TextNode& center_vertically(bool);

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
// ImageNode
// ---------------------------------------------------------------------------

class ImageNode : public Node {
public:
    ImageNode();
    explicit ImageNode(std::string_view path);
    ImageNode& source(std::string_view path);
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

    Node& root();

    float mouse_x();
    float mouse_y();

    MouseButton held_button();
    Modifiers   modifiers();
    int         key_vkey();
    bool        key_repeat();
    wchar_t     key_char();
    float       scroll_dx();
    float       scroll_dy();

    int run(std::function<void()> = nullptr);
};

// ---------------------------------------------------------------------------
// LoadResult
// ---------------------------------------------------------------------------
//
// Returned by load().  The root node contains the full scene sub-tree;
// the stylesheet holds all named styles from the file so they can be applied
// to dynamically created nodes after load() returns.
//
// Typical usage:
//
//     auto [subtree, sheet] = load("ui.lt");
//     CORE.root.push(std::move(subtree));
//
//     // Dynamically create a node that inherits the file's styles:
//     auto& btn = someParent.push();
//     sheet.apply(btn, "button");


class StyleSheet {
public:
    // ── Types ─────────────────────────────────────────────────────────────

    // A single resolved key/value pair, independent of the AST.
    struct Prop {
        std::string key;
        PropValue   value;
    };

    // A set of Prop deltas that fire when an event occurs on a node to which
    // this handler was applied.
    struct Handler {
        Event             event;
        std::vector<Prop> deltas;
    };

    // One complete named style.
    struct Style {
        std::vector<Prop>    props;    // applied at style-application time
        std::vector<Handler> handlers; // wired as event handlers
    };

    // ── Build API ─────────────────────────────────────────────────────────
    //
    // These are called by load() during tree construction.  They can also be
    // called directly to build a stylesheet programmatically.
    //

    // Define or replace a named style.
    StyleSheet& define(std::string name, std::vector<Prop> props,
                       std::vector<Handler> handlers = {});

    // Append event handlers to an existing (or new) style.
    StyleSheet& define_handler(const std::string& name,
                               Event              event,
                               std::vector<Prop>  deltas);

    // ── Query ─────────────────────────────────────────────────────────────

    bool has_style(std::string_view name) const;
    const Style* find_style(std::string_view name) const;

    // ── Application ───────────────────────────────────────────────────────

    // Apply all props of a named style to n (snap, no animation).
    // Wire all of that style's event handlers onto n.
    // No-op if the style does not exist.
    void apply(Node& n, std::string_view style_name) const;

    // Apply a list of props as animated deltas (used by event handlers).
    // Routes each prop through INode::animate_prop so active transitions fire.
    static void animate(Node& n, const std::vector<Prop>& deltas);

    // ── Single-prop dispatch ───────────────────────────────────────────────
    //
    // Translates a string key + PropValue into the appropriate Node mutation.
    // Handles shorthands (padding, margin), enum coercions (direction, align,
    // justify, text-align), transition installation, focusable/draggable
    // flags, and falls back to Attributes::set() via INode::apply() for all
    // registered framework properties.
    //
    // Both apply() and animate() delegate to this method, passing a different
    // application mode.
    //

    enum class Mode { Snap, Animate };

    static void dispatch_prop(Node& n, std::string_view key, const PropValue& val, Mode mode = Mode::Snap);

    // ── Query ───────────────────────────────────────────────────────

    void register_node(const std::string& name, WeakNode node);
    WeakNode find(const char* name);

    template<typename T>
    WeakImpl<T> find(const char* name) {
        if (auto base = find(name)) return WeakImpl<T>(base.handle());
        return WeakImpl<T>(nullptr);
    }

private:
    std::unordered_map<std::string, WeakNode> named_;
    std::unordered_map<std::string, Style> styles_;

    // Apply props to n using the given mode.
    static void apply_props(Node& n, const std::vector<Prop>& props, Mode mode);

    // Register event handlers on n so that each handler fires animate() on
    // its delta list when the event occurs.
    static void wire_handlers(Node& n, const std::vector<Handler>& handlers);
};

struct LoadResult {
    Node        root;
    StyleSheet  sheet;
    // Structured-bindings support (C++17 tie-breaking).
    // Usage: auto [root, sheet] = load("ui.lt");
};

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

// Returns a LoadResult.  The caller owns both the scene sub-tree and the
// StyleSheet.
LoadResult  load(const char* path_to_file);

} // namespace lintel
