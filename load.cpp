// load.cpp - parse → resolve → build StyleSheet → build scene graph.

#include "visitor.h"
#include "parser.h"
#include "inode.h"

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
    register_node<lintel::Node>("node");
    register_node<lintel::TextNode>("text");
    register_node<lintel::GraphNode>("graph");
    register_node<lintel::ImageNode>("image");
    return true;
})();

// ─── Helpers for new Property / UIValue / Enum system ────────────────────────
//
// After the property refactor, values must be turned into the exact
// Property variant the target key expects (UIValue for dimensions/gap,
// unsigned-int enum for Direction/AlignItems/etc.).  The DSL still uses
// the old value syntax (NumExpr, IdentExpr, …) so we dispatch on the
// property name string.

static bool is_ui_value_key(std::string_view prop) {
    lintel::Key k = lintel::get_key(std::string(prop));
    if (k.index == lintel::Key::Null || !k.affects_layout()) return false;

    // All layout keys except the three enums are UIValue (px by default,
    // "auto" special-cased in Ident handling). Share stays float.
    switch (k.index) {
        case lintel::Key::Width:
        case lintel::Key::Height:
        case lintel::Key::PaddingTop:
        case lintel::Key::PaddingRight:
        case lintel::Key::PaddingBottom:
        case lintel::Key::PaddingLeft:
        case lintel::Key::MarginTop:
        case lintel::Key::MarginRight:
        case lintel::Key::MarginBottom:
        case lintel::Key::MarginLeft:
        case lintel::Key::Gap:
            return true;
        default:
            return false;
    }
}

static lintel::Property try_parse_enum(std::string_view prop_key, const std::string& value_name) {
    lintel::Key k = lintel::get_key(std::string(prop_key));
    if (k.index == lintel::Key::Null) return {};

    if (k == lintel::Key::Direction) {
        if (value_name == "row")    return lintel::Property(static_cast<unsigned int>(lintel::Direction::Row));
        if (value_name == "column") return lintel::Property(static_cast<unsigned int>(lintel::Direction::Column));
    }
    else if (k == lintel::Key::AlignItems) {
        if (value_name == "start")   return lintel::Property(static_cast<unsigned int>(lintel::Align::Start));
        if (value_name == "center")  return lintel::Property(static_cast<unsigned int>(lintel::Align::Center));
        if (value_name == "end")     return lintel::Property(static_cast<unsigned int>(lintel::Align::End));
        if (value_name == "stretch") return lintel::Property(static_cast<unsigned int>(lintel::Align::Stretch));
    }
    else if (k == lintel::Key::JustifyItems) {
        if (value_name == "start")          return lintel::Property(static_cast<unsigned int>(lintel::Justify::Start));
        if (value_name == "center")         return lintel::Property(static_cast<unsigned int>(lintel::Justify::Center));
        if (value_name == "end")            return lintel::Property(static_cast<unsigned int>(lintel::Justify::End));
        if (value_name == "space-between")  return lintel::Property(static_cast<unsigned int>(lintel::Justify::SpaceBetween));
        if (value_name == "space-around")   return lintel::Property(static_cast<unsigned int>(lintel::Justify::SpaceAround));
    }
    else if (k == lintel::Key::TextAlign) {
        if (value_name == "left")     return lintel::Property(static_cast<unsigned int>(lintel::TextAlign::Left));
        if (value_name == "center")   return lintel::Property(static_cast<unsigned int>(lintel::TextAlign::Center));
        if (value_name == "right")    return lintel::Property(static_cast<unsigned int>(lintel::TextAlign::Right));
        if (value_name == "justify")  return lintel::Property(static_cast<unsigned int>(lintel::TextAlign::Justify));
    }

    return {};
}

// ─── AST value → PropValue (key-aware) ───────────────────────────────────────
//
// Now takes the property name so we can emit UIValue / enum Property variants.
// Variable resolution still follows one level exactly as before.

