// parser.cpp
//
// Implementation of the cpage document language parser.
//
// Property pipeline change
// ------------------------
// String tokens are now mapped to Prop enum values once during apply_props
// rather than being compared at draw / layout time.  The k_prop_map table
// covers every directly assignable property; shorthand properties (padding,
// margin) and keyword-valued enum properties (direction, align, justify,
// text-align) are handled by named special-case blocks immediately before
// the generic lookup, exactly as they were with the old dispatch tables —
// just with Prop::xxx keys instead of Node fluent setter lambdas.
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
// Node factory registry
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
// Style table
// ----------------------------------------------------------------
struct StyleEntry {
    std::vector<Property>                       props;
    std::unordered_map<std::string, StyleDelta> event_styles;
};
using StyleTable = std::unordered_map<std::string, StyleEntry>;

// ----------------------------------------------------------------
// Recursive-descent parser  (unchanged from original)
// ----------------------------------------------------------------
namespace {

struct RDParser {
    const std::vector<Line>& lines;
    size_t                   pos = 0;

    bool        done() const { return pos >= lines.size(); }
    const Line& cur()  const { return lines[pos]; }

    static bool is_on_block(std::string_view content) {
        return content.starts_with("on ") &&
            (content.back() == ':' ||
             content.find(' ', 3) == std::string_view::npos);
    }

    static bool is_apply(std::string_view content) {
        return content.starts_with("apply ");
    }

    static bool is_property(std::string_view content) {
        auto eq = content.find('=');
        auto col = content.find(':');
        return eq != std::string_view::npos &&
            (col == std::string_view::npos || eq < col);
    }

    static std::string event_name(std::string_view content) {
        auto rest = detail::trim(content.substr(3));
        if (!rest.empty() && rest.back() == ':') rest.remove_suffix(1);
        return std::string(detail::trim(rest));
    }

    Property parse_property_line() {
        const std::string& text = cur().content;
        auto eq_pos = text.find('=');
        auto key = detail::trim(std::string_view(text).substr(0, eq_pos));
        auto val = detail::trim(std::string_view(text).substr(eq_pos + 1));
        ++pos;
        return { std::string(key), detail::parse_value(val) };
    }

