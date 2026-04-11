#include "lexer.h"
#include <unordered_map>

namespace lintel::parser {

// ── Keyword map ───────────────────────────────────────────────────────────────

TokenKind Lexer::parse_keyword(std::string_view view) {
    static const auto kw_map = ([] {
        std::unordered_map<std::string_view, TokenKind> map;
        for (auto t = TokenKind::__KeywordsBegin;
             t < TokenKind::__KeywordsEnd;
             t = TokenKind(static_cast<size_t>(t) + 1))
            map[to_string(t)] = t;
        return map;
    })();

    auto it = kw_map.find(view);
    return it == kw_map.end() ? TokenKind::Null : it->second;
}

// ── Main scanner ──────────────────────────────────────────────────────────────

Token Lexer::next() {
    skip_whitespace();

    uint32_t b = cur_;
    char c = ch();

    // ── EOF ──────────────────────────────────────────────────────────────────
    if (c == '\0') {
        return make(TokenKind::EndOfFile, b, b);
    }

    // ── Hex colour ────────────────────────────────────────────────────────────
    if (c == '#') return hex_color();

    // ── Quoted string → Identifier ────────────────────────────────────────────
    if (c == '"') return quoted_string();

    // ── Single-character punctuators ─────────────────────────────────────────
    if (c == ':') { ++cur_; return make(TokenKind::Colon, b, cur_); }
    if (c == '=') { ++cur_; return make(TokenKind::Equals, b, cur_); }
    if (c == '(') { ++cur_; return make(TokenKind::LParen, b, cur_); }
    if (c == ')') { ++cur_; return make(TokenKind::RParen, b, cur_); }
    if (c == ',') { ++cur_; return make(TokenKind::Comma, b, cur_); }
    if (c == '{') { ++cur_; return make(TokenKind::LBrace, b, cur_); }
    if (c == '}') { ++cur_; return make(TokenKind::RBrace, b, cur_); }

    // ── Keywords / identifiers ────────────────────────────────────────────────
    if (is_ident_start(c)) return keyword_or_ident();

    // ── Numbers (including 100px / 50% / .5) ─────────────────────────────────
    if ((c >= '0' && c <= '9') || (c == '.' && ch(1) >= '0' && ch(1) <= '9'))
        return number();

    // ── Standalone dot (for dotted property paths: main-text.content) ────────
    if (c == '.') {
        ++cur_;
        return make(TokenKind::Dot, b, cur_);
    }

    // ── Unknown character ─────────────────────────────────────────────────────
    ++cur_;
    return make(TokenKind::Error, b, cur_);
}

} // namespace lintel::parser
