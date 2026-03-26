// load.cpp — framework integration layer.
//
// Orchestration:
//   1. Parse source  → AST              (Parser)
//   2. Resolve vars / styles            (StyleResolver visitor, pass 1)
//   3. Build runtime scene graph        (TreeBuilder,           pass 2)

#include "visitor.h"
#include "parser.h"
#include "inode.h"
#include "framework.h"

#include <charconv>
#include <cwctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

namespace lintel::parser {


static bool s_registered = ([] {
    FRAMEWORK.register_node<lintel::Node>("node");
    FRAMEWORK.register_node<lintel::TextNode>("text");
    FRAMEWORK.register_node<lintel::GraphNode>("graph");
    FRAMEWORK.register_node<lintel::ImageNode>("image");

    /*
    register_value_function("rgb", [] (std::string_view args) -> AttribValue {
        std::vector<float> vals;
        for (size_t pos = 0; pos < args.size(); ) {
            size_t next = args.find(',', pos);
            auto part = trim(next == std::string_view::npos
                             ? args.substr(pos) : args.substr(pos, next - pos));
            float f{};
            if (!part.empty() &&
                std::from_chars(part.data(), part.data() + part.size(), f).ec == std::errc())
                vals.push_back(f);
            if (next == std::string_view::npos) break;
            pos = next + 1;
        }
        if (vals.size() == 3) vals.push_back(1.f);
        else if (vals.size() != 4)
            throw std::runtime_error("rgb() expects 3 or 4 components");
        return Color::rgb(vals[0], vals[1], vals[2], vals[3]);
    });

    register_value_function("hsl", [] (std::string_view args) -> AttribValue {
        std::vector<float> vals;
        for (size_t pos = 0; pos < args.size(); ) {
            size_t next = args.find(',', pos);
            auto part = trim(next == std::string_view::npos
                             ? args.substr(pos) : args.substr(pos, next - pos));
            float f{};
            if (!part.empty())
                std::from_chars(part.data(), part.data() + part.size(), f);
            vals.push_back(f);
            if (next == std::string_view::npos) break;
            pos = next + 1;
        }
        if (vals.size() == 3) vals.push_back(1.f);
        else if (vals.size() != 4)
            throw std::runtime_error("hsl() expects 3 or 4 components");
        return Color::hsl(vals[0], vals[1], vals[2], vals[3]);
    });

    register_value_function("animate", [] (std::string_view args) -> AttribValue {
        std::vector<size_t> comma_pos;
        int depth = 0;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == '(') ++depth;
            else if (args[i] == ')') --depth;
            else if (args[i] == ',' && depth == 0) comma_pos.push_back(i);
        }
        if (comma_pos.size() < 2)
            throw std::runtime_error("animate() expects 3 arguments: value, duration, easing");

        const size_t c1 = comma_pos[comma_pos.size() - 2];
        const size_t c2 = comma_pos[comma_pos.size() - 1];

        const auto val_part = trim(args.substr(0, c1));
        const auto dur_part = trim(args.substr(c1 + 1, c2 - c1 - 1));
        const auto ease_part = trim(args.substr(c2 + 1));

        float dur = 0.15f;
        std::from_chars(dur_part.data(), dur_part.data() + dur_part.size(), dur);

        Easing e = Easing::EaseOut;
        if (ease_part == "linear")      e = Easing::Linear;
        else if (ease_part == "ease-in")     e = Easing::EaseIn;
        else if (ease_part == "ease-in-out") e = Easing::EaseInOut;
        else if (ease_part == "spring")      e = Easing::Spring;

        AnimateDescriptor ad;
        const AttribValue parsed = parse_value(val_part);
        if (const float* f = std::get_if<float>(&parsed))      ad.target = *f;
        else if (const Color* c = std::get_if<Color>(&parsed)) ad.target = *c;
        else
            throw std::runtime_error("animate(): target must be a float or color");
        ad.duration = dur;
        ad.easing = e;
        return ad;
    });
    */

    return true;
})();

static AttribValue node_to_attrib(const Node* node, const StyleResolver& res) {
    if (!node) return std::wstring{};
    switch (node->kind) {
        case NodeKind::NumExpr:
            return node->as<NumExpr>().to_number();
        case NodeKind::BoolExpr:
            return node->as<BoolExpr>().value;
        case NodeKind::HexExpr:
            return node->as<HexExpr>().to_color();

        case NodeKind::IdentExpr:
        {
            const std::string& name = node->as<IdentExpr>().name;
            if (const Node* target = res.var(name))
                return node_to_attrib(target, res);
            return std::wstring(name.begin(), name.end());
        }

        case NodeKind::ListExpr:
        {
            std::wostringstream oss;
            bool first = true;
            for (const Node* item : node->as<ListExpr>().list) {
                if (!first) oss << L' ';
                std::string s = res.resolve_str(item);
                oss << std::wstring(s.begin(), s.end());
                first = false;
            }
            return oss.str();
        }

        default:
            return std::wstring{};
    }
}

// ─── Inherited properties ─────────────────────────────────────────────────────

struct InheritedProps {
    std::optional<std::wstring> font_family;
    std::optional<float>        font_size;
    std::optional<Color>        text_color;
    std::optional<bool>         bold;
    std::optional<bool>         italic;
    std::optional<bool>         wrap;
    std::optional<float>        opacity;
};

static InheritedProps derive_inherited(const lintel::Node& n, const InheritedProps& parent) {
    InheritedProps out = parent;
    const Attributes& a = const_cast<lintel::Node&>(n).attr();
    if (const auto* v = a.get<std::wstring>(property::FontFamily)) out.font_family = *v;
    if (const auto* v = a.get<float>(property::FontSize))   out.font_size = *v;
    if (const auto* v = a.get<Color>(property::TextColor))  out.text_color = *v;
    if (const auto* v = a.get<bool>(property::Bold))       out.bold = *v;
    if (const auto* v = a.get<bool>(property::Italic))     out.italic = *v;
    if (const auto* v = a.get<bool>(property::Wrap))       out.wrap = *v;
    if (const auto* v = a.get<float>(property::Opacity))    out.opacity = *v;
    return out;
}

static void apply_inherited(lintel::Node& n, const InheritedProps& inh) {
    Attributes& a = n.attr();
    if (inh.font_family && !a.has(property::FontFamily)) a.set(property::FontFamily, *inh.font_family);
    if (inh.font_size && !a.has(property::FontSize))   a.set(property::FontSize, *inh.font_size);
    if (inh.text_color && !a.has(property::TextColor))  a.set(property::TextColor, *inh.text_color);
    if (inh.bold && !a.has(property::Bold))        a.set(property::Bold, *inh.bold);
    if (inh.italic && !a.has(property::Italic))      a.set(property::Italic, *inh.italic);
    if (inh.wrap && !a.has(property::Wrap))        a.set(property::Wrap, *inh.wrap);
    if (inh.opacity && !a.has(property::Opacity))     a.set(property::Opacity, *inh.opacity);
}

// ─── Shorthand expansion ──────────────────────────────────────────────────────

static Edges parse_edges(const AttribValue& raw) {
    if (const float* f = std::get_if<float>(&raw))
        return Edges(*f);

    if (const std::wstring* ws = std::get_if<std::wstring>(&raw)) {
        std::vector<float> vals;
        size_t i = 0;
        while (i < ws->size()) {
            while (i < ws->size() && std::iswspace((*ws)[i])) ++i;
            if (i >= ws->size()) break;
            size_t j = i;
            while (j < ws->size() && !std::iswspace((*ws)[j])) ++j;
            std::wstring tok = ws->substr(i, j - i);
            std::string  narrow(tok.begin(), tok.end());
            float f{};
            if (std::from_chars(narrow.data(), narrow.data() + narrow.size(), f).ec == std::errc())
                vals.push_back(f);
            i = j;
        }
        if (vals.size() == 1) return Edges(vals[0]);
        if (vals.size() == 2) return Edges(vals[0], vals[1]);
        if (vals.size() == 4) return Edges(vals[0], vals[1], vals[2], vals[3]);
        std::cerr << "padding/margin: expected 1, 2 or 4 values, got "
            << vals.size() << " — defaulting to 0\n";
    }
    return Edges(0.f);
}

// ─── Transition installation ──────────────────────────────────────────────────

static std::string wstr_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(),
                        out.data(), n, nullptr, nullptr);
    return out;
}

