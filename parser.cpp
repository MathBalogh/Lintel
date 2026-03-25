#include "parser.h"

namespace lintel::parser {

// ─── Block ────────────────────────────────────────────────────────────────────
//
// A block is introduced by a ':' followed by a Newline+Indent sequence.
// It is terminated by a matching Dedent.
//
//   node:           ← caller already consumed 'node' and ':'
//     direction = row
//     text:
//       content = "hello"
//

std::vector<Node*> Parser::parse_block() {
    // Consume the Newline that follows ':'.
    if (!lexer_.match(TokenKind::Newline)) {
        unexpected(lexer_.peek(), "expected newline before block");
    }

    // If there is no Indent, the block is empty (or malformed).
    if (!lexer_.match(TokenKind::Indent)) {
        unexpected(lexer_.peek(), "expected indented block");
        return {};
    }

    std::vector<Node*> stmts;
    while (lexer_.peek().kind != TokenKind::Dedent &&
           lexer_.peek().kind != TokenKind::EndOfFile) {
        skip_newlines();
        if (lexer_.peek().kind == TokenKind::Dedent ||
            lexer_.peek().kind == TokenKind::EndOfFile) break;

        if (Node* n = parse_inner_stmt()) stmts.push_back(n);
    }
    lexer_.match(TokenKind::Dedent);
    return stmts;
}

// ─── Statement dispatch (inside a block) ─────────────────────────────────────

Node* Parser::parse_inner_stmt() {
    const Token& t = lexer_.peek();

    if (t.kind == TokenKind::KwApply)
        return parse_apply();

    if (t.kind == TokenKind::Identifier) {
        std::string_view sv = lexer_.slice(t);

        // on <event> : <block>
        if (sv == "on")
            return parse_on_decl();

        // <tag> [<name>] : <block>  — any identifier followed by ':'
        // peek(1) can be ':' (no name) or Identifier then ':' (with name).
        if (lexer_.peek(1).kind == TokenKind::Colon ||
            (lexer_.peek(1).kind == TokenKind::Identifier &&
            lexer_.peek(2).kind == TokenKind::Colon))
            return parse_node_decl();

        // <key> = <value>
        if (lexer_.peek(1).kind == TokenKind::Equals)
            return parse_prop_decl();
    }

    unexpected(t, "block statement");
    lexer_.pop();
    return nullptr;
}

// ─── Top-level declarations ───────────────────────────────────────────────────

Node* Parser::parse_var_decl() {
    Token name_t = lexer_.pop(); // Identifier already verified by parse()
    lexer_.expect(TokenKind::Equals);
    Node* val = parse_value();
    lexer_.match(TokenKind::Newline);

    auto* v = ast_.make<VarDecl>(std::string(lexer_.slice(name_t)));
    v->value = val;
    v->range(name_t);
    if (val) v->range(val);
    return v;
}

Node* Parser::parse_style_decl() {
    Token kw = lexer_.pop(); // KwStyle
    Token name_t = lexer_.expect(TokenKind::Identifier, "style name");
    lexer_.expect(TokenKind::Colon);

    auto* s = ast_.make<StyleDecl>(std::string(lexer_.slice(name_t)));
    s->range(kw);
    s->range(name_t);
    s->props = parse_block();
    return s;
}

Node* Parser::parse_root_decl() {
    Token t = lexer_.pop(); // "root" identifier
    lexer_.expect(TokenKind::Colon);

    auto* n = ast_.make<NodeDecl>("root");
    n->range(t);
    n->props = parse_block();
    return n;
}

// ─── Node / On / Apply ───────────────────────────────────────────────────────

Node* Parser::parse_node_decl() {
    Token tag_t = lexer_.pop();
    std::string tag(lexer_.slice(tag_t));
    std::string name;

    // Optional name between tag and colon.
    if (lexer_.peek().kind == TokenKind::Identifier &&
        lexer_.peek(1).kind == TokenKind::Colon) {
        Token name_t = lexer_.pop();
        name = std::string(lexer_.slice(name_t));
    }
    lexer_.expect(TokenKind::Colon);

    auto* n = ast_.make<NodeDecl>(std::move(tag), std::move(name));
    n->range(tag_t);
    n->props = parse_block();
    return n;
}

Node* Parser::parse_on_decl() {
    Token on_t = lexer_.pop(); // "on"
    Token event_t = lexer_.pop();
    if (event_t.kind != TokenKind::Identifier)
        unexpected(event_t, "event name");
    lexer_.expect(TokenKind::Colon);

    auto* o = ast_.make<OnDecl>(std::string(lexer_.slice(event_t)));
    o->range(on_t);
    o->range(event_t);
    o->props = parse_block();
    return o;
}

Node* Parser::parse_apply() {
    Token app = lexer_.pop(); // KwApply
    Token style_t = lexer_.pop();
    if (style_t.kind != TokenKind::Identifier)
        unexpected(style_t, "style name after 'apply'");
    lexer_.match(TokenKind::Newline);

    auto* a = ast_.make<ApplyExpr>(std::string(lexer_.slice(style_t)));
    a->range(app);
    a->range(style_t);
    return a;
}

Node* Parser::parse_prop_decl() {
    Token prop_t = lexer_.pop();
    lexer_.expect(TokenKind::Equals);
    Node* val = parse_value();
    lexer_.match(TokenKind::Newline);

    auto* p = ast_.make<PropDecl>(std::string(lexer_.slice(prop_t)));
    p->value = val;
    p->range(prop_t);
    if (val) p->range(val);
    return p;
}

// ─── Expression / value ───────────────────────────────────────────────────────

Node* Parser::parse_expr() {
    const Token& t = lexer_.peek();

    if (t.kind == TokenKind::Number) {
        Token n = lexer_.pop();
        return ast_.make<NumExpr>(lexer_.slice(n));
    }
    if (t.kind == TokenKind::HexColor) {
        Token h = lexer_.pop();
        return ast_.make<HexExpr>(lexer_.slice(h));
    }
    if (t.kind == TokenKind::Boolean) {
        Token b = lexer_.pop();
        return ast_.make<BoolExpr>(lexer_.slice(b) == "true");
    }
    if (t.kind == TokenKind::Identifier) {
        Token id = lexer_.pop();
        return ast_.make<IdentExpr>(lexer_.slice(id));
    }

    // Not a recognised value start.
    return nullptr;
}

// A value is one or more expressions.  A single expression is returned as-is;
// multiple space-separated expressions are wrapped in a ListExpr.
// The value ends at Newline, Indent, Dedent, or EOF.
Node* Parser::parse_value() {
    auto is_value_end = [this] {
        switch (lexer_.peek().kind) {
            case TokenKind::Newline:
            case TokenKind::Indent:
            case TokenKind::Dedent:
            case TokenKind::EndOfFile: return true;
            default:                   return false;
        }
    };

    Node* first = parse_expr();
    if (!first || is_value_end()) return first;

    // Multiple tokens — collect into a ListExpr.
    auto* lst = ast_.make<ListExpr>();
    lst->list.push_back(first);
    lst->range(first);

    while (!is_value_end()) {
        Node* nxt = parse_expr();
        if (!nxt) break;
        lst->list.push_back(nxt);
        lst->range(nxt);
    }
    return lst;
}

// ─── Entry point ─────────────────────────────────────────────────────────────

void Parser::parse() {
    ast_.allocate(4096);

    skip_newlines(); // skip any leading blank lines

    while (lexer_.peek().kind != TokenKind::EndOfFile) {
        skip_newlines();
        if (lexer_.peek().kind == TokenKind::EndOfFile) break;

        const Token& t = lexer_.peek();

        if (t.kind == TokenKind::KwStyle) {
            ast_.nodes.push_back(parse_style_decl());
            continue;
        }

        if (t.kind == TokenKind::Identifier) {
            std::string_view sv = lexer_.slice(t);

            // root : <block>
            if (sv == "root" && lexer_.peek(1).kind == TokenKind::Colon) {
                ast_.nodes.push_back(parse_root_decl());
                continue;
            }
            // Any identifier followed by ':' is a top-level NodeDecl.
            if (lexer_.peek(1).kind == TokenKind::Colon ||
                (lexer_.peek(1).kind == TokenKind::Identifier &&
                lexer_.peek(2).kind == TokenKind::Colon)) {
                ast_.nodes.push_back(parse_node_decl());
                continue;
            }
            // <ident> = <value>  — global variable binding
            if (lexer_.peek(1).kind == TokenKind::Equals) {
                ast_.nodes.push_back(parse_var_decl());
                continue;
            }
        }

        unexpected(t, "top-level declaration");
        lexer_.pop();
    }
}

} // namespace lintel::parser
