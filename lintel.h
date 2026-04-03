#pragma once

#include "handle.h"

#include <string>
#include <functional>

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

namespace lintel {

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

enum class Direction {
    Row,
    Column
};
enum class Align {
    Start,
    Center,
    End,
    Stretch
};
enum class Justify {
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround
};

enum class TextAlign {
    Left,
    Center,
    Right,
    Justify
};

enum class MouseButton {
    None,
    Left,
    Right,
    Middle
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
struct Modifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

struct Color {
    float r, g, b, a;

    Color() noexcept;
    Color(float c);
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

enum class UIUnit {
    Auto,
    Pixels,
    Percent
};
struct UIValue {
    UIUnit unit = UIUnit::Auto;
    float value = 0.0f;

    static UIValue make_auto() { return { UIUnit::Auto, 0.f }; }
    static UIValue px(float px) { return { UIUnit::Pixels, px }; }
    static UIValue pct(float pct) { return { UIUnit::Percent, pct }; }

    bool is_auto() const {
        return unit == UIUnit::Auto;
    }
    bool is_pixels() const {
        return unit == UIUnit::Pixels;
    }
    bool is_percent() const {
        return unit == UIUnit::Percent;
    }
};
UIValue operator"" _px(unsigned long long v);
UIValue operator"" _pct(unsigned long long v);

struct Key {
    using key_t = unsigned int;

    enum : key_t {
        Null,
        __hot_floats_begin,
        BorderWeight,
        BorderRadius,
        __hot_floats_end,
        // Layout affecting properties
        __hot_layout_begin,
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
        __hot_layout_end,
        __hot_colors_begin,
        BackgroundColor,
        BorderColor,
        TextColor,
        __hot_colors_end,
        __cold_properties_begin,
        FontSize,
        FontFamily,
        Bold,
        Italic,
        Wrap,
        TextAlign,
        Editable,
        VerticalCenter,
        Scrollbar,
        GridColor,
        GridWeight,
        LabelColor,
        LabelFontSize,
        Opacity,
        __cold_properties_end
    };

    key_t index;

    Key(): index(Null) {}
    Key(key_t idx): index(idx) {}

    bool affects_layout() const noexcept { return within(__hot_layout_begin, __hot_layout_end); }
    bool is_hot_float()   const noexcept { return within(__hot_floats_begin, __hot_floats_end); }
    bool is_hot_color()   const noexcept { return within(__hot_colors_begin, __hot_colors_end); }

    key_t hot_float_index() const noexcept { return index - __hot_floats_begin - 1; }
    key_t hot_color_index() const noexcept { return index - __hot_colors_begin - 1; }

    bool operator == (Key other) const noexcept { return index == other.index; }
private:
    // non-inclusive
    bool within(key_t begin, key_t end) const { return index > begin && index < end; }
};

} // namespace lintel

// ---------------------------------------------------------------------------
// Hash Override for Key
// ---------------------------------------------------------------------------

namespace std {
template<>
struct hash<lintel::Key> {
    size_t operator()(const lintel::Key& k) const noexcept {
        return std::hash<unsigned int>{}(k.index);
    }
};
} // namespace std

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

namespace lintel {

class Property {
public:
    enum class Type {
        Null,
        Bool,
        Float,
        Enum,
        UIValue,
        Color,
        WString
    };

    Property();
    ~Property();

    Property(bool p);
    Property(float p);
    Property(unsigned int p);
    Property(const UIValue& p);
    Property(const Color& p);
    Property(const std::wstring& str);

    Property(const Property& other);
    Property& operator=(const Property& other);

    Property(Property&& other) noexcept;
    Property& operator=(Property&& other) noexcept;

    Type type() const noexcept;

    bool is_null() const noexcept;
    bool is_bool() const noexcept;
    bool is_float() const noexcept;
    bool is_enum() const noexcept;
    bool is_ui() const noexcept;
    bool is_color() const noexcept;
    bool is_wstring() const noexcept;

    bool         get_bool()  const noexcept;
    float        get_float() const noexcept;
    float& get_float();
    unsigned int get_enum()  const noexcept;
    Color        get_color() const noexcept;
    Color& get_color();
    UIValue      get_ui()    const noexcept;

    const std::wstring& get_wstring() const noexcept;

    bool operator==(const std::wstring&) const;

    operator bool()   const noexcept;
    operator float()  const noexcept;
    operator Color()  const noexcept;
    operator UIValue() const noexcept;

private:
    // Compute max size safely
    static constexpr size_t DATA_SIZE =
        std::max({ sizeof(bool), sizeof(float), sizeof(unsigned int),
                   sizeof(UIValue), sizeof(Color), sizeof(std::wstring) });

    static constexpr size_t DATA_ALIGN =
        std::max({ alignof(bool), alignof(float), alignof(unsigned int),
                   alignof(UIValue), alignof(Color), alignof(std::wstring) });

    using Storage = std::aligned_storage_t<DATA_SIZE, DATA_ALIGN>;
    Storage data_;
    Type type_;

    void destroy();
    void copy_from(const Property& other);
    void move_from(Property&& other) noexcept;

    template<typename T>
    T* ptr() {
        return reinterpret_cast<T*>(&data_);
    }