static void install_transition(lintel::Node& n, const std::wstring& spec_w) {
    const std::string spec = wstr_to_utf8(spec_w);
    std::vector<std::string_view> tokens;
    for (size_t i = 0; i < spec.size(); ) {
        while (i < spec.size() && std::isspace((unsigned char) spec[i])) ++i;
        if (i >= spec.size()) break;
        size_t j = i;
        while (j < spec.size() && !std::isspace((unsigned char) spec[j])) ++j;
        tokens.push_back(std::string_view(spec).substr(i, j - i));
        i = j;
    }
    if (tokens.empty()) return;

    auto pit = FRAMEWORK.get_property(std::string(tokens[0]));
    if (pit == 0) {
        std::cerr << "transition: unknown property '" << tokens[0] << "' — skipped\n";
        return;
    }
    TransitionSpec ts;
    if (tokens.size() >= 2)
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), ts.duration);
    if (tokens.size() >= 3) {
        const auto& ek = tokens[2];
        if (ek == "linear")      ts.easing = Easing::Linear;
        else if (ek == "ease-in")     ts.easing = Easing::EaseIn;
        else if (ek == "ease-out")    ts.easing = Easing::EaseOut;
        else if (ek == "ease-in-out") ts.easing = Easing::EaseInOut;
        else if (ek == "spring")      ts.easing = Easing::Spring;
    }
    n.handle<INode>()->transitions_[pit] = ts;
}

