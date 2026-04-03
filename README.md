# Lintel

Lintel is a C++ UI library for building desktop applications on Windows. It provides a retained-mode node tree, a declarative stylesheet DSL (`.ltl` files), and an event system, all exposed through a clean, chainable C++ API.

---

## Getting Started

```cpp
#include <lintel/include.h>
using namespace lintel;

int WINAPI wWinMain(...) {
    Window win;

    auto [node, sheet] = load("./ui.ltl");
    win.root() = std::move(node);

    return win.run();
}
```

`load()` parses a `.ltl` file and returns a `LoadResult` containing the root node tree and its associated stylesheet. Structured bindings are supported via C++17.

---

## The DSL

UI layout and style are defined in `.ltl` files, separate from application logic.

```
text-color = #111827

style message:
    background-color = #FFFFFF
    border-weight = 1
    border-color = #E5E7EB
    padding = 10
    border-radius = 10
    text-color = text-color

root:
    node "messages":
        share = 1
        padding = 10

    text "content":
        height = 100
        editable = true
        font-size = 14
```

### Variables

Top-level assignments define reusable values that can be referenced by name in any property.

### Styles

Styles are named property sets applied to nodes. They support composition via `apply`:

```
style message-self:
    apply message
    text-align = right
```

`apply` is lower priority than explicit declarations. Properties set directly in the style always win over those pulled in via `apply`, regardless of order. When multiple applied styles define the same property, the last `apply` wins.

### Node Types

| DSL keyword | C++ type     |
|-------------|--------------|
| `node`      | `Node`       |
| `text`      | `TextNode`   |
| `graph`     | `GraphNode`  |
| `image`     | `ImageNode`  |

Named nodes (e.g. `text "content"`) can be retrieved from the stylesheet at runtime via `sheet.find<T>("name")`.

---

## Node Tree

Nodes are the building blocks of the UI. Every node holds layout properties, style properties, and an event handler list. The tree is modified at runtime through the C++ API.

```cpp
Node container;
container.set(Key::Direction, (unsigned int)Direction::Column);
container.padding(Edges(10.f));

TextNode label("Hello");
container.push(label);
```

### Layout Properties

Layout uses a flexbox-inspired model.

| Key              | Type      | Description                        |
|------------------|-----------|------------------------------------|
| `Key::Width`     | `UIValue` | Fixed, percent, or auto width      |
| `Key::Height`    | `UIValue` | Fixed, percent, or auto height     |
| `Key::Share`     | `float`   | Proportion of remaining space      |
| `Key::Direction` | enum      | `Row` or `Column`                  |
| `Key::AlignItems`| enum      | `Start`, `Center`, `End`, `Stretch`|
| `Key::JustifyItems`| enum    | `Start`, `Center`, `End`, `SpaceBetween`, `SpaceAround` |
| `Key::Gap`       | `float`   | Space between children             |
| Padding/Margin   | `float`   | Per-edge or shorthand via `Edges`  |

### UIValue

Dimension values can be expressed in pixels, percent, or auto:

```cpp
node.width(UIValue::px(200.f));
node.width(UIValue::pct(50.f));
node.width(UIValue::make_auto());

// Or with user-defined literals:
node.width(200_px);
node.width(50_pct);
```

### Visual Properties

| Key                  | Type    |
|----------------------|---------|
| `Key::BackgroundColor` | `Color` |
| `Key::BorderColor`   | `Color` |
| `Key::BorderWeight`  | `float` |
| `Key::BorderRadius`  | `float` |
| `Key::Opacity`       | `float` |

### Text Properties

| Key                 | Type        |
|---------------------|-------------|
| `Key::TextColor`    | `Color`     |
| `Key::FontSize`     | `float`     |
| `Key::FontFamily`   | `wstring`   |
| `Key::Bold`         | `bool`      |
| `Key::Italic`       | `bool`      |
| `Key::TextAlign`    | `TextAlign` |
| `Key::Wrap`         | `bool`      |
| `Key::Editable`     | `bool`      |

---

## TextNode

`TextNode` extends `Node` with text content and selection APIs.

```cpp
TextNode t("Initial text");
t.content(L"Updated text");
t.clear_content();
t.text_align(TextAlign::Center);
t.scrollbar(true);
t.center_vertically(true);

std::wstring sel = t.selected_text();
t.select_all();
t.deselect();
```

