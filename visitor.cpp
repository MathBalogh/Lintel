#include "visitor.h"
#include <sstream>

namespace lintel::parser {

// ─── dispatch ─────────────────────────────────────────────────────────────────

void dispatch(Node& node, NodeVisitor& v) {
    switch (node.kind) {
        case NodeKind::IdentExpr: v.visit(node.as<IdentExpr>()); break;
        case NodeKind::NumExpr:   v.visit(node.as<NumExpr>());   break;
        case NodeKind::HexExpr:   v.visit(node.as<HexExpr>());   break;
        case NodeKind::BoolExpr:  v.visit(node.as<BoolExpr>());  break;
        case NodeKind::ListExpr:  v.visit(node.as<ListExpr>());  break;
        case NodeKind::ApplyExpr: v.visit(node.as<ApplyExpr>()); break;
        case NodeKind::VarDecl:   v.visit(node.as<VarDecl>());   break;
        case NodeKind::PropDecl:  v.visit(node.as<PropDecl>());  break;
        case NodeKind::StyleDecl: v.visit(node.as<StyleDecl>()); break;
        case NodeKind::NodeDecl:  v.visit(node.as<NodeDecl>());  break;
        case NodeKind::OnDecl:    v.visit(node.as<OnDecl>());    break;
        default: break;
    }
}

// ─── traverse ─────────────────────────────────────────────────────────────────

void traverse(AST& ast, NodeVisitor& v) {
    for (Node* n : ast.nodes)
        if (n) dispatch(*n, v);
}

// ─── NodeVisitor default container implementations ────────────────────────────

void NodeVisitor::visit(StyleDecl& s) {
    for (Node* p : s.props) if (p) dispatch(*p, *this);
}

void NodeVisitor::visit(NodeDecl& n) {
    for (Node* p : n.props) if (p) dispatch(*p, *this);
}

// ─── StyleResolver ────────────────────────────────────────────────────────────

void StyleResolver::visit(VarDecl& d) {
    vars_[d.name] = d.value;
}

void StyleResolver::visit(StyleDecl& d) {
    PropertyMap& pm = styles_[d.name];
    // Collect explicit props, then let "apply" inside a style merge another.
    for (Node* child : d.props) {
        if (!child) continue;
        if (child->kind == NodeKind::PropDecl) {
            auto& pd = child->as<PropDecl>();
            pm[pd.property] = pd.value;
        }
        else if (child->kind == NodeKind::ApplyExpr) {
            // Style inheritance: merge the referenced style's properties.
            const auto& ref = child->as<ApplyExpr>().style;
            if (const PropertyMap* base = style(ref))
                for (const auto& [k, v] : *base)
                    pm.emplace(k, v); // don't overwrite — child wins
        }
        // OnDecl inside a style is left for EventBinder / TreeBuilder.
    }
}

void StyleResolver::visit(NodeDecl& d) {
    // Recurse so nested NodeDecls and their VarDecls are also visited.
    for (Node* child : d.props)
        if (child) dispatch(*child, *this);
}

void StyleResolver::visit(ApplyExpr& a) {
    if (!on_apply_) return;
    if (const PropertyMap* pm = style(a.style))
        on_apply_(a.style, *pm);
}

std::string StyleResolver::resolve_str(const Node* node) const {
    if (!node) return {};

    switch (node->kind) {
        case NodeKind::IdentExpr:
        {
            const std::string& name = node->as<IdentExpr>().name;
            // Follow one level of variable indirection.
            if (Node* target = var(name))
                return resolve_str(target);
            return name;
        }
        case NodeKind::NumExpr:
            return node->as<NumExpr>().text;
        case NodeKind::HexExpr:
            return node->as<HexExpr>().value;
        case NodeKind::BoolExpr:
            return node->as<BoolExpr>().value ? "true" : "false";
        case NodeKind::ListExpr:
        {
            std::ostringstream oss;
            bool first = true;
            for (const Node* item : node->as<ListExpr>().list) {
                if (!first) oss << ' ';
                oss << resolve_str(item);
                first = false;
            }
            return oss.str();
        }
        default:
            return {};
    }
}

// ─── EventBinder ──────────────────────────────────────────────────────────────

void EventBinder::visit(NodeDecl& d) {
    NodeDecl* prev = current_;
    current_ = &d;

    for (Node* child : d.props) {
        if (!child) continue;
        if (child->kind == NodeKind::OnDecl)
            bindings_.push_back({ current_, &child->as<OnDecl>() });
        else if (child->kind == NodeKind::NodeDecl)
            visit(child->as<NodeDecl>());  // recurse into nested nodes
    }

    current_ = prev;
}

} // namespace lintel::parser
