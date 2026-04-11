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

// ─── Helpers for new Property / UIValue / Enum system ────────────────────────
static bool is_ui_value_key(std::string_view prop) {
    lintel::Key k = lintel::get_key(std::string(prop));
    if (k.index == lintel::Key::Null || !k.affects_layout()) return false;

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
        if (value_name == "row")    return lintel::Property(DirectionRow);
        if (value_name == "column") return lintel::Property(DirectionCol);
    }
    else if (k == lintel::Key::AlignItems) {
        if (value_name == "start")   return lintel::Property(AlignStart);
        if (value_name == "center")  return lintel::Property(AlignCenter);
        if (value_name == "end")     return lintel::Property(AlignEnd);
        if (value_name == "stretch") return lintel::Property(AlignStretch);
    }
    else if (k == lintel::Key::JustifyItems) {
        if (value_name == "start")          return lintel::Property(JustifyStart);
        if (value_name == "center")         return lintel::Property(JustifyCenter);
        if (value_name == "end")            return lintel::Property(JustifyEnd);
        if (value_name == "space-between")  return lintel::Property(JustifySpaceBetween);
        if (value_name == "space-around")   return lintel::Property(JustifySpaceAround);
    }
    else if (k == lintel::Key::TextAlign) {
        if (value_name == "left")     return lintel::Property(TextAlignLeft);
        if (value_name == "center")   return lintel::Property(TextAlignCenter);
        if (value_name == "right")    return lintel::Property(TextAlignRight);
        if (value_name == "justify")  return lintel::Property(TextAlignJustify);
    }

    return {};
}

// ─── Template argument bindings ──────────────────────────────────────────────
using TemplateArgs = std::unordered_map<std::string, const Node*>;

