// parser.cpp
//
// Implementation of the cpage document language parser.
//
// Design goals
// ------------
//  • deterministic parsing (no exceptions during normal value parsing)
//  • minimal string copying
//  • explicit invariants for indentation-based structure
//  • extensible node and property registries
//
// Parsing pipeline
// ----------------
//  1. Read file
//  2. Extract variable declarations  ($name = value)
//  3. Substitute variables
//  4. Lex meaningful lines           (indent + trimmed content)
//  5. Build AST with recursive descent
//     - top-level  style <name>:  blocks are collected into a StyleTable
//     - node blocks may contain  apply <name>  and  on <event>:  lines
//  6. Convert AST -> runtime node tree
//     - apply inlines template properties (local props override)
//     - on <event>  wires a lambda via node.on(Event::type, ...)
//
// Extended grammar
// ----------------
//  document      := (style_block | root_block)*
//
//  style_block   := 'style' IDENT ':' NEWLINE
//                   (INDENT property | INDENT on_block)*
//
//  root_block    := 'root' ':' NEWLINE
//                   (INDENT node_body)*
//
//  node_block    := TYPE ['"' NAME '"'] ':' NEWLINE
//                   (INDENT node_body)*
//
//  node_body     := property
//                 | 'apply' IDENT
//                 | on_block
//                 | node_block
//
//  on_block      := 'on' IDENT ':' NEWLINE
//                   (INDENT property)*
//
//  property      := IDENT '=' value
//

#include "lexer.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <optional>

namespace lintel {

// ----------------------------------------------------------------
// Value-function registry  (definition; declaration is in lexer.h)
// ----------------------------------------------------------------
namespace detail {
std::unordered_map<std::string, ValueFuncParser> s_value_func_registry;
} // namespace detail

// ----------------------------------------------------------------
// Registries
// ----------------------------------------------------------------
using NodeFactory = std::function<Node(Node&, const ASTNode&)>;
static std::unordered_map<std::string, NodeFactory> s_node_registry;

void register_node(const std::string& type, NodeFactory f) {
    s_node_registry[type] = std::move(f);
}
void register_value_function(const std::string& fname, detail::ValueFuncParser f) {
    detail::s_value_func_registry[fname] = std::move(f);
}

// ----------------------------------------------------------------
// Built-in registrations
// ----------------------------------------------------------------
static bool s_registered = ([] {
    register_node("node", [] (Node&, const ASTNode&) { return Node();      });
    register_node("text", [] (Node&, const ASTNode&) { return TextNode();  });
    register_node("graph", [] (Node&, const ASTNode&) { return GraphNode(); });

    // Extensible colour function; users can register "hsl", "rgba", etc. the same way.
    register_value_function("rgb", [] (std::string_view args) -> AttribValue {
        std::vector<float> vals;
        size_t pos = 0;
        while (pos < args.size()) {
            size_t next = args.find(',', pos);
            auto part = detail::trim(next == std::string_view::npos
                                     ? args.substr(pos)
                                     : args.substr(pos, next - pos));
            if (!part.empty()) {
                float f{};
                if (std::from_chars(part.data(), part.data() + part.size(), f).ec
                    == std::errc())
                    vals.push_back(f);
                else
                    throw std::runtime_error("malformed rgb component");
            }
            if (next == std::string_view::npos) break;
            pos = next + 1;
        }
        if (vals.size() == 3) vals.push_back(1.0f);
        else if (vals.size() != 4)
            throw std::runtime_error("rgb expects 3 or 4 components");
        return Color::rgb(vals[0], vals[1], vals[2], vals[3]);
    });
    return true;
})();

// ----------------------------------------------------------------
// Style table  (name -> props + per-event deltas)
// ----------------------------------------------------------------
struct StyleEntry {
    std::vector<Property>                            props;
    std::unordered_map<std::string, StyleDelta>      event_styles;
};
using StyleTable = std::unordered_map<std::string, StyleEntry>;

// ----------------------------------------------------------------
// Recursive-descent parser
// ----------------------------------------------------------------
namespace {

struct RDParser {
    const std::vector<Line>& lines;
    size_t                   pos = 0;

    bool        done()  const { return pos >= lines.size(); }
    const Line& cur()   const { return lines[pos]; }

    // ---- helpers ------------------------------------------------

    // Returns true if `content` looks like "on <word>:" or "on <word>"
    static bool is_on_block(std::string_view content) {
        return content.starts_with("on ") &&
            (content.back() == ':' ||
             content.find(' ', 3) == std::string_view::npos);
    }

