#include "parser.h"
#include <iostream>

namespace lintel::parser {

class Parser {
    Lexer lexer_;
    AST& ast_;

    // ── Parse helpers ─────────────────────────────────────────────────────────
    void error(const Token& at, std::string msg) { lexer_.error(at, std::move(msg)); }
    void unexpected(const Token& t, std::string_view ctx) { lexer_.unexpected_token_error(t, ctx); }

    // ── Block (now brace-delimited - no whitespace/indent dependency) ────────
    std::vector<Node*> parse_block() {
        lexer_.expect(TokenKind::LBrace);

        std::vector<Node*> stmts;
        while (lexer_.peek().kind != TokenKind::RBrace &&
               lexer_.peek().kind != TokenKind::EndOfFile) {

            if (Node* n = parse_inner_stmt())
                stmts.push_back(n);
        }

        lexer_.expect(TokenKind::RBrace);
        return stmts;
    }

    // ── Statements ───────────────────────────────────────────────────────────
    Node* parse_inner_stmt() {
        const Token& t = lexer_.peek();

        if (t.kind == TokenKind::KwApply)
            return parse_apply();

        if (t.kind == TokenKind::Identifier) {
            std::string_view sv = lexer_.slice(t);

            if (sv == "on")
                return parse_on_decl();

            // Check whether this is a property assignment (possibly dotted: main-text.content = …)
            // or a node declaration.  We peek ahead without consuming.
            bool is_prop_assign = false;
            size_t off = 1;
            TokenKind nk = lexer_.peek(off).kind;
            if (nk == TokenKind::Equals) {
                is_prop_assign = true;
            }
            else if (nk == TokenKind::Dot) {
                do {
                    off++;  // skip Dot
                    if (lexer_.peek(off).kind != TokenKind::Identifier) {
                        break;
                    }
                    off++;  // skip Identifier after dot
                    nk = lexer_.peek(off).kind;
                } while (nk == TokenKind::Dot);

                if (nk == TokenKind::Equals) {
                    is_prop_assign = true;
                }
            }

            if (is_prop_assign)
                return parse_prop_decl();

            // Otherwise: node declaration (full form with optional args / as / block)
            return parse_node_decl();
        }

        unexpected(t, "block statement");
        lexer_.pop();
        return nullptr;
    }

    // ── Top-level ────────────────────────────────────────────────────────────
    Node* parse_var_decl() {
        Token name_t = lexer_.pop();
        lexer_.expect(TokenKind::Equals);

        Node* val = parse_value();

        auto* v = ast_.make<VarDecl>(std::string(lexer_.slice(name_t)));
        v->value = val;
        v->range(name_t);
        if (val) v->range(val);
        return v;
    }

    Node* parse_style_decl() {
        Token kw = lexer_.pop();
        Token name_t = lexer_.expect(TokenKind::Identifier, "style name");

        auto* s = ast_.make<StyleDecl>(std::string(lexer_.slice(name_t)));
        s->range(kw);
        s->range(name_t);
        s->props = parse_block();
        return s;
    }

    Node* parse_template_decl() {
        // Syntax:  template <name> <base> [( <param> , … )] {
        Token kw = lexer_.pop();
        Token name_t = lexer_.expect(TokenKind::Identifier, "template name");
        Token base_t = lexer_.expect(TokenKind::Identifier, "base node type (node/text/graph/...)");

        // Optional parameter list  (bg, border, …)
        std::vector<std::string> params;
        if (lexer_.peek().kind == TokenKind::LParen) {
            lexer_.pop(); // consume '('
            while (lexer_.peek().kind != TokenKind::RParen &&
                   lexer_.peek().kind != TokenKind::EndOfFile) {
                if (lexer_.peek().kind == TokenKind::Identifier) {
                    Token pt = lexer_.pop();
                    params.push_back(std::string(lexer_.slice(pt)));
                }
                lexer_.match(TokenKind::Comma);
            }
            lexer_.expect(TokenKind::RParen);
        }

        auto* t = ast_.make<TemplateDecl>(
            std::string(lexer_.slice(name_t)),
            std::string(lexer_.slice(base_t))
        );
        t->params = std::move(params);
        t->range(kw);
        t->range(name_t);
        t->range(base_t);
        t->props = parse_block();
        return t;
    }