// ─── apply_one_prop — inner dispatch ─────────────────────────────────────────
//
// Applies a single key/value pair to a runtime Node.  Both apply_props and
// apply_style_props delegate here so the enum-coercion logic lives in exactly
// one place.

static void apply_one_prop(lintel::Node& n, const std::string& key, const AttribValue& v) {
    if (key == "transition") {
        if (const std::wstring* w = std::get_if<std::wstring>(&v))
            install_transition(n, *w);
        return;
    }
    if (key == "padding") { n.padding(parse_edges(v)); return; }
    if (key == "margin") { n.margin(parse_edges(v)); return; }

    if (key == "direction") {
        if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
            Direction d = (*w == L"row") ? Direction::Row : Direction::Column;
            n.attr().set(property::Direction, static_cast<float>(static_cast<int>(d)));
        }
        return;
    }
    if (key == "align") {
        if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
            Align a = Align::Stretch;
            if (*w == L"start")  a = Align::Start;
            if (*w == L"center") a = Align::Center;
            if (*w == L"end")    a = Align::End;
            n.attr().set(property::AlignItems, static_cast<float>(static_cast<int>(a)));
        }
        return;
    }
    if (key == "justify") {
        if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
            Justify j = Justify::Start;
            if (*w == L"center")        j = Justify::Center;
            if (*w == L"end")           j = Justify::End;
            if (*w == L"space-between") j = Justify::SpaceBetween;
            if (*w == L"space-around")  j = Justify::SpaceAround;
            n.attr().set(property::JustifyItems, static_cast<float>(static_cast<int>(j)));
        }
        return;
    }
    if (key == "text-align") {
        if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
            TextAlign ta = TextAlign::Left;
            if (*w == L"center")  ta = TextAlign::Center;
            if (*w == L"right")   ta = TextAlign::Right;
            if (*w == L"justify") ta = TextAlign::Justify;
            n.attr().set(property::TextAlign, static_cast<float>(static_cast<int>(ta)));
        }
        return;
    }
    if (key == "focusable") { if (const bool* b = std::get_if<bool>(&v)) n.focusable(*b); return; }
    if (key == "draggable") { if (const bool* b = std::get_if<bool>(&v)) n.draggable(*b); return; }

    if (auto p = FRAMEWORK.get_property(key); p != 0)
        n->apply(p, v);
}

// ─── Resolved prop — a baked key/value pair, AST-lifetime independent ───────
// Produced during scene construction by resolving all variable references
// eagerly.  Event lambdas capture these directly so no reference back into
// the StyleResolver (which lives only for the duration of load()) is needed.

struct ResolvedProp {
    std::string  key;
    AttribValue  value;
};
using ResolvedProps = std::vector<ResolvedProp>;

static ResolvedProps resolve_props(const std::vector<Node*>& props, const StyleResolver& res) {
    ResolvedProps out;
    for (Node* child : props) {
        if (!child || child->kind != NodeKind::PropDecl) continue;
        const PropDecl& pd = child->as<PropDecl>();
        out.push_back({ pd.property, node_to_attrib(pd.value, res) });
    }
    return out;
}

