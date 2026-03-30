# Lintel UI DSL Documentation

## Overview

This Domain-Specific Language (DSL) defines a declarative UI layout and styling system for building structured interfaces. It combines:

* **Design tokens** (colors, constants)
* **Reusable styles**
* **Hierarchical layout (node tree)**
* **Stateful interactions (hover, focus, etc.)**

The syntax is indentation-based and designed for readability.

---

## 1. Design Tokens

Global variables define reusable values such as colors.

### Syntax

```
<name> = <value>
```

### Example

```dsl
accent-green = #00e5a0
bg-root      = #0a0b0d
text-muted   = #3a3f55
```

### Usage

Tokens can be referenced anywhere:

```dsl
background-color = bg-root
text-color = text-muted
```

---

## 2. Style Definitions

Reusable style blocks define common UI patterns.

### Syntax

```dsl
style <style-name>:
    <property> = <value>
    ...
```

### Example

```dsl
style btn:
    height = 32
    background-color = #141720
    border-radius = 3
```

---

## 3. Style Composition

Styles can inherit from other styles.

### Syntax

```dsl
apply <style-name>
```

### Example

```dsl
style btn-danger:
    apply btn
    background-color = bg-error
```

---

## 4. Properties

Properties define layout, appearance, and behavior.

### Common Layout Properties

| Property  | Description                 |
| --------- | --------------------------- |
| direction | `row` or `column`           |
| align     | Cross-axis alignment        |
| justify   | Main-axis alignment         |
| gap       | Space between children      |
| padding   | Inner spacing               |
| margin    | Outer spacing               |
| width     | Fixed width                 |
| height    | Fixed height                |
| share     | Flexible sizing (like flex) |

### Visual Properties

| Property         | Description      |
| ---------------- | ---------------- |
| background-color | Background color |
| border-color     | Border color     |
| border-weight    | Border thickness |
| border-radius    | Corner rounding  |

### Text Properties

| Property    | Description |
| ----------- | ----------- |
| content     | Text string |
| font-size   | Font size   |
| font-family | Font family |
| text-color  | Text color  |
| bold        | Boolean     |

### Behavior Properties

| Property   | Description               |
| ---------- | ------------------------- |
| focusable  | Enables focus interaction |
| editable   | Allows text editing       |
| transition | Animated property change  |

---

## 5. Event Handlers

Styles and nodes can define interaction states.

### Syntax

```dsl
on <event>:
    <property> = <value>
```

### Supported Events

* `mouse-enter`
* `mouse-leave`
* `mouse-down`
* `focus`
* `blur`

### Example

```dsl
on mouse-enter:
    background-color = #1e2444
```

---

## 6. Node System

UI is built as a tree of nodes.

### Syntax

```dsl
node "<name>":
    <properties>
    <children>
```

### Anonymous Nodes

```dsl
node:
    direction = row
```

---

## 7. Root Layout

The `root` block defines the entire UI.

### Syntax

```dsl
root:
    <properties>
    <children>
```

---

## 8. Text Nodes

Text elements are defined using `text`.

### Syntax

```dsl
text "<optional-name>":
    content = "..."
```

### Example

```dsl
text "title":
    content = "LINTEL RECV"
    font-size = 12
```

---

## 9. Specialized Nodes

### Graph

```dsl
graph "<name>":
    <properties>
```

Used for rendering charts.

### Example

```dsl
graph "throughput-graph":
    grid-color = #141820
```

---

## 10. Layout Concepts

### Flex-like Behavior

* `direction = row | column`
* `share = 1` → expands to fill available space
* Fixed vs flexible sizing supported

### Nesting

Layouts are hierarchical:

```dsl
node "container":
    direction = column

    node "child":
        height = 20
```

---

## 11. Reusability Patterns

### Base Style Pattern

```dsl
style base-chip:
    padding = 4 10

style chip-active:
    apply base-chip
    background-color = #141720
```

### Variant Pattern

* `chip-active`
* `chip-warn`
* `chip-inactive`

---

## 12. Example Structure

High-level UI composition:

```
root
 ├── topbar
 ├── source-strip
 ├── body
 │    ├── left-panel
 │    ├── center-panel
 │    └── right-panel
 └── statusbar
```

---

## 13. Key Features Summary

* Declarative UI tree
* Style inheritance (`apply`)
* Design tokens
* Built-in layout system (flex-like)
* Event-driven styling
* Strong readability via indentation

---

## 14. Best Practices

* Use **design tokens** for all colors and constants
* Define **base styles** and extend them
* Keep node trees **shallow and modular**
* Use `share` instead of hardcoding sizes where possible
* Group related UI into named nodes

---

## 15. Notes

* Order matters for overrides (later properties win)
* Multiple `transition` entries are allowed
* Events override base properties temporarily
* Styles are purely declarative (no logic)

---

## 16. File Extension

Recommended extension:

```
.lintel.ltl
```

or

```
.ui.ltl
```

---

## Conclusion

This DSL provides a clean, composable way to define UI structure, styling, and interactions in a single readable format—ideal for dashboards, tooling interfaces, and system UIs.
