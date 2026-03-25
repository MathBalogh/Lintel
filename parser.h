#pragma once
#include "lexer.h"

namespace lintel::parser {

class Parser {
    Lexer lexer_;
    AST& ast_;

    // ── Parse helpers ─────────────────────────────────────────────────────────
    void  error(const Token& at, std::string msg) { lexer_.error(at, std::move(msg)); }
    void  unexpected(const Token& t, std::string_view ctx) { lexer_.unexpected_token_error(t, ctx); }

    // Skip any pending Newline tokens (used between statements).
    void skip_newlines() { while (lexer_.peek().kind == TokenKind::Newline) lexer_.pop(); }

    // ── Top-level declarations ────────────────────────────────────────────────
    Node* parse_var_decl();
    Node* parse_style_decl();
    Node* parse_root_decl();

    // ── Block contents ────────────────────────────────────────────────────────
    // Consumes the Newline+Indent that opens a block and the closing Dedent.
    std::vector<Node*> parse_block();

    // Dispatch a single statement inside a block.
    Node* parse_inner_stmt();

    Node* parse_node_decl();
    Node* parse_on_decl();
    Node* parse_apply();
    Node* parse_prop_decl();

    // ── Value parsing ─────────────────────────────────────────────────────────
    Node* parse_expr();

    // Returns a single Expr, or a ListExpr for space-separated values.
    // Stops at Newline, Indent, Dedent, or EOF.
    Node* parse_value();

public:
    explicit Parser(std::string_view src, AST& root)
        : lexer_(src, root), ast_(root) {}

    void parse();
};

} // namespace lintel::parser