    // Returns true if `content` looks like "apply <word>"
    static bool is_apply(std::string_view content) {
        return content.starts_with("apply ");
    }

    // Returns true if the line is a property assignment (key = value)
    // and the '=' comes before any ':' (to avoid matching node declarations).
    static bool is_property(std::string_view content) {
        auto eq = content.find('=');
        auto col = content.find(':');
        return eq != std::string_view::npos &&
            (col == std::string_view::npos || eq < col);
    }

    // Extract the event name from "on <name>[:]"
    static std::string event_name(std::string_view content) {
        // strip leading "on "
        auto rest = detail::trim(content.substr(3));
        if (!rest.empty() && rest.back() == ':')
            rest.remove_suffix(1);
        return std::string(detail::trim(rest));
    }

    // Parse one property line; pos must already point at it.
    Property parse_property_line() {
        const std::string& text = cur().content;
        auto eq_pos = text.find('=');
        auto key = detail::trim(std::string_view(text).substr(0, eq_pos));
        auto val = detail::trim(std::string_view(text).substr(eq_pos + 1));
        ++pos;
        return { std::string(key), detail::parse_value(val) };
    }

    // ---- on-block -----------------------------------------------
    // Parse "on <event>:" and its indented property children.
    // Returns {event_name, delta_props}.
    //
    // child_indent is derived from the "on" line itself, not from any
    // parent context — this is what was causing the off-by-one indentation
    // error when on-blocks appeared inside already-indented nodes.
    std::pair<std::string, StyleDelta> parse_on_block() {
        std::string name = event_name(cur().content);
        int         on_indent = cur().indent;       // e.g. 2 inside a node at level 1
        int         child_indent = on_indent + 1;   // properties must be one level deeper
        ++pos; // consume the "on ..." line

        StyleDelta delta;
        while (!done() && cur().indent == child_indent) {
            if (is_property(cur().content))
                delta.push_back(parse_property_line());
            else
                throw std::runtime_error(
                "unexpected content inside 'on' block: " + cur().content);
        }
        return { name, std::move(delta) };
    }

    // ---- node-block ---------------------------------------------
    ASTNode parse_node() {
        ASTNode node;
        int base = cur().indent;
        int child = base + 1;

        std::string decl = cur().content;
        if (decl.back() == ':') decl.pop_back();

        // Extract optional "name" from the declaration  (text "title":)
        auto q1 = decl.find('"');
        if (q1 != std::string::npos) {
            auto q2 = decl.find('"', q1 + 1);
            if (q2 != std::string::npos) {
                node.name = decl.substr(q1 + 1, q2 - q1 - 1);
                decl.erase(q1, q2 - q1 + 1);
            }
        }
        node.type = std::string(detail::trim(decl));
        ++pos; // consume node declaration line

        while (!done()) {
            if (cur().indent < child) break;
            if (cur().indent > child)
                throw std::runtime_error("invalid indentation near: " + cur().content);

            std::string_view content = cur().content;

            if (is_apply(content)) {
                // "apply <style-name>"
                node.applies.push_back(
                    std::string(detail::trim(content.substr(6))));
                ++pos;
            }
            else if (is_on_block(content)) {
                auto [name, delta] = parse_on_block();
                node.event_styles[name] = std::move(delta);
            }
            else if (is_property(content)) {
                node.props.push_back(parse_property_line());
            }
            else {
                // Must be a child node block
                node.children.push_back(parse_node());
            }
        }
        return node;
    }

    // ---- style-block --------------------------------------------
    // style <name>:
    //     prop = value
    //     on <event>:
    //         prop = value
    std::pair<std::string, StyleEntry> parse_style_block() {
        int base = cur().indent; // should be 0
        int child = base + 1;

        // Extract style name from "style <name>:"
        std::string decl = cur().content;
        if (decl.back() == ':') decl.pop_back();
        std::string name(detail::trim(
            std::string_view(decl).substr(5))); // skip "style"
        ++pos;

        StyleEntry entry;
        while (!done()) {
            if (cur().indent < child) break;
            if (cur().indent > child)
                throw std::runtime_error("invalid indentation in style block");

            std::string_view content = cur().content;

            if (is_on_block(content)) {
                auto [ev_name, delta] = parse_on_block();
                entry.event_styles[ev_name] = std::move(delta);
            }
            else if (is_property(content)) {
                entry.props.push_back(parse_property_line());
            }
            else {
                throw std::runtime_error(
                    "unexpected content in style block: " + cur().content);
            }
        }
        return { name, std::move(entry) };
    }