    template<typename T>
    const T* ptr() const {
        return reinterpret_cast<const T*>(&data_);
    }
};
class Properties {
    static constexpr unsigned int HOT_FLOATS_COUNT =
        Key::__hot_floats_end - Key::__hot_floats_begin - 1;

    static constexpr unsigned int HOT_COLORS_COUNT =
        Key::__hot_colors_end - Key::__hot_colors_begin - 1;

    float hot_floats_[HOT_FLOATS_COUNT]{ 0 };
    uint32_t hot_floats_mask_ = 0;
    bool has_hot_float(unsigned int rel) const;

    Color hot_colors_[HOT_COLORS_COUNT]{ 0 };
    uint32_t hot_colors_mask_ = 0;
    bool has_hot_color(unsigned int rel) const;

    std::unordered_map<Key, Property> cold_;

    bool layout_dirty_ = true;
public:
    bool is_dirty() const;
    bool is_clean() const;
    void make_dirty();
    void make_clean();

    Properties& set(Key key, Property value);
    Properties& clear(Key key);
    bool has(Key key) const noexcept;

    float* get_float(Key key);
    Color* get_color(Key key);

    Property* find(Key key);
    Property* find(Key key, Property::Type type);

    Property get(Key key) const;
    Property get_or(Key key, Property::Type type, Property or_) const;
};

} // namespace std

// ---------------------------------------------------------------------------
// Nodes
// ---------------------------------------------------------------------------

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

    Node& set(Key key, Property value);

    Node& padding(const Edges&);
    Node& margin(const Edges&);
    Node& width(UIValue);
    Node& height(UIValue);

    // -- Tree ------------------------------------------------------------

    Node& push(Node& child);
    Node& push();
    Node  remove(Node& child);
    Node* child(size_t index);

    // Destroy all children.  Equivalent to repeatedly calling remove() but
    // O(n) rather than O(n²) and flushes every child's CORE weak-refs.
    Node& clear_children();

    void dirty();

    // -- Behaviour -------------------------------------------------------

    Node& focusable(bool f = true);
    Node& draggable(bool d = true);

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
    explicit TextNode(const std::wstring& content);

    TextNode& content(const std::wstring& c);
    TextNode& clear_content();
    std::wstring& content();

    TextNode& text_align(TextAlign a);
    TextNode& scrollbar(bool);
    TextNode& center_vertically(bool);

    TextNode& select_all();
    TextNode& deselect();
    std::wstring selected_text() const;

    TextNode& on(Event event, std::function<void(TextNode&)> callback);
};

// ---------------------------------------------------------------------------
// GraphNode
// ---------------------------------------------------------------------------

struct DataSeries {
    std::wstring name;
    std::vector<float> xs;
    std::vector<float> ys;
    Color color = Color(1.0f, 0.8f, 0.2f);
    float weight = 1.0f;

    void push(float x, float y) {
        xs.push_back(x);
        ys.push_back(y);
    }

    void clear() {
        xs.clear();
        ys.clear();
    }
};
class GraphNode : public Node {
public:
    GraphNode();

    DataSeries& get_series(size_t index);
    DataSeries& create_series();

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
// StyleSheet
// ---------------------------------------------------------------------------

class StyleSheet {
public:
    struct Prop {
        std::string key;
        Property value;
    };

    struct Handler {
        Event             event;
        std::vector<Prop> deltas;
    };

    struct Style {
        std::vector<Prop>    props;
        std::vector<Handler> handlers;
    };

    // -- Build API ---------------------------------------------------------
    
    // Define or replace a named style.
    StyleSheet& define(std::string name, std::vector<Prop> props, std::vector<Handler> handlers = {});

    // Append event handlers to an existing (or new) style.
    StyleSheet& define_handler(const std::string& name, Event event, std::vector<Prop> deltas);

    // -- Query -------------------------------------------------------------

    bool has_style(std::string_view name) const;
    const Style* find_style(std::string_view name) const;

    // -- Application -------------------------------------------------------

    // Apply all props of a named style to n
    // Wire all of that style's event handlers onto n.
    // No-op if the style does not exist.
    void apply(Node& n, std::string_view style_name) const;

    static void dispatch_prop(Node& n, std::string_view key, const Property& val);

    // -- Query -------------------------------------------------------

    void register_node(const std::string& name, WeakNode node);
    WeakNode find(const char* name);

    template<typename T>
    WeakImpl<T> find(const char* name) {
        if (auto base = find(name)) return WeakImpl<T>(base.handle());
        return WeakImpl<T>(nullptr);
    }

    // Apply props to n using the given mode.
    static void apply_props(Node& n, const std::vector<Prop>& props);
private:
    std::unordered_map<std::string, WeakNode> named_;
    std::unordered_map<std::string, Style> styles_;

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
// Free Functions
// ---------------------------------------------------------------------------

// Returns a LoadResult.  The caller owns both the scene sub-tree and the
// StyleSheet.
LoadResult load(const char* path_to_file);

// Convert a wstring to a standard string
std::string to_string(const std::wstring& w);
std::wstring to_wstring(const std::string& s);

} // namespace lintel

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

namespace lintel {

Event get_event(const std::string& name);

Key register_key(const std::string& name);
Key get_key(const std::string& name);

void register_node(const std::string& name, Node(*factory)());
template<typename T> void register_node(const std::string& name) {
    register_node(name, [] () -> Node {
        return T();
    });
}
Node create_node(const std::string& name);

} // namespace lintel