// ─── animate_one_prop ─────────────────────────────────────────────────────────
// Same structure as apply_one_prop but routes through INode::animate_prop.

static void animate_one_prop(lintel::Node& n, INode* impl, const std::string& key, const AttribValue& v) {
    if (key == "transition") return; // transitions are installed, not animated
    if (key == "padding") { n.padding(parse_edges(v)); return; }
    if (key == "margin") { n.margin(parse_edges(v)); return; }

    if (key == "direction") {
        if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
            Direction d = (*w == L"row") ? Direction::Row : Direction::Column;
            impl->animate_prop(property::Direction, static_cast<float>(static_cast<int>(d)));
        }
        return;
    }
    if (key == "align") {
        if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
            Align a = Align::Stretch;
            if (*w == L"start")  a = Align::Start;
            if (*w == L"center") a = Align::Center;
            if (*w == L"end")    a = Align::End;
            impl->animate_prop(property::AlignItems, static_cast<float>(static_cast<int>(a)));
        }
        return;
    }
    if (key == "justify") {
        if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
            Justify j = Justify::Start;
            if (*w == L"center")        j = Justify::Center;
            if (*w == L"end")           j = Justify::End;
            if (*w == L"space-between") j = Justify::SpaceBetween;
            if (*w == L"space-around")  j = Justify::SpaceAround;
            impl->animate_prop(property::JustifyItems, static_cast<float>(static_cast<int>(j)));
        }
        return;
    }
    if (key == "text-align") {
        if (const std::wstring* w = std::get_if<std::wstring>(&v)) {
            TextAlign ta = TextAlign::Left;
            if (*w == L"center")  ta = TextAlign::Center;
            if (*w == L"right")   ta = TextAlign::Right;
            if (*w == L"justify") ta = TextAlign::Justify;
            impl->animate_prop(property::TextAlign, static_cast<float>(static_cast<int>(ta)));
        }
        return;
    }
    if (key == "focusable") { if (const bool* b = std::get_if<bool>(&v)) n.focusable(*b); return; }
    if (key == "draggable") { if (const bool* b = std::get_if<bool>(&v)) n.draggable(*b); return; }

    if (auto p = FRAMEWORK.get_property(key); p != 0)
        impl->animate_prop(p, v);
}

// ─── Public apply surfaces ────────────────────────────────────────────────────

// Apply all PropDecl children of a node's prop list.
// Non-PropDecl children (NodeDecl, OnDecl, ApplyExpr) are silently skipped —
// they're handled by different parts of the TreeBuilder.
static void apply_props(lintel::Node& n, const std::vector<Node*>& props, const StyleResolver& res) {
    for (Node* child : props) {
        if (!child || child->kind != NodeKind::PropDecl) continue;
        const PropDecl& pd = child->as<PropDecl>();
        apply_one_prop(n, pd.property, node_to_attrib(pd.value, res));
    }
}

// Apply a PropertyMap produced by StyleResolver — used when applying a named
// style to a node.  Avoids any need to reconstruct PropDecl objects.
static void apply_style_props(lintel::Node& n, const PropertyMap& pm, const StyleResolver& res) {
    for (const auto& [key, value_node] : pm)
        apply_one_prop(n, key, node_to_attrib(value_node, res));
}

// Animate all PropDecl children — called from event handlers.
static void animate_props(lintel::Node& n, const ResolvedProps& props) {
    INode* impl = n.handle<INode>();
    if (!impl) return;
    for (const ResolvedProp& p : props)
        animate_one_prop(n, impl, p.key, p.value);
}

// ─── Event wiring ─────────────────────────────────────────────────────────────
//
// Captures the OnDecl's prop list by value so the lambda is self-contained.
static void wire_event(lintel::Node& n, const OnDecl& on_decl, const StyleResolver& res) {
    auto ev = FRAMEWORK.get_event(on_decl.event);
    if (ev == Event::Null) {
        std::cerr << "unknown event '" << on_decl.event << "' — skipped\n";
        return;
    }
    ResolvedProps resolved = resolve_props(on_decl.props, res);
    n.on(ev, [resolved] (WeakNode self) {
        animate_props(self.as(), resolved);
    });
}