    // ---- document -----------------------------------------------
    // Collects style blocks into `styles`, then returns the root node.
    ASTNode parse_document(StyleTable& styles) {
        while (!done()) {
            std::string_view content = cur().content;
            if (content.starts_with("style ")) {
                auto [name, entry] = parse_style_block();
                styles[name] = std::move(entry);
            }
            else if (content == "root" || content == "root:") {
                return parse_node();
            }
            else {
                ++pos; // skip unrecognised top-level lines
            }
        }
        throw std::runtime_error("no root node found");
    }
};

} // anonymous namespace

// ----------------------------------------------------------------
// Builder  (AST -> runtime node tree)
// ----------------------------------------------------------------
static void build_children(Node&, const std::vector<ASTNode>&, const StyleTable&);

// ---- Event name -> Event enum dispatch table ----------------------------
//
// Covers every Event enumerator from lintel.h.  Unknown names are logged
// and skipped; no if-chain needed at call sites.
//
static const std::unordered_map<std::string, Event> k_event_names = {
    { "mouse-enter",   Event::MouseEnter   },
    { "mouse-leave",   Event::MouseLeave   },
    { "mouse-move",    Event::MouseMove    },
    { "mouse-down",    Event::MouseDown    },
    { "mouse-up",      Event::MouseUp      },
    { "click",         Event::Click        },
    { "double-click",  Event::DoubleClick  },
    { "right-click",   Event::RightClick   },
    { "scroll",        Event::Scroll       },
    { "focus",         Event::Focus        },
    { "blur",          Event::Blur         },
    { "key-down",      Event::KeyDown      },
    { "key-up",        Event::KeyUp        },
    { "char",          Event::Char         },
    { "drag-start",    Event::DragStart    },
    { "drag",          Event::Drag         },
    { "drag-end",      Event::DragEnd      },
    { "any",           Event::Any          },
};

static std::optional<Event> lookup_event(const std::string& name) {
    auto it = k_event_names.find(name);
    if (it == k_event_names.end()) return std::nullopt;
    return it->second;
}

// ---- Float property dispatch table -------------------------------------
//
// Each entry maps a property key to a setter on Node.
// Using std::function avoids repeated key comparisons inside apply_props.
//
using FloatSetter = std::function<void(Node&, float)>;
static const std::unordered_map<std::string, FloatSetter> k_float_props = {
    { "width",   [] (Node& n, float v) { n.width(v);          } },
    { "height",  [] (Node& n, float v) { n.height(v);         } },
    { "share",   [] (Node& n, float v) { n.share(v);          } },
    { "gap",     [] (Node& n, float v) { n.gap(v);            } },
    { "padding", [] (Node& n, float v) { n.padding(Edges(v)); } },
    { "margin",  [] (Node& n, float v) { n.margin(Edges(v));  } },
};

// ---- Bool property dispatch table --------------------------------------
using BoolSetter = std::function<void(Node&, bool)>;
static const std::unordered_map<std::string, BoolSetter> k_bool_props = {
    { "focusable", [] (Node& n, bool v) { n.focusable(v); } },
    { "draggable", [] (Node& n, bool v) { n.draggable(v); } },
    // { "editable",  [] (Node& n, bool v) { n.editable(v);  } },
};

// ---- Keyword (wstring) property dispatch tables ------------------------
//
// Each keyword property has its own value -> action map so that adding a new
// keyword variant is a one-line table entry.
//
static const std::unordered_map<std::wstring, std::function<void(Node&)>> k_direction_vals = {
    { L"row",    [] (Node& n) { n.row();    } },
    { L"column", [] (Node& n) { n.column(); } },
};
static const std::unordered_map<std::wstring, std::function<void(Node&)>> k_align_vals = {
    { L"start",   [] (Node& n) { n.align(Align::Start);   } },
    { L"center",  [] (Node& n) { n.align(Align::Center);  } },
    { L"end",     [] (Node& n) { n.align(Align::End);     } },
    { L"stretch", [] (Node& n) { n.align(Align::Stretch); } },
};
static const std::unordered_map<std::wstring, std::function<void(Node&)>> k_justify_vals = {
    { L"start",         [] (Node& n) { n.justify(Justify::Start);        } },
    { L"center",        [] (Node& n) { n.justify(Justify::Center);       } },
    { L"end",           [] (Node& n) { n.justify(Justify::End);          } },
    { L"space-between", [] (Node& n) { n.justify(Justify::SpaceBetween); } },
    { L"space-around",  [] (Node& n) { n.justify(Justify::SpaceAround);  } },
};

// Keyword properties: key -> (value table, default action when key absent).
// The default is invoked when the keyword is unrecognised.
struct KeywordProp {
    const std::unordered_map<std::wstring, std::function<void(Node&)>>* values;
    std::function<void(Node&)> fallback; // called when value is not in the table
};
static const std::unordered_map<std::string, KeywordProp> k_keyword_props = {
    { "direction", { &k_direction_vals, [] (Node& n) { n.row();                    } } },
    { "align",     { &k_align_vals,     [] (Node& n) { n.align(Align::Stretch);    } } },
    { "justify",   { &k_justify_vals,   [] (Node& n) { n.justify(Justify::Start);  } } },
};

// Apply a flat list of Property objects to a Node.
static void apply_props(Node& n, const std::vector<Property>& props) {
    for (const Property& p : props) {
        const AttribValue& v = p.val;
        const std::string& key = p.key;

        if (const float* f = std::get_if<float>(&v)) {
            auto it = k_float_props.find(key);
            if (it != k_float_props.end()) { it->second(n, *f); continue; }
        }
        else if (const bool* b = std::get_if<bool>(&v)) {
            auto it = k_bool_props.find(key);
            if (it != k_bool_props.end()) { it->second(n, *b); continue; }
        }
        else if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
            auto kit = k_keyword_props.find(key);
            if (kit != k_keyword_props.end()) {
                const KeywordProp& kp = kit->second;
                auto vit = kp.values->find(*w);
                if (vit != kp.values->end()) vit->second(n);
                else                         kp.fallback(n);
                continue;
            }
        }

        // Everything else (Color, unrecognised keys) goes to the Attributes map.
        n.attr().set(key, v);
    }
}