    std::pair<std::string, StyleDelta> parse_on_block() {
        std::string name = event_name(cur().content);
        int         on_indent = cur().indent;
        int         child_indent = on_indent + 1;
        ++pos;

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

    ASTNode parse_node() {
        ASTNode node;
        int base = cur().indent;
        int child = base + 1;

        std::string decl = cur().content;
        if (decl.back() == ':') decl.pop_back();

        auto q1 = decl.find('"');
        if (q1 != std::string::npos) {
            auto q2 = decl.find('"', q1 + 1);
            if (q2 != std::string::npos) {
                node.name = decl.substr(q1 + 1, q2 - q1 - 1);
                decl.erase(q1, q2 - q1 + 1);
            }
        }
        node.type = std::string(detail::trim(decl));
        ++pos;

        while (!done()) {
            if (cur().indent < child) break;
            if (cur().indent > child)
                throw std::runtime_error("invalid indentation near: " + cur().content);

            std::string_view content = cur().content;

            if (is_apply(content)) {
                node.applies.push_back(std::string(detail::trim(content.substr(6))));
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
                node.children.push_back(parse_node());
            }
        }
        return node;
    }

    std::pair<std::string, StyleEntry> parse_style_block() {
        int base = cur().indent;
        int child = base + 1;

        std::string decl = cur().content;
        if (decl.back() == ':') decl.pop_back();
        std::string name(detail::trim(std::string_view(decl).substr(5)));
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
                ++pos;
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

// ---- Event name dispatch table ------------------------------------------
static const std::unordered_map<std::string, Event> k_event_names = {
    { "mouse-enter",  Event::MouseEnter  }, { "mouse-leave",  Event::MouseLeave  },
    { "mouse-move",   Event::MouseMove   }, { "mouse-down",   Event::MouseDown   },
    { "mouse-up",     Event::MouseUp     }, { "click",        Event::Click       },
    { "double-click", Event::DoubleClick }, { "right-click",  Event::RightClick  },
    { "scroll",       Event::Scroll      }, { "focus",        Event::Focus       },
    { "blur",         Event::Blur        }, { "key-down",     Event::KeyDown     },
    { "key-up",       Event::KeyUp       }, { "char",         Event::Char        },
    { "drag-start",   Event::DragStart   }, { "drag",         Event::Drag        },
    { "drag-end",     Event::DragEnd     }, { "any",          Event::Any         },
};

static std::optional<Event> lookup_event(const std::string& name) {
    auto it = k_event_names.find(name);
    return (it == k_event_names.end()) ? std::nullopt : std::optional(it->second);
}

// ---- Prop lookup table --------------------------------------------------
//
// Every property that maps directly from a string key to a Prop enum value
// lives here.  Shorthand properties (padding, margin) and keyword-valued
// enum properties (direction, align, justify, text-align) are handled by
// named special-case blocks in apply_props before this table is consulted.
//
static const std::unordered_map<std::string, Prop> k_prop_map = {
    // Visual
    { "background-color", Prop::BackgroundColor },
    { "border-color",     Prop::BorderColor     },
    { "border-weight",    Prop::BorderWeight     },
    { "corner-radius",    Prop::CornerRadius     },
    { "text-color",       Prop::TextColor        },
    { "font-size",        Prop::FontSize         },
    { "font-family",      Prop::FontFamily       },
    // Box model — individual sides
    { "width",            Prop::Width            },
    { "height",           Prop::Height           },
    { "padding-top",      Prop::PaddingTop       },
    { "padding-right",    Prop::PaddingRight     },
    { "padding-bottom",   Prop::PaddingBottom    },
    { "padding-left",     Prop::PaddingLeft      },
    { "margin-top",       Prop::MarginTop        },
    { "margin-right",     Prop::MarginRight      },
    { "margin-bottom",    Prop::MarginBottom     },
    { "margin-left",      Prop::MarginLeft       },
    { "gap",              Prop::Gap              },
    { "share",            Prop::Share            },
    // Text
    { "bold",             Prop::Bold             },
    { "italic",           Prop::Italic           },
    { "wrap",             Prop::Wrap             },
    { "editable",         Prop::Editable         },
    // Graph / chart
    { "grid-color",       Prop::GridColor        },
    { "grid-weight",      Prop::GridWeight       },
    { "label-color",      Prop::LabelColor       },
    { "label-font-size",  Prop::LabelFontSize    },
    // Misc
    { "opacity",          Prop::Opacity          },
};

// ---- apply_props --------------------------------------------------------
//
// Converts each parsed Property (string key + AttribValue) into a typed
// attr.set(Prop, ...) call.  The string-to-Prop conversion happens here,
// once at tree-build time; no string lookup occurs during layout or draw.
//
// Ordering:
//   1. Shorthand expansion (padding, margin) → calls Node fluent setters.
//   2. Keyword/enum coercions (direction, align, justify, text-align).
//   3. Behaviour flags (focusable, draggable) → direct INode flag writes.
//   4. Generic k_prop_map lookup → attr.set(Prop, value).
//
static void apply_props(Node& n, const std::vector<Property>& props) {
    for (const Property& p : props) {
        const AttribValue& v = p.val;
        const std::string& key = p.key;

        // ── 1. Shorthand expansion ─────────────────────────────────────────
        //
        // "padding = 8" sets all four sides; "margin = 4 8" uses x/y shorthands
        // from the Edges constructors.  Only float values make sense here.
        //
        if (key == "padding") {
            if (const float* f = std::get_if<float>(&v)) n.padding(Edges(*f));
            continue;
        }
        if (key == "margin") {
            if (const float* f = std::get_if<float>(&v)) n.margin(Edges(*f));
            continue;
        }

        // ── 2. Keyword/enum coercions ──────────────────────────────────────
        //
        // Enum-valued props are stored as float (integer cast) in attr so they
        // fit in the AttribValue variant.  The accessors in inode.cpp cast back.
        //
        if (key == "direction") {
            if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
                Direction d = (*w == L"row") ? Direction::Row : Direction::Column;
                n.attr().set(Prop::Direction,
                             static_cast<float>(static_cast<int>(d)));
            }
            continue;
        }
        if (key == "align") {
            if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
                Align a = Align::Stretch;
                if (*w == L"start")   a = Align::Start;
                if (*w == L"center")  a = Align::Center;
                if (*w == L"end")     a = Align::End;
                n.attr().set(Prop::AlignItems,
                             static_cast<float>(static_cast<int>(a)));
            }
            continue;
        }
        if (key == "justify") {
            if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
                Justify j = Justify::Start;
                if (*w == L"center")        j = Justify::Center;
                if (*w == L"end")           j = Justify::End;
                if (*w == L"space-between") j = Justify::SpaceBetween;
                if (*w == L"space-around")  j = Justify::SpaceAround;
                n.attr().set(Prop::JustifyItems,
                             static_cast<float>(static_cast<int>(j)));
            }
            continue;
        }
        if (key == "text-align") {
            if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
                TextAlign ta = TextAlign::Left;
                if (*w == L"center")  ta = TextAlign::Center;
                if (*w == L"right")   ta = TextAlign::Right;
                if (*w == L"justify") ta = TextAlign::Justify;
                n.attr().set(Prop::TextAlign,
                             static_cast<float>(static_cast<int>(ta)));
            }
            continue;
        }

        // ── 3. Behaviour flags ─────────────────────────────────────────────
        //
        // focusable and draggable are stored directly on INode, not in attr.
        //
        if (key == "focusable") {
            if (const bool* b = std::get_if<bool>(&v)) n.focusable(*b);
            continue;
        }
        if (key == "draggable") {
            if (const bool* b = std::get_if<bool>(&v)) n.draggable(*b);
            continue;
        }

        // ── 4. Generic k_prop_map lookup ──────────────────────────────────
        //
        // All remaining recognised properties route directly through attr.set().
        // Unknown keys are silently discarded — no arbitrary string storage.
        //
        auto it = k_prop_map.find(key);
        if (it != k_prop_map.end())
            n.attr().set(it->second, v);
    }
}

// Wire one event delta onto a node.
static void wire_event(Node& n, const std::string& ev_name,
                       const StyleDelta& delta) {
    auto opt = lookup_event(ev_name);
    if (!opt) {
        std::cerr << "unknown event '" << ev_name << "' — skipped\n";
        return;
    }
    StyleDelta captured = delta;
    n.on(*opt, [captured] (WeakNode self) {
        apply_props(self.as(), captured);
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

    // 3. Wire node-local event deltas.
    for (const auto& [ev_name, delta] : ast.event_styles)
        wire_event(n, ev_name, delta);

    // 4. Recurse into children.
    build_children(n, ast.children, styles);

    // 5. Register named node.
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

    auto vars = detail::extract_vars(raw);
    detail::apply_vars(raw, vars);
    auto lines = lex_lines(raw);

    StyleTable styles;
    RDParser   p{ lines };
    ASTNode    root_ast = p.parse_document(styles);

    Node root;
    apply_props(root, root_ast.props);
    build_children(root, root_ast.children, styles);
    return root;
}

} // namespace lintel
