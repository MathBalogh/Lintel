#include "ast.h"
#include <charconv>
#include <iostream>

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
        "template",
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

Color HexExpr::to_color() const {
    std::string_view hex = value;
    if (!hex.empty() && hex[0] == '#') hex.remove_prefix(1);
    auto nibble = [] (char c) -> uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    auto byte2 = [&] (size_t i) -> float {
        return (nibble(hex[i]) * 16 + nibble(hex[i + 1])) / 255.f;
    };
    if (hex.size() == 6) return Color::rgb(byte2(0), byte2(2), byte2(4), 1.f);
    if (hex.size() == 8) return Color::rgb(byte2(0), byte2(2), byte2(4), byte2(6));
    std::cerr << "malformed hex colour '#" << hex << "' - defaulted to black\n";
    return Color::rgb(0.f, 0.f, 0.f, 1.f);
}

float NumExpr::to_number() const {
    float f = 0.f;
    std::from_chars(text.data(), text.data() + text.size(), f);
    return f;
}

} // namespace lintel::parser
