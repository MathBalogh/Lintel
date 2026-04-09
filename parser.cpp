#include "parser.h"
#include <iostream>

namespace lintel::parser {

class Parser {
    Lexer lexer_;
    AST& ast_;

    // ── Parse helpers ─────────────────────────────────────────────────────────
    void error(const Token& at, std::string msg) { lexer_.error(at, std::move(msg)); }
    void unexpected(const Token& t, std::string_view ctx) { lexer_.unexpected_token_error(t, ctx); }

    void skip_newlines() {
        while (lexer_.peek().kind == TokenKind::Newline)
            lexer_.pop();
    }

    // ── Block ────────────────────────────────────────────────────────────────
    std::vector<Node*> parse_block() {
        if (!lexer_.match(TokenKind::Newline)) {
            unexpected(lexer_.peek(), "expected newline before block");
        }

        if (!lexer_.match(TokenKind::Indent)) {
            unexpected(lexer_.peek(), "expected indented block");
            return {};
        }

        std::vector<Node*> stmts;
        while (lexer_.peek().kind != TokenKind::Dedent &&
               lexer_.peek().kind != TokenKind::EndOfFile) {

            skip_newlines();

            if (lexer_.peek().kind == TokenKind::Dedent ||
                lexer_.peek().kind == TokenKind::EndOfFile)
                break;

            if (Node* n = parse_inner_stmt())
                stmts.push_back(n);
        }

        lexer_.match(TokenKind::Dedent);
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

            // NEW: properties use "ident = value" syntax.
            // This check must come BEFORE the node-decl fallback
            // so that "background-color = #000000" is parsed as PropDecl,
            // not mistaken for a shorthand child node.
            if (lexer_.peek(1).kind == TokenKind::Equals)
                return parse_prop_decl();

            // Otherwise: node declaration (full form "tag:" / "tag name:"
            // or the new shorthand one-liner "tag" with empty props).
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
        lexer_.match(TokenKind::Newline);

        auto* v = ast_.make<VarDecl>(std::string(lexer_.slice(name_t)));
        v->value = val;
        v->range(name_t);
        if (val) v->range(val);
        return v;
    }

    Node* parse_style_decl() {
        Token kw = lexer_.pop();
        Token name_t = lexer_.expect(TokenKind::Identifier, "style name");
        lexer_.expect(TokenKind::Colon);

        auto* s = ast_.make<StyleDecl>(std::string(lexer_.slice(name_t)));
        s->range(kw);
        s->range(name_t);
        s->props = parse_block();
        return s;
    }

    Node* parse_template_decl() {
        // Syntax:  template <name> <base> [( <param> , … )] :
        //   template myBtn  node :
        //   template myBtn  node (bg, border) :
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
                // Skip commas between parameters.
                lexer_.match(TokenKind::Comma);
            }
            lexer_.expect(TokenKind::RParen, "')' closing template parameter list");
        }

        lexer_.expect(TokenKind::Colon);

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
        //   tag [args…] [as <id>] :    ← full form with block
        //   tag [args…] [as <id>]      ← shorthand, empty props
        //   tag[(arg, …)] [as <id>] [:]   ← new syntax (args now parenthesized and comma-separated)
        //
        // 'as <id>' explicitly names this instance for later lookup (the identifier
        // may be a template parameter name; it is resolved dynamically at build time).
        Token tag_t = lexer_.pop();
        std::string tag(lexer_.slice(tag_t));
        // New syntax: optional parenthesized, comma-separated argument list.
        // Non-template nodes simply omit the parentheses (args stay empty).
        std::vector<Node*> args;
        if (lexer_.peek().kind == TokenKind::LParen) {
            lexer_.pop(); // consume '('
            while (lexer_.peek().kind != TokenKind::RParen &&
                   lexer_.peek().kind != TokenKind::EndOfFile) {
                Node * a = parse_expr();
                if (a) {
                    args.push_back(a);
                }
                else {
                    break;
                }
                if (!lexer_.match(TokenKind::Comma)) {
                    break;
                }
            }
            lexer_.expect(TokenKind::RParen, "')' closing template arguments");
        }

        // Optional instance identifier:  as myRef
        std::string id;
        if (lexer_.peek().kind == TokenKind::KwAs) {
            lexer_.pop(); // consume 'as'
            Token id_t = lexer_.expect(TokenKind::Identifier, "instance identifier after 'as'");
            id = std::string(lexer_.slice(id_t));
        }

        const bool has_colon = lexer_.match(TokenKind::Colon);

        auto* n = ast_.make<NodeDecl>(std::move(tag));
        n->id = std::move(id);
        n->args = std::move(args);
        n->range(tag_t);

        if (has_colon)
            n->props = parse_block();
        // else: shorthand one-liner, props stay empty

        return n;
    }

    Node* parse_on_decl() {
        Token on_t = lexer_.pop();
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

    Node* parse_apply() {
        Token app = lexer_.pop();
        Token style_t = lexer_.pop();

        if (style_t.kind != TokenKind::Identifier)
            unexpected(style_t, "style name after 'apply'");

        lexer_.match(TokenKind::Newline);

        auto* a = ast_.make<ApplyExpr>(std::string(lexer_.slice(style_t)));
        a->range(app);
        a->range(style_t);
        return a;
    }

    Node* parse_prop_decl() {
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
        auto is_value_end = [this] {
            switch (lexer_.peek().kind) {
                case TokenKind::Newline:
                case TokenKind::Indent:
                case TokenKind::Dedent:
                case TokenKind::EndOfFile:
                    return true;
                default:
                    return false;
            }
        };

        Node* first = parse_expr();
        if (!first || is_value_end())
            return first;

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

public:
    explicit Parser(std::string_view src, AST& root)
        : lexer_(src, root), ast_(root) {}

    void parse() {
        ast_.allocate(4096);

        skip_newlines();

        while (lexer_.peek().kind != TokenKind::EndOfFile) {
            skip_newlines();
            if (lexer_.peek().kind == TokenKind::EndOfFile) break;

            const Token& t = lexer_.peek();

            if (t.kind == TokenKind::KwStyle) {
                ast_.nodes.push_back(parse_style_decl());
                continue;
            }
            if (t.kind == TokenKind::KwTemplate) {
                ast_.nodes.push_back(parse_template_decl());
                continue;
            }

            // "root:" (or any tag) is now just a normal NodeDecl handled by parse_node_decl.
            // Shorthand "button" (no colon) works here too.
            if (t.kind == TokenKind::Identifier) {
                if (lexer_.peek(1).kind == TokenKind::Equals) {
                    ast_.nodes.push_back(parse_var_decl());
                    continue;
                }
                ast_.nodes.push_back(parse_node_decl()); // full or shorthand
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