    // ── Node / On / Apply ────────────────────────────────────────────────────
    Node* parse_node_decl() {
        // Syntax:
        //   tag [ (args…) ] [as <id>] {
        Token tag_t = lexer_.pop();
        std::string tag(lexer_.slice(tag_t));

        // Optional parenthesized, comma-separated argument list.
        std::vector<Node*> args;
        if (lexer_.peek().kind == TokenKind::LParen) {
            lexer_.pop(); // consume '('
            while (lexer_.peek().kind != TokenKind::RParen &&
                   lexer_.peek().kind != TokenKind::EndOfFile) {
                Node* a = parse_expr();
                if (a) args.push_back(a);
                else break;
                if (!lexer_.match(TokenKind::Comma)) break;
            }
            lexer_.expect(TokenKind::RParen);
        }

        // Optional instance identifier:  as myRef
        std::string id;
        if (lexer_.peek().kind == TokenKind::KwAs) {
            lexer_.pop(); // consume 'as'
            Token id_t = lexer_.expect(TokenKind::Identifier, "instance identifier after 'as'");
            id = std::string(lexer_.slice(id_t));
        }

        auto* n = ast_.make<NodeDecl>(std::move(tag));
        n->id = std::move(id);
        n->args = std::move(args);
        n->range(tag_t);

        // Block is now mandatory (brace syntax)
        n->props = parse_block();

        return n;
    }

    Node* parse_on_decl() {
        Token on_t = lexer_.pop();
        Token event_t = lexer_.pop();

        if (event_t.kind != TokenKind::Identifier)
            unexpected(event_t, "event name");

        auto* o = ast_.make<OnDecl>(std::string(lexer_.slice(event_t)));
        o->range(on_t);
        o->range(event_t);
        o->props = parse_block();
        return o;
    }

    Node* parse_apply() {
        Token app = lexer_.pop(); // 'apply'
        Node* style_expr = parse_expr();

        if (!style_expr) {
            unexpected(lexer_.peek(), "style name or template parameter after 'apply'");
            return nullptr;
        }

        auto* a = ast_.make<ApplyExpr>();
        a->style_node = style_expr;
        a->range(app);
        a->range(style_expr);
        return a;
    }

    Node* parse_prop_decl() {
        // Property name (supports dotted paths for cross-node references: main-text.content)
        std::string property;
        Token prop_t = lexer_.pop();
        if (prop_t.kind != TokenKind::Identifier) {
            unexpected(prop_t, "property name");
            return nullptr;
        }
        property = std::string(lexer_.slice(prop_t));

        while (lexer_.peek().kind == TokenKind::Dot) {
            lexer_.pop(); // consume '.'
            Token next_t = lexer_.expect(TokenKind::Identifier, "property name after '.'");
            property += ".";
            property += lexer_.slice(next_t);
        }

        lexer_.expect(TokenKind::Equals);

        Node* val = parse_value();

        auto* p = ast_.make<PropDecl>(std::move(property));
        p->value = val;
        p->range(prop_t);
        if (val) p->range(val);
        return p;
    }

    // ── Expressions ──────────────────────────────────────────────────────────
    Node* parse_expr() {
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

        return nullptr;
    }

    Node* parse_value() {
        Node* first = parse_expr();
        if (!first) return nullptr;

        // Support comma-separated lists for production UI values (e.g. padding = 4, 8, 4, 8)
        // Single values remain the common case.
        if (lexer_.peek().kind != TokenKind::Comma)
            return first;

        auto* lst = ast_.make<ListExpr>();
        lst->list.push_back(first);
        lst->range(first);

        while (lexer_.match(TokenKind::Comma)) {
            Node* nxt = parse_expr();
            if (!nxt) break;
            lst->list.push_back(nxt);
            lst->range(nxt);
        }
        return lst;
    }

public:
    explicit Parser(std::string_view src, AST& root)
        : lexer_(src, root), ast_(root) {}

    void parse() {
        ast_.allocate(4096);

        while (lexer_.peek().kind != TokenKind::EndOfFile) {
            const Token& t = lexer_.peek();

            if (t.kind == TokenKind::KwStyle) {
                ast_.nodes.push_back(parse_style_decl());
                continue;
            }
            if (t.kind == TokenKind::KwTemplate) {
                ast_.nodes.push_back(parse_template_decl());
                continue;
            }

            if (t.kind == TokenKind::Identifier) {
                // Top-level variable binding vs node declaration
                if (lexer_.peek(1).kind == TokenKind::Equals) {
                    ast_.nodes.push_back(parse_var_decl());
                    continue;
                }
                ast_.nodes.push_back(parse_node_decl());
                continue;
            }

            unexpected(t, "top-level declaration");
            lexer_.pop();
        }
    }
};

// ── Entry point ─────────────────────────────────────────────────────────────

void parse(std::string_view src, AST& ast) {
    Parser parser(src, ast);
    parser.parse();

    if (!ast.errors.empty()) {
        for (const parser::Error& e : ast.errors)
            std::cerr << "parse error at "
            << e.line_column.begin << ':'
            << e.line_column.end << ": "
            << e.message << '\n';
    }
}

} // namespace lintel::parser