// ─── AST value → PropValue (key-aware) ───────────────────────────────────────
static lintel::Property node_to_prop(std::string_view key, const Node* node,
                                     const StyleResolver& res,
                                     const TemplateArgs& targs = {}) {
    if (!node) return {};

    if (node->kind == NodeKind::IdentExpr) {
        const std::string& name = node->as<IdentExpr>().name;
        auto it = targs.find(name);
        if (it != targs.end())
            return node_to_prop(key, it->second, res, targs);
    }

    if (node->kind == NodeKind::IdentExpr) {
        const std::string& name = node->as<IdentExpr>().name;
        if (const Node* target = res.var(name))
            return node_to_prop(key, target, res, targs);
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

            if (name == "auto" && is_ui_value_key(key))
                return lintel::Property(lintel::UIValue::make_auto());

            lintel::Property enum_val = try_parse_enum(key, name);
            if (!enum_val.is_null())
                return enum_val;

            return lintel::Property(to_wstring(name));
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
            return lintel::Property(oss.str());
        }

        case NodeKind::CallExpr:
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
    if (auto* v = a.get_wstring(Key::FontFamily)) out.font_family = *v;
    if (auto v = a.get_color(Key::TextColor)) out.text_color = *v;
    if (auto v = a.get(Key::FontSize))  out.font_size = v;
    if (auto v = a.get(Key::Bold))      out.bold = v;
    if (auto v = a.get(Key::Italic))    out.italic = v;
    if (auto v = a.get(Key::Wrap))      out.wrap = v;
    if (auto v = a.get(Key::Opacity))   out.opacity = v;
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

// ─── AST → StyleSheet (pass 2) ─────────────────────────────────────────────
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
    const std::unordered_map<std::string, const Node*>& templates_;

    void build(lintel::Node& parent, const NodeDecl& decl,
               const InheritedProps& inherited, TemplateArgs targs = {}) {

        const TemplateDecl* tmpl = nullptr;
        {
            auto it = templates_.find(decl.tag);
            if (it != templates_.end()) {
                tmpl = &it->second->as<TemplateDecl>();
            }
        }

        if (tmpl && !tmpl->params.empty()) {
            for (size_t i = 0; i < tmpl->params.size() && i < decl.args.size(); ++i)
                targs[tmpl->params[i]] = decl.args[i];
        }

        lintel::Node node;
        if (tmpl) {
            node = create_node(tmpl->base);
            if (!node) return;
        }
        else {
            node = create_node(decl.tag);
            if (!node) return;
        }
        lintel::Node& n = parent.push(node);

        apply_inherited(n, inherited);

        std::vector<Node*> effective_props;
        if (tmpl) effective_props = tmpl->props;
        effective_props.insert(effective_props.end(), decl.props.begin(), decl.props.end());

        for (Node* child : effective_props) {
            if (!child || child->kind != NodeKind::ApplyExpr) continue;
            sheet_.apply(n, child->as<ApplyExpr>().style);
        }

        for (Node* child : effective_props) {
            if (!child || child->kind != NodeKind::PropDecl) continue;
            const PropDecl& pd = child->as<PropDecl>();
            sheet_.dispatch_prop(n, pd.property, node_to_prop(pd.property, pd.value, res_, targs));
        }

        for (Node* child : effective_props) {
            if (!child || child->kind != NodeKind::OnDecl) continue;
            const OnDecl& od = child->as<OnDecl>();
            Event ev = get_event(od.event);
            if (ev == Event::Null) continue;

            auto props = std::make_shared<std::vector<StyleSheet::Prop>>();
            for (Node* dp : od.props) {
                if (!dp || dp->kind != NodeKind::PropDecl) continue;
                const PropDecl& pd = dp->as<PropDecl>();
                props->push_back({ pd.property,
                    node_to_prop(pd.property, pd.value, res_, targs) });
            }

            StyleSheet& ref = sheet_;
            n.on(ev, [&ref, props] (NodeView self) {
                ref.apply_props(self.as(), *props);
            });
        }

        const InheritedProps child_inh = derive_inherited(n, inherited);

        for (Node* child : effective_props) {
            if (!child || child->kind != NodeKind::NodeDecl) continue;
            build(n, child->as<NodeDecl>(), child_inh, targs);
        }

        if (!decl.id.empty()) {
            std::string node_id = decl.id;
            auto it = targs.find(decl.id);
            if (it != targs.end())
                node_id = res_.resolve_str(it->second);
            sheet_.register_node(node_id, NodeView(n.handle()));
        }
    }

public:
    TreeBuilder(StyleSheet& sheet, const StyleResolver& res, const std::unordered_map<std::string, const Node*>& templates)
        : sheet_(sheet), res_(res), templates_(templates) {}

    void run(lintel::Node& root, const AST& ast) {
        const InheritedProps blank{};

        for (Node* top : ast.nodes) {
            if (!top || top->kind != NodeKind::NodeDecl) continue;
            const NodeDecl& decl = top->as<NodeDecl>();

            if (decl.tag == "root") {
                for (Node* child : decl.props) {
                    if (!child || child->kind != NodeKind::PropDecl) continue;
                    const PropDecl& pd = child->as<PropDecl>();
                    sheet_.dispatch_prop(root, pd.property, node_to_prop(pd.property, pd.value, res_));
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

Node load(const char* path, StyleSheet& ss) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error(std::string("cannot open file: ") + path);

    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());

    parser::AST ast;
    parser::parse(source, ast);

    parser::StyleResolver resolver(ast);
    traverse(ast, resolver);

    ss = parser::build_stylesheet(ast, resolver);

    std::unordered_map<std::string, const parser::Node*> template_map;
    for (const parser::Node* top : ast.nodes) {
        if (top && top->kind == parser::NodeKind::TemplateDecl) {
            const parser::TemplateDecl& td = top->as<parser::TemplateDecl>();
            template_map[td.name] = top;
        }
    }

    Node root;
    parser::TreeBuilder(ss, resolver, template_map).run(root, ast);
    return root;
}

} // namespace lintel

