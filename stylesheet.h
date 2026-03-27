#pragma once
// stylesheet.h
//
// StyleSheet — a reusable, AST-independent collection of named styles.
//
// Design goals
// ────────────
//  • Styles survive load().  User code can call sheet.apply(node, "button")
//    at any point after the file has been parsed, not just at tree-build time.
//
//  • Single source of truth for property dispatch.  The logic that converts a
//    key/value pair into a Node mutation (enum coercions, shorthand expansion,
//    transition installation) lives in StyleSheet::set_prop and is shared by
//    load(), runtime apply(), and any future programmatic style builder.
//
//  • No raw AST pointers.  Styles are fully resolved (PropValue, not Node*)
//    so the AST can be freed after load() returns.
//
// Usage after load()
// ──────────────────
//    auto [root, sheet] = lintel::load("ui.lt");
//    CORE.root.push(std::move(root));
//
//    // Later — dynamically styled node:
//    auto& btn = someParent.push();
//    sheet.apply(btn, "button");
//    sheet.apply(btn, "button-danger");  // override on top
//
// Building a sheet programmatically
// ──────────────────────────────────
//    StyleSheet sheet;
//    sheet.define("card", {
//        { "background-color", Color(0.1f, 0.1f, 0.12f, 1.f) },
//        { "corner-radius",    8.f },
//        { "padding",         std::wstring(L"12 16") },
//    });
//    sheet.define_handler("card", Event::Click, {
//        { "background-color", Color(0.15f, 0.15f, 0.18f, 1.f) },
//    });
//

#include "types.h"

namespace lintel {

// Forward declarations
class Node;
class INode;

// ---------------------------------------------------------------------------
// StyleSheet
// ---------------------------------------------------------------------------

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

private:
    std::unordered_map<std::string, Style> styles_;

    // Apply props to n using the given mode.
    static void apply_props(Node& n, const std::vector<Prop>& props, Mode mode);

    // Register event handlers on n so that each handler fires animate() on
    // its delta list when the event occurs.
    static void wire_handlers(Node& n, const std::vector<Handler>& handlers);
};

} // namespace lintel
