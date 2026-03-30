// load.cpp - parse → resolve → build StyleSheet → build scene graph.

#include "visitor.h"
#include "parser.h"
#include "inode.h"
#include "framework.h"

#include <charconv>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <optional>

namespace lintel::parser {

// ─── Node registrations ───────────────────────────────────────────────────────

static bool s_registered = ([] {
    FRAMEWORK.register_node<lintel::Node>("node");
    FRAMEWORK.register_node<lintel::TextNode>("text");
    FRAMEWORK.register_node<lintel::GraphNode>("graph");
    FRAMEWORK.register_node<lintel::ImageNode>("image");
    return true;
})();

// ─── AST value → PropValue ───────────────────────────────────────────────────
//
// Converts a parser AST expression node into a concrete PropValue.
// IdentExpr nodes that match a VarDecl in the resolver are followed one level.

static PropValue node_to_prop(const Node* node, const StyleResolver& res) {
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
                return node_to_prop(target, res);
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
//
// A small set of visual properties that flow from parent to child when the
// child does not set them explicitly (font-family, font-size, text-color, …).

struct InheritedProps {
    std::optional<std::wstring> font_family;
    std::optional<float>        font_size;
    std::optional<Color>        text_color;
    std::optional<bool>         bold;
    std::optional<bool>         italic;
    std::optional<bool>         wrap;
    std::optional<float>        opacity;
};

static InheritedProps derive_inherited(const lintel::Node& n,
                                       const InheritedProps& parent) {
    InheritedProps out = parent;
    const Attributes& a = const_cast<lintel::Node&>(n).attr();
    if (const auto* v = a.get<std::wstring>(property::FontFamily)) out.font_family = *v;
    if (const auto* v = a.get<float>(property::FontSize))          out.font_size = *v;
    if (const auto* v = a.get<Color>(property::TextColor))         out.text_color = *v;
    if (const auto* v = a.get<bool>(property::Bold))               out.bold = *v;
    if (const auto* v = a.get<bool>(property::Italic))             out.italic = *v;
    if (const auto* v = a.get<bool>(property::Wrap))               out.wrap = *v;
    if (const auto* v = a.get<float>(property::Opacity))           out.opacity = *v;
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

// ─── AST → StyleSheet (pass 2) ────────────────────────────────────────────────
//
// Walks all StyleDecl and VarDecl nodes in the AST and populates a StyleSheet.
// Variable resolution uses the StyleResolver built in pass 1.

static StyleSheet build_stylesheet(const AST& ast, const StyleResolver& res) {
    StyleSheet sheet;

    for (Node* top : ast.nodes) {
        if (!top || top->kind != NodeKind::StyleDecl) continue;
        const StyleDecl& sd = top->as<StyleDecl>();

        std::vector<StyleSheet::Prop>    props;
        std::vector<StyleSheet::Handler> handlers;

        for (Node* child : sd.props) {
            if (!child) continue;

            if (child->kind == NodeKind::PropDecl) {
                const PropDecl& pd = child->as<PropDecl>();
                props.push_back({ pd.property, node_to_prop(pd.value, res) });
            }
            else if (child->kind == NodeKind::ApplyExpr) {
                // Style inheritance: fold the base style's props into this one.
                // Later declarations in sd.props win, so we prepend base props.
                const std::string& base = child->as<ApplyExpr>().style;
                if (const StyleSheet::Style* s = sheet.find_style(base)) {
                    // Insert at front so local props (processed after) override.
                    props.insert(props.begin(), s->props.begin(), s->props.end());
                }
                else {
                    std::cerr << "load: style '" << sd.name
                        << "' applies undefined base '" << base << "'\n";
                }
            }
            else if (child->kind == NodeKind::OnDecl) {
                const OnDecl& od = child->as<OnDecl>();
                Event ev = FRAMEWORK.get_event(od.event);
                if (ev == Event::Null) {
                    std::cerr << "load: unknown event '" << od.event
                        << "' in style '" << sd.name << "' - skipped\n";
                    continue;
                }
                std::vector<StyleSheet::Prop> deltas;
                for (Node* dp : od.props) {
                    if (!dp || dp->kind != NodeKind::PropDecl) continue;
                    const PropDecl& pd = dp->as<PropDecl>();
                    deltas.push_back({ pd.property, node_to_prop(pd.value, res) });
                }
                handlers.push_back({ ev, std::move(deltas) });
            }
        }

        sheet.define(sd.name, std::move(props), std::move(handlers));
    }

    return sheet;
}

// ─── TreeBuilder (pass 3) ─────────────────────────────────────────────────────
//
// Walks NodeDecl nodes in the AST and constructs the runtime scene graph,
// delegating all property application and event wiring to the StyleSheet.
//
// Priority per node (CSS-like, weakest to strongest):
//   1. Inherited props   (gap-fill from parent)
//   2. Applied styles    (in declaration order, later overrides earlier)
//   3. Node-local props  (always win)
//   4. Event handlers    (from both styles and local OnDecls)

class TreeBuilder {
    StyleSheet& sheet_;
    const StyleResolver& res_;

    void build(lintel::Node& parent, const NodeDecl& decl,
               const InheritedProps& inherited) {

        lintel::Node node = FRAMEWORK.create_node(decl.tag);
        if (!node) {
            std::cerr << "load: unknown node type '" << decl.tag << "' - skipped\n";
            return;
        }
        lintel::Node& n = parent.push(std::move(node));

        // 1. Seed with inherited values.
        apply_inherited(n, inherited);

        // 2. Applied styles - iterate props in declaration order.
        for (Node* child : decl.props) {
            if (!child || child->kind != NodeKind::ApplyExpr) continue;
            sheet_.apply(n, child->as<ApplyExpr>().style);
        }

        // 3. Node-local properties (props only, not OnDecls or NodeDecls).
        for (Node* child : decl.props) {
            if (!child || child->kind != NodeKind::PropDecl) continue;
            const PropDecl& pd = child->as<PropDecl>();
            StyleSheet::dispatch_prop(n, pd.property,
                                      node_to_prop(pd.value, res_),
                                      StyleSheet::Mode::Snap);
        }

        // 4. Node-local event handlers.
        for (Node* child : decl.props) {
            if (!child || child->kind != NodeKind::OnDecl) continue;
            const OnDecl& od = child->as<OnDecl>();

            Event ev = FRAMEWORK.get_event(od.event);
            if (ev == Event::Null) {
                std::cerr << "load: unknown event '" << od.event << "' - skipped\n";
                continue;
            }

            // Resolve delta props eagerly so the lambda is AST-independent.
            auto deltas = std::make_shared<std::vector<StyleSheet::Prop>>();
            for (Node* dp : od.props) {
                if (!dp || dp->kind != NodeKind::PropDecl) continue;
                const PropDecl& pd = dp->as<PropDecl>();
                deltas->push_back({ pd.property, node_to_prop(pd.value, res_) });
            }

            n.on(ev, [deltas] (WeakNode self) {
                StyleSheet::animate(self.as(), *deltas);
            });
        }

        // 5. Derive inherited props to pass down.
        const InheritedProps child_inh = derive_inherited(n, inherited);

        // 6. Recurse into nested NodeDecls.
        for (Node* child : decl.props) {
            if (!child || child->kind != NodeKind::NodeDecl) continue;
            build(n, child->as<NodeDecl>(), child_inh);
        }

        // 7. Register named nodes for cross-reference via find().
        if (!decl.name.empty())
            sheet_.register_node(decl.name, WeakNode(n.handle()));
    }

public:
    TreeBuilder(StyleSheet& sheet, const StyleResolver& res)
        : sheet_(sheet), res_(res) {}

    void run(lintel::Node& root, const AST& ast) {
        const InheritedProps blank{};

        for (Node* top : ast.nodes) {
            if (!top || top->kind != NodeKind::NodeDecl) continue;
            const NodeDecl& decl = top->as<NodeDecl>();

            if (decl.tag == "root") {
                // Root-level props go directly onto the pre-existing CORE.root.
                for (Node* child : decl.props) {
                    if (!child || child->kind != NodeKind::PropDecl) continue;
                    const PropDecl& pd = child->as<PropDecl>();
                    StyleSheet::dispatch_prop(root, pd.property,
                                              node_to_prop(pd.value, res_),
                                              StyleSheet::Mode::Snap);
                }
                for (Node* child : decl.props) {
                    if (!child || child->kind != NodeKind::NodeDecl) continue;
                    build(root, child->as<NodeDecl>(), blank);
                }
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

LoadResult load(const char* path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error(std::string("cannot open file: ") + path);

    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());

    // Pass 1 - lex + parse → AST.
    parser::AST ast;
    parser::parse(source, ast);

    // Pass 2 - collect variables and style declarations.
    parser::StyleResolver resolver(ast);
    traverse(ast, resolver);

    // Pass 2b - resolve AST style nodes into a StyleSheet.
    StyleSheet sheet = parser::build_stylesheet(ast, resolver);

    // Pass 3 - build the runtime scene graph.
    LoadResult result;
    parser::TreeBuilder(sheet, resolver).run(result.root, ast);
    result.sheet = std::move(sheet);

    return result;
}

} // namespace lintel
