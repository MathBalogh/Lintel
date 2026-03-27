#pragma once  // was missing
#include "ast.h"
#include <deque>
#include <vector>

namespace lintel::parser {

class Lexer {
    // ── Character classification ─────────────────────────────────────────────
    static bool is_horiz_space(char c) { return c == ' ' || c == '\t'; }
    static bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    // Hyphens allowed in continuations so that font-size, text-color etc. lex as
    // single tokens.  A trailing '-' is trimmed in keyword_or_ident().
    static bool is_ident_cont(char c) { return is_ident_start(c) || (c >= '0' && c <= '9') || c == '-'; }
    static bool is_hex_digit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

    TokenKind parse_keyword(std::string_view);

    // ── Source & position ─────────────────────────────────────────────────────
    std::string_view src_;
    uint32_t         cur_ = 0;

    bool   bounds()           const { return cur_ < src_.size(); }
    bool   bounds(uint32_t o) const { return (cur_ + o) < src_.size(); }
    char   ch()               const { return bounds() ? src_[cur_] : '\0'; }
    char   ch(uint32_t o)     const { return bounds(o) ? src_[cur_ + o] : '\0'; }

    // ── Indent tracking ───────────────────────────────────────────────────────
    std::vector<uint32_t> indent_stack_; // stack of column counts, base is 0
    std::deque<Token>     pending_;      // synthetic INDENT/DEDENT tokens queued
    // by handle_indent(), consumed by next()
    std::deque<Token>     look_;         // peek() lookahead buffer

    // ── Token factory ─────────────────────────────────────────────────────────
    Token make(TokenKind k, uint32_t b, uint32_t e) const {
        return Token{ k, Range{b, e} };
    }

    // ── Internal scanners ─────────────────────────────────────────────────────

    // Skip spaces/tabs and block comments on the current line.
    // Does NOT consume '\n' - newlines are handled explicitly in next().
    void skip_horiz() {
        while (true) {
            char c = ch();
            if (c == ' ' || c == '\t') { ++cur_; continue; }
            // line comment: skip to (but not past) the newline
            if (c == '/' && ch(1) == '/') {
                cur_ += 2;
                while (ch() != '\n' && ch() != '\0') ++cur_;
                return; // next char is '\n' or '\0'; let next() handle it
            }
            // block comment: may span lines - treated as transparent whitespace
            if (c == '/' && ch(1) == '*') {
                cur_ += 2;
                while (ch() != '\0') {
                    if (ch() == '*' && ch(1) == '/') { cur_ += 2; break; }
                    ++cur_;
                }
                continue;
            }
            break;
        }
    }

    // Called immediately after consuming a '\n'.
    // Skips blank / comment-only lines, then compares the new indentation level
    // against the stack and pushes Indent / Dedent tokens into pending_.
    void handle_indent();

    Token number() {
        uint32_t b = cur_;
        bool dot = false;
        while (true) {
            char c = ch();
            if (c >= '0' && c <= '9') { ++cur_; continue; }
            if (c == '.' && !dot) { dot = true; ++cur_; continue; }
            break;
        }
        if (ch() == 'f') ++cur_; // accept "1.0f" format
        return make(TokenKind::Number, b, cur_);
    }

    Token keyword_or_ident() {
        uint32_t b = cur_;
        while (is_ident_cont(ch())) ++cur_;
        // Trim trailing hyphens so "foo-" does not produce a dangling dash.
        while (cur_ > b && src_[cur_ - 1] == '-') --cur_;

        std::string_view lexeme = src_.substr(b, cur_ - b);

        if (lexeme == "true" || lexeme == "false")
            return make(TokenKind::Boolean, b, cur_);
        if (TokenKind kw = parse_keyword(lexeme); kw != TokenKind::Null)
            return make(kw, b, cur_);
        return make(TokenKind::Identifier, b, cur_);
    }

    Token hex_color() {
        uint32_t b = cur_;
        ++cur_; // consume '#'
        while (is_hex_digit(ch())) ++cur_;
        return make(TokenKind::HexColor, b, cur_);
    }

    // Quoted string: returns an Identifier whose range covers the content
    // (without the quote characters themselves).
    Token quoted_string() {
        ++cur_; // skip opening '"'
        uint32_t start = cur_;
        while (bounds() && ch() != '"' && ch() != '\n' && ch() != '\0') ++cur_;
        uint32_t e = cur_;
        if (ch() == '"') {
            ++cur_;
        }
        else {
            root_.errors.push_back(Error{
                Range{start, cur_},
                get_line_column(start),
                "unclosed string literal"
                                   });
        }
        return make(TokenKind::Identifier, start, e);
    }

    // Raw next token - does not touch look_.
    // Callers should go through peek() / pop().
    Token next();

public:
    AST& root_;

    explicit Lexer(std::string_view src, AST& root)
        : src_(src), cur_(0), root_(root) {
        root_.source = src;
        indent_stack_.push_back(0); // base indent level
    }

    // ── Error helpers ─────────────────────────────────────────────────────────

    void error(const Token& at, std::string message) {
        root_.errors.push_back(Error{
            at.range,
            get_line_column(at.range.begin),
            std::move(message)
                               });
    }

    void unexpected_token_error(const Token& t, std::string_view context) {
        std::string msg = "unexpected token '";
        msg += std::string(slice(t));
        msg += '\'';
        if (!context.empty()) { msg += " in "; msg += context; }
        error(t, std::move(msg));
    }

    // ── Lookahead interface ───────────────────────────────────────────────────

    Token pop() {
        const Token t = peek(0);
        look_.erase(look_.begin());
        return t;
    }
    const Token& peek(size_t off = 0) {
        while (off >= look_.size()) look_.push_back(next());
        return look_[off];
    }
    bool match(TokenKind kind) {
        if (peek().kind != kind) return false;
        pop();
        return true;
    }

    // ── Source utilities ──────────────────────────────────────────────────────

    std::string_view slice(const Node* n) const { return src_.substr(n->contents.begin, n->contents.end - n->contents.begin); }
    std::string_view slice(const Token& t) const { return src_.substr(t.range.begin, t.range.end - t.range.begin); }

    Token expect(TokenKind kind, std::string_view desc = "") {
        Token t = pop();
        if (t.kind == kind) return t;
        std::string msg = "expected '";
        msg += desc.empty() ? to_string(kind) : desc;
        msg += "', found '";
        msg += std::string(slice(t));
        msg += '\'';
        error(t, std::move(msg));
        return t;
    }

    // Pop an Identifier token; emit a diagnostic and return "<error>" otherwise.
    std::string_view identifier() {
        Token t = pop();
        if (t.kind == TokenKind::Identifier) return slice(t);
        if (is_keyword(t.kind))
            error(t, "expected identifier, got keyword '" + std::string(slice(t)) + '\'');
        else
            error(t, "expected identifier");
        return "<error>";
    }

    Range get_line_column(uint32_t pos) const {
        Range loc{ 1, 1 };
        for (uint32_t i = 0; i < pos && i < src_.size(); ++i) {
            if (src_[i] == '\n') { ++loc.begin; loc.end = 1; }
            else { ++loc.end; }
        }
        return loc;
    }
};

} // namespace lintel::parser