`TextNode::on()` provides a typed event callback — the handler receives a `TextNode&` directly, with no cast required:

```cpp
t.on(Event::Char, [](TextNode& n) {
    // n is already typed
});
```

---

## GraphNode

`GraphNode` renders one or more line series on a 2D plot.

```cpp
GraphNode graph;
graph.x_range(0.f, 100.f);
graph.y_range(-1.f, 1.f);

DataSeries& s = graph.create_series();
s.name = L"Signal";
s.color = Color::rgb(0.2f, 0.6f, 1.f);
s.push(0.f, 0.f);
s.push(1.f, 0.5f);
```

Individual series are accessed by index via `get_series(size_t)`.

---

## ImageNode

`ImageNode` loads and displays an image from disk.

```cpp
ImageNode img("./assets/logo.png");
img.source("./assets/other.png"); // swap at runtime
```

---

## Events

Events are dispatched to nodes via the `on()` method. The base `Node::on()` uses a type-erased handler; `TextNode::on()` provides a typed overload.

```cpp
node.on(Event::Click, [](WeakNode n) {
    // handle click
});

node.on(Event::Any, handler); // catch-all
node.clear_on_of(Event::Click);
```

### Available Events

| Category   | Events |
|------------|--------|
| Mouse      | `MouseEnter`, `MouseLeave`, `MouseMove`, `MouseDown`, `MouseUp`, `Click`, `DoubleClick`, `RightClick`, `Scroll` |
| Focus      | `Focus`, `Blur` |
| Keyboard   | `KeyDown`, `KeyUp`, `Char` |
| Drag       | `DragStart`, `Drag`, `DragEnd` |
| Catch-all  | `Any` |

### Input Query (via `Window`)

Inside event handlers, input state is queried through the `Window`:

```cpp
win.key_char()      // wchar_t — character for Char events
win.key_vkey()      // virtual key code for KeyDown/KeyUp
win.key_repeat()    // true if key is being held
win.held_button()   // MouseButton::Left / Right / Middle
win.modifiers()     // Modifiers { shift, ctrl, alt }
win.scroll_dx/dy()  // scroll delta
win.mouse_x/y()     // cursor position relative to window
```

Mouse position relative to a specific node is available via `node.mouse_x()` / `node.mouse_y()`.

---

## StyleSheet

`StyleSheet` is returned by `load()` alongside the node tree. It holds named styles and a registry of named nodes.

### Applying Styles

```cpp
sheet.apply(node, "message");
sheet.apply(node, "message-self");
```

### Finding Named Nodes

```cpp
WeakNode n        = sheet.find("messages");
WeakImpl<TextNode> t = sheet.find<TextNode>("content");

if (t) {
    t->content(L"Hello");
}
```

### Defining Styles at Runtime

```cpp
sheet.define("highlight", {
    { "background-color", Color::rgb(1.f, 1.f, 0.f) },
    { "border-weight",    1.f }
});
```

Event-driven property deltas (hover effects, focus rings, etc.) can be attached via `define_handler()`:

```cpp
sheet.define_handler("button", Event::MouseEnter, {
    { "background-color", Color::rgb(0.9f, 0.9f, 0.9f) }
});
```

---

## Color

```cpp
Color c1 = Color::rgb(1.f, 0.5f, 0.f);
Color c2 = Color::hsl(200.f, 0.8f, 0.5f);
Color c3 = Color::hsv(200.f, 0.8f, 0.9f);
Color c4 = Color::hex(0xFF8800FF);

Color lighter = Color::lighten(c1, 0.2f);
```

All channel values are normalized floats in `[0, 1]`. `clamp()` enforces this range in-place.

---

## Custom Node Types

Third-party node types can be registered and instantiated from `.ltl` files:

```cpp
lintel::register_node<MyCustomNode>("my-node");

// Now usable in .ltl:
// my-node "widget":
//     width = 200
```

Custom property keys can also be registered and resolved at runtime:

```cpp
Key myKey = lintel::register_key("my-property");
Key resolved = lintel::get_key("my-property");
```

---

## Window

```cpp
Window win;
win.root() = std::move(rootNode);

// Blocking run loop. Optional per-frame callback.
int exit_code = win.run([&]() {
    // called every frame
});
```

`win.width()` and `win.height()` return the current client area dimensions.