static lintel::Property node_to_prop(std::string_view key, const Node* node, const StyleResolver& res) {
    if (!node) return {};

    // Ident that names a VarDecl → follow exactly one level (old behaviour).
    if (node->kind == NodeKind::IdentExpr) {
        const std::string& name = node->as<IdentExpr>().name;
        if (const Node* target = res.var(name))
            return node_to_prop(key, target, res);
    }

    switch (node->kind) {
        case NodeKind::NumExpr:
        {
            float num = node->as<NumExpr>().to_number();
            if (is_ui_value_key(key))
                return lintel::Property(lintel::UIValue::px(num));
            return lintel::Property(num);
        }

        case NodeKind::BoolExpr:
            return lintel::Property(node->as<BoolExpr>().value);

        case NodeKind::HexExpr:
            return lintel::Property(node->as<HexExpr>().to_color());

        case NodeKind::IdentExpr:
        {
            const std::string& name = node->as<IdentExpr>().name;

            // "auto" for any UIValue key
            if (name == "auto" && is_ui_value_key(key))
                return lintel::Property(lintel::UIValue::make_auto());

            // Enum literals (Direction, AlignItems, JustifyItems, TextAlign)
            lintel::Property enum_val = try_parse_enum(key, name);
            if (!enum_val.is_null())
                return enum_val;

            // ordinary identifier → wstring (font-family, style names, …)
            return lintel::Property(to_wstring(name));
        }

        case NodeKind::ListExpr:
        {
            // space-joined string (unchanged from original)
            std::wostringstream oss;
            bool first = true;
            for (const Node* item : node->as<ListExpr>().list) {
                if (!first) oss << L' ';
                std::string s = res.resolve_str(item);
                oss << std::wstring(s.begin(), s.end());
                first = false;
            }
            return lintel::Property(oss.str());
        }

        case NodeKind::CallExpr:
            // reserved for future function values (px(100), …); ignored for now
            return {};

        default:
            return {};
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
    Properties& a = const_cast<lintel::Node&>(n)->props;
    if (Property* v = a.find(Key::FontFamily, Property::Type::WString)) out.font_family = v->get_wstring();
    if (Property* v = a.find(Key::FontSize)) out.font_size = *v;
    if (Property* v = a.find(Key::TextColor)) out.text_color = v->get_color();
    if (Property* v = a.find(Key::Bold)) out.bold = *v;
    if (Property* v = a.find(Key::Italic)) out.italic = *v;
    if (Property* v = a.find(Key::Wrap)) out.wrap = *v;
    if (Property* v = a.find(Key::Opacity)) out.opacity = *v;
    return out;
}

static void apply_inherited(lintel::Node& n, const InheritedProps& inh) {
    Properties& a = n->props;
    if (inh.font_family && !a.has(Key::FontFamily)) a.set(Key::FontFamily, *inh.font_family);
    if (inh.font_size && !a.has(Key::FontSize)) a.set(Key::FontSize, *inh.font_size);
    if (inh.text_color && !a.has(Key::TextColor)) a.set(Key::TextColor, *inh.text_color);
    if (inh.bold && !a.has(Key::Bold)) a.set(Key::Bold, *inh.bold);
    if (inh.italic && !a.has(Key::Italic)) a.set(Key::Italic, *inh.italic);
    if (inh.wrap && !a.has(Key::Wrap)) a.set(Key::Wrap, *inh.wrap);
    if (inh.opacity && !a.has(Key::Opacity)) a.set(Key::Opacity, *inh.opacity);
}

// ─── AST → StyleSheet (pass 2) ────────────────────────────────────────────────

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
                props.push_back({ pd.property, node_to_prop(pd.property, pd.value, res) });
            }
            else if (child->kind == NodeKind::ApplyExpr) {
                const std::string& base = child->as<ApplyExpr>().style;
                if (const StyleSheet::Style* s = sheet.find_style(base)) {
                    props.insert(props.begin(), s->props.begin(), s->props.end());
                }
                else {
                    std::cerr << "load: style '" << sd.name
                        << "' applies undefined base '" << base << "'\n";
                }
            }
            else if (child->kind == NodeKind::OnDecl) {
                const OnDecl& od = child->as<OnDecl>();
                Event ev = get_event(od.event);
                if (ev == Event::Null) {
                    std::cerr << "load: unknown event '" << od.event
                        << "' in style '" << sd.name << "' - skipped\n";
                    continue;
                }
                std::vector<StyleSheet::Prop> deltas;
                for (Node* dp : od.props) {
                    if (!dp || dp->kind != NodeKind::PropDecl) continue;
                    const PropDecl& pd = dp->as<PropDecl>();
                    deltas.push_back({ pd.property, node_to_prop(pd.property, pd.value, res) });
                }
                handlers.push_back({ ev, std::move(deltas) });
            }
        }

        sheet.define(sd.name, std::move(props), std::move(handlers));
    }

    return sheet;
}

// ─── TreeBuilder (pass 3) ─────────────────────────────────────────────────────

class TreeBuilder {
    StyleSheet& sheet_;
    const StyleResolver& res_;

    void build(lintel::Node& parent, const NodeDecl& decl,
               const InheritedProps& inherited) {

        lintel::Node node = create_node(decl.tag);
        if (!node) {
            std::cerr << "load: unknown node type '" << decl.tag << "' - skipped\n";
            return;
        }
        lintel::Node& n = parent.push(node);

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
            StyleSheet::dispatch_prop(n, pd.property, node_to_prop(pd.property, pd.value, res_));
        }

        // 4. Node-local event handlers.
        for (Node* child : decl.props) {
            if (!child || child->kind != NodeKind::OnDecl) continue;
            const OnDecl& od = child->as<OnDecl>();

            Event ev = get_event(od.event);
            if (ev == Event::Null) {
                std::cerr << "load: unknown event '" << od.event << "' - skipped\n";
                continue;
            }

            auto props = std::make_shared<std::vector<StyleSheet::Prop>>();
            for (Node* dp : od.props) {
                if (!dp || dp->kind != NodeKind::PropDecl) continue;
                const PropDecl& pd = dp->as<PropDecl>();
                props->push_back({ pd.property, node_to_prop(pd.property, pd.value, res_) });
            }

            n.on(ev, [props] (WeakNode self) {
                StyleSheet::apply_props(self.as(), *props);
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
                    StyleSheet::dispatch_prop(root, pd.property, node_to_prop(pd.property, pd.value, res_));
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