// Wire one event delta onto a node.
// The lambda captures the delta by value so the StyleTable can be freed after load().
static void wire_event(Node& n, const std::string& ev_name,
                       const StyleDelta& delta) {
    auto opt = lookup_event(ev_name);
    if (!opt) {
        std::cerr << "unknown event '" << ev_name << "' — skipped\n";
        return;
    }
    StyleDelta captured = delta; // copy for lambda capture
    n.on(*opt, [captured] (Node& self) {
        apply_props(self, captured);
    });
}

static void build_node(Node& parent, const ASTNode& ast, const StyleTable& styles) {
    auto it = s_node_registry.find(ast.type);
    if (it == s_node_registry.end()) {
        std::cerr << "unknown node type '" << ast.type << "' — skipped\n";
        return;
    }

    Node& n = parent.push(it->second(parent, ast));

    // 1. Apply style templates in declaration order.
    //    Each "apply" inlines the style's base props, then wires its event deltas.
    //    Later local props will override what the template set.
    for (const std::string& style_name : ast.applies) {
        auto sit = styles.find(style_name);
        if (sit == styles.end()) {
            std::cerr << "undefined style '" << style_name << "' — skipped\n";
            continue;
        }
        apply_props(n, sit->second.props);
        for (const auto& [ev_name, delta] : sit->second.event_styles)
            wire_event(n, ev_name, delta);
    }

    // 2. Apply node-local base properties (override template props).
    apply_props(n, ast.props);

    // 3. Wire node-local event deltas (override any same-event template delta).
    for (const auto& [ev_name, delta] : ast.event_styles)
        wire_event(n, ev_name, delta);

    // 4. Recurse into children.
    build_children(n, ast.children, styles);

    // 5. Register named node for lookup via CORE.
    if (!ast.name.empty())
        CORE.register_named(ast.name, WeakNode(n.handle()));
}

static void build_children(Node& parent, const std::vector<ASTNode>& children,
                           const StyleTable& styles) {
    for (const auto& c : children)
        build_node(parent, c, styles);
}

// ----------------------------------------------------------------
// Entry point
// ----------------------------------------------------------------
Node load(const char* path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error(std::string("cannot open file: ") + path);

    std::vector<std::string> raw;
    std::string ln;
    while (std::getline(file, ln))
        raw.push_back(std::move(ln));

    // Preprocess
    auto vars = detail::extract_vars(raw);
    detail::apply_vars(raw, vars);
    auto lines = lex_lines(raw);

    // Parse
    StyleTable styles;
    RDParser   p{ lines };
    ASTNode    root_ast = p.parse_document(styles);

    // Build runtime tree
    Node root;
    apply_props(root, root_ast.props);
    build_children(root, root_ast.children, styles);
    return root;
}

} // namespace lintel
