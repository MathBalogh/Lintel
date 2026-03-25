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

    int run();
};

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

Node&       root();

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
