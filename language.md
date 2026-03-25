# Lintel Layout Language

Lintel is a declarative, indentation-scoped language for defining UI trees, styles, transitions, and event handlers. It is parsed into a typed AST and then applied to a runtime scene graph by the `load()` function.

---

## Syntax Overview

Indentation is structural. A block opens with `:` at the end of a line and closes when indentation returns to its parent level. Spaces or tabs are both accepted; mixing them in the same file is not recommended. Comments begin with `//` and extend to the end of the line.

---

## Values

| Form | Type | Example |
|---|---|---|
| Integer or float | Number | `16`, `1.5`, `0.25f` |
| Hex colour | HexColor | `#FF0000`, `#FF0000FF` |
| Boolean | Boolean | `true`, `false` |
| Quoted string | Identifier | `"monospace"` |
| Bare word | Identifier | `row`, `center` |
| Variable reference | Identifier | `text-muted` |
| Space-separated list | ListExpr | `4 8 4 8` |

Hex colours accept 6 digits (opaque) or 8 digits (with alpha). Numbers accept an optional trailing `f`.

---

## Variables

Top-level bindings expand in-place wherever the name is used as a value. They are resolved once at load time.

```
text-muted   = #9CA3AF
accent-color = #3B82F6
base-size    = 14
```

---

## Styles

A style is a named block of properties (and optional event overrides) that can be applied to any node. Styles may apply other styles; later properties override earlier ones.

```
style card:
    direction  = column
    padding    = 16
    background = #1E1E2E
    border-radius = 8

style card-hover:
    apply card
    background = #2A2A3E
```

---

## Nodes

Nodes form the scene tree. Any identifier followed by `:` opens a node block. Optionally a name follows the tag for cross-reference registration.

```
node:
    direction = row
    gap       = 8

node sidebar:
    width = 240

text:
    content   = "hello"
    font-size = 14
```

Built-in node tags: `node`, `text`, `graph`, `image`. Custom tags can be registered via `register_node()` before calling `load()`.

---

## Properties

Properties are `key = value` pairs inside any block. Keys are hyphenated identifiers. The following properties receive special treatment:

| Key | Notes |
|---|---|
| `direction` | `row` or `column` |
| `align` | `start`, `center`, `end`, `stretch` |
| `justify` | `start`, `center`, `end`, `space-between`, `space-around` |
| `text-align` | `left`, `center`, `right`, `justify` |
| `padding` | 1, 2, or 4 space-separated numbers (CSS box model order) |
| `margin` | same as `padding` |
| `transition` | `<property> <duration_s> <easing>` |
| `focusable` | `true` / `false` |
| `draggable` | `true` / `false` |

All other keys are looked up in the framework property registry.

---

## Applying Styles

`apply` merges a style's properties into the current node. Multiple `apply` lines are allowed; later ones override earlier ones. Local properties always override applied styles.

```
node:
    apply card
    apply card-hover
    width = 100        // wins over anything in the styles
```

---

## Event Handlers

`on <event>:` blocks declare property overrides that are animated onto the node when the named event fires. The easing and duration are controlled by `transition` declarations on the node.

```
node:
    background = #1E1E2E
    transition = background 0.2 ease-out

    on hover:
        background = #2A2A3E

    on press:
        background = #111122
```

Events can appear inside styles and are inherited when the style is applied:

```
style button:
    padding    = 8 16
    background = #3B82F6
    transition = background 0.15 ease-out

    on hover:
        background = #60A5FA

    on press:
        background = #2563EB
```

Available events are registered by the framework via `FRAMEWORK.get_event()`. Common names: `hover`, `press`, `focus`, `blur`, `drag`.

---

## Nesting

Nodes nest by indentation. Any property type (`apply`, properties, events, child nodes) may appear at any depth in any order, though the conventional order is: `apply` lines, then properties, then events, then children.

```
node:
    direction = column
    gap       = 12

    text:
        content   = "mem 412 MB"
        font-size = 10
        font-family = "monospace"
        text-color = text-muted

    node:
        direction = row
        gap       = 4

        text:
            content = "▲"
        text:
            content = "1.2%"
```

---

## Root

`root:` is a reserved node tag. Its properties are applied directly to the top-level scene node rather than creating a new child.

```
root:
    direction = column
    padding   = 24
    gap       = 16

    node:
        // ...
```

---

## Complete Example

```
// ── Variables ──────────────────────────────────────────────────
bg-base    = #1E1E2E
bg-surface = #2A2A3E
text-main  = #CDD6F4
text-muted = #9CA3AF
accent     = #89B4FA

// ── Styles ─────────────────────────────────────────────────────
style surface:
    background    = bg-surface
    border-radius = 6
    padding       = 12

style interactive:
    transition = background 0.15 ease-out

    on hover:
        background = #353550

    on press:
        background = #111122

// ── Tree ───────────────────────────────────────────────────────
root:
    background = bg-base
    direction  = column
    padding    = 24
    gap        = 16

    node panel:
        apply surface
        apply interactive
        direction = row
        gap       = 8

        text:
            content     = "mem"
            font-size   = 10
            font-family = "monospace"
            text-color  = text-muted

        text:
            content     = "412 MB"
            font-size   = 10
            font-family = "monospace"
            text-color  = text-main
```

---

## Loading

```cpp
// Register any custom node types before calling load().
lintel::register_node("sparkline", [] (Node& parent, const NodeDecl&) {
    return SparklineNode();
});

Node root = lintel::load("ui/dashboard.lt");
```

Parse errors are printed to `stderr` and the loader continues with a partial tree where possible. The returned `Node` is always valid.