// ─── TreeBuilder ─────────────────────────────────────────────────────────────
//
// Recursively constructs the runtime scene graph from NodeDecl AST nodes.
//
// Priority per node (unchanged from the original load.cpp):
//   1. Inherited properties    — lowest priority, only fills gaps
//   2. Applied styles          — in declaration order; later overrides earlier
//   3. Node-local properties   — highest priority, always win
//   4. Event wiring            — OnDecl from both styles and local props

class TreeBuilder {
    const StyleResolver& res_;
    // Provides raw OnDecl access for styles so event handlers can be wired
    // from style blocks without parsing style props a second time.
    const std::unordered_map<std::string, std::vector<Node*>>& style_raw_;

    void build(lintel::Node& parent, const NodeDecl& decl, const InheritedProps& inherited) {
        auto node = FRAMEWORK.create_node(decl.tag);
        if (!node) {
            std::cerr << "unknown node type '" << decl.tag << "' — skipped\n";
            return;
        }
        lintel::Node& n = parent.push(std::move(node));

        // 1. Seed with inherited values.
        apply_inherited(n, inherited);

        // 2. Applied styles — iterate the props list to preserve declaration order.
        for (Node* child : decl.props) {
            if (!child || child->kind != NodeKind::ApplyExpr) continue;
            const std::string& style_name = child->as<ApplyExpr>().style;

            // 2a. Apply property values.
            if (const PropertyMap* pm = res_.style(style_name))
                apply_style_props(n, *pm, res_);
            else
                std::cerr << "undefined style '" << style_name << "' — skipped\n";

            // 2b. Wire OnDecl handlers embedded inside the style block.
            auto rit = style_raw_.find(style_name);
            if (rit != style_raw_.end()) {
                for (Node* sc : rit->second)
                    if (sc && sc->kind == NodeKind::OnDecl)
                        wire_event(n, sc->as<OnDecl>(), res_);
            }
        }

        // 3. Node-local properties.
        apply_props(n, decl.props, res_);

        // 4. Node-local event wiring.
        for (Node* child : decl.props)
            if (child && child->kind == NodeKind::OnDecl)
                wire_event(n, child->as<OnDecl>(), res_);

        // 5. Derive what this node exports to its children.
        const InheritedProps child_inh = derive_inherited(n, inherited);

        // 6. Recurse into nested NodeDecls, preserving document order.
        for (Node* child : decl.props)
            if (child && child->kind == NodeKind::NodeDecl)
                build(n, child->as<NodeDecl>(), child_inh);

        // 7. Register named nodes for cross-reference.
        if (!decl.name.empty())
            CORE.register_named(decl.name, WeakNode(n.handle()));
    }

public:
    TreeBuilder(const StyleResolver& res,
                const std::unordered_map<std::string, std::vector<Node*>>& style_raw)
        : res_(res), style_raw_(style_raw) {}

    void run(lintel::Node& root, const AST& ast) {
        const InheritedProps blank{};
        for (Node* top : ast.nodes) {
            if (!top || top->kind != NodeKind::NodeDecl) continue;
            const NodeDecl& decl = top->as<NodeDecl>();

            if (decl.tag == "root") {
                // Root props go directly onto the pre-existing root Node.
                apply_props(root, decl.props, res_);
                for (Node* child : decl.props)
                    if (child && child->kind == NodeKind::NodeDecl)
                        build(root, child->as<NodeDecl>(), blank);
            }
            else {
                build(root, decl, blank);
            }
        }
    }
};

} // namespace lintel::parser

// ─── Entry point ─────────────────────────────────────────────────────────────

namespace lintel {

Node load(const char* path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error(std::string("cannot open file: ") + path);

    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Pass 1 — parse.
    parser::AST ast;
    parser::parse(source, ast);

    // Pass 2 — collect variables and style definitions.
    parser::StyleResolver resolver(ast);
    traverse(ast, resolver);

    // Build a raw-prop index for StyleDecls so TreeBuilder can wire OnDecls
    // from inside style blocks without re-traversing the entire AST.
    std::unordered_map<std::string, std::vector<parser::Node*>> style_raw;
    for (parser::Node* top : ast.nodes) {
        if (top && top->kind == parser::NodeKind::StyleDecl) {
            const parser::StyleDecl& sd = top->as<parser::StyleDecl>();
            style_raw[sd.name] = sd.props;
        }
    }

    // Pass 3 — build the runtime scene graph.
    Node root;
    parser::TreeBuilder(resolver, style_raw).run(root, ast);
    return root;
}

}
