#include "ast.h"

namespace lintel::parser {

static bool within(TokenKind x, TokenKind low, TokenKind high) {
    return x > low && x < high;
}

const char* to_string(TokenKind kind) {
    // Must stay in exact enum-declaration order.
    static const char* table[] = {
        "<tokens-begin>",
        "<null>",
        "<end-of-file>",
        "<error>",
        "<literals-begin>",
        "<identifier>",
        "<number>",
        "<boolean>",
        "<hex-color>",
        "<literals-end>",
        "<keywords-begin>",
        "style",
        "apply",
        "<keywords-end>",
        "<punctuators-begin>",
        "\"",
        ":",
        "=",
        "<newline>",
        "<indent>",
        "<dedent>",
        "<punctuators-end>",
        "<tokens-end>"
    };
    return within(kind, TokenKind::__TokenBegin, TokenKind::__TokenEnd)
        ? table[static_cast<size_t>(kind)]
        : "<invalid-token>";
}

bool is_keyword(TokenKind kind) {
    return within(kind, TokenKind::__KeywordsBegin, TokenKind::__KeywordsEnd);
}

bool operator == (const std::string& str, TokenKind kind) { return str == to_string(kind); }
bool operator == (const Token& t, TokenKind kind) { return t.kind == kind; }
bool operator != (const Token& t, TokenKind kind) { return t.kind != kind; }

} // namespace lintel::parser
