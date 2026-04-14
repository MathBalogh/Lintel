#pragma once
#include "ast.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <iostream>

namespace lintel::parser {

// ─── NodeVisitor - base interface ─────────────────────────────────────────────
//
// Provides virtual hooks for every node kind that carries semantic content.
// All hooks default to no-ops so concrete visitors only override what they need.
//
// Usage:
//   MyVisitor v;
//   traverse(ast, v);

struct NodeVisitor {
    virtual ~NodeVisitor() = default;

    // Expressions
    virtual void visit(IdentExpr&) {}
    virtual void visit(NumExpr&) {}
    virtual void visit(HexExpr&) {}
    virtual void visit(BoolExpr&) {}
    virtual void visit(ListExpr&) {}

    // Statements
    virtual void visit(ApplyExpr&) {}
    virtual void visit(VarDecl&) {}
    virtual void visit(PropDecl&) {}
    virtual void visit(OnDecl&) {}

    // Containers - default implementations recurse into children.
    virtual void visit(StyleDecl& s);
    virtual void visit(NodeDecl& n);
};

// Depth-first traversal of every top-level node in the AST.
void traverse(AST& ast, NodeVisitor& v);

// Dispatch a single node to the correct virtual override.
void dispatch(Node& node, NodeVisitor& v);

// ─── PropertyMap - a resolved key→value mapping ───────────────────────────────

using PropertyMap = std::unordered_map<std::string, Node*>;

// ─── StyleResolver - pass 1 ───────────────────────────────────────────────────
//
// Walk the AST once to collect:
//   • VarDecl bindings  (global variables)
//   • StyleDecl entries (named style libraries)
//
// After traversal:
//   resolver.resolve_str(node)   → string representation, following var refs
//   resolver.style("button")     → PropertyMap for that style (or nullptr)
//   resolver.var("text-muted")   → raw Node* for that variable (or nullptr)
//
// An optional on_apply callback is fired whenever an ApplyExpr is encountered
// during traversal - useful for immediate style application in a single pass.

struct StyleResolver : NodeVisitor {
    using ApplyCallback = std::function<void(const std::string& style, const PropertyMap&)>;

    explicit StyleResolver(AST& ast, ApplyCallback cb = {})
        : ast_(ast), on_apply_(std::move(cb)) {}

    void visit(VarDecl&)   override;
    void visit(StyleDecl&) override;
    void visit(NodeDecl&)  override;
    void visit(ApplyExpr&) override;

    // Look up a variable by name; returns nullptr if not found.
    Node* var(const std::string& name) const {
        auto it = vars_.find(name);
        return it != vars_.end() ? it->second : nullptr;
    }

    // Look up a named style; returns nullptr if not found.
    const PropertyMap* style(const std::string& name) const {
        auto it = styles_.find(name);
        return it != styles_.end() ? &it->second : nullptr;
    }

    // Resolve a value node to its string representation.
    // If the node is an IdentExpr that matches a VarDecl, the variable is
    // followed one level.  HexExpr values are returned as their text (e.g.
    // "#FFFFFA").  ListExpr elements are space-joined.
    std::string resolve_str(const Node* node) const;

private:
    AST& ast_;
    ApplyCallback                                  on_apply_;
    std::unordered_map<std::string, Node*>         vars_;
    std::unordered_map<std::string, PropertyMap>   styles_;
};

// ─── EventBinder - pass 2 (or combined with TreeBuilder) ─────────────────────
//
// Traverses NodeDecl trees and records each OnDecl together with its parent
// NodeDecl so the caller can wire event handlers after the scene is built.

struct EventBinder : NodeVisitor {
    struct Binding {
        NodeDecl* parent = nullptr;
        OnDecl* on = nullptr;
    };

    void visit(NodeDecl&) override;

    const std::vector<Binding>& bindings() const { return bindings_; }

private:
    NodeDecl* current_ = nullptr;
    std::vector<Binding> bindings_;
};

using TemplateArgs = std::unordered_map<std::string, const Node*>;
std::string resolve_style_name(const Node* node,
                               const StyleResolver& res,
                               const TemplateArgs& targs = {});

} // namespace lintel::parser
