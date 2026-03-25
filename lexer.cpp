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

// ── Indent handling ───────────────────────────────────────────────────────────
//
// Called immediately after consuming a '\n'.  Scans forward, skipping blank
// lines and comment-only lines, then computes the indentation level of the
// next real line and emits Indent / Dedent tokens into pending_.
//
// When the file ends without a trailing newline, next() emits the remaining
// Dedents directly; handle_indent() covers the trailing-newline case.

void Lexer::handle_indent() {
    while (true) {
        uint32_t line_start = cur_;
        uint32_t col = 0;

        // Count leading horizontal whitespace.
        while (ch() == ' ' || ch() == '\t') { ++col; ++cur_; }

        if (ch() == '\n') { ++cur_; continue; }           // blank line — skip

        if (ch() == '\0') {
            // EOF after newline: close all open indent levels.
            cur_ = line_start + col;
            while (indent_stack_.size() > 1) {
                indent_stack_.pop_back();
                pending_.push_back(make(TokenKind::Dedent, cur_, cur_));
            }
            return;
        }

        // Comment-only line: skip past it and try again.
        if (ch() == '/' && ch(1) == '/') {
            while (ch() != '\n' && ch() != '\0') ++cur_;
            if (ch() == '\n') { ++cur_; continue; }
            // EOF inside comment — treat like EOF above.
            while (indent_stack_.size() > 1) {
                indent_stack_.pop_back();
                pending_.push_back(make(TokenKind::Dedent, cur_, cur_));
            }
            return;
        }

        // Real content found at indentation level `col`.
        cur_ = line_start + col; // advance past the leading spaces
        uint32_t base = indent_stack_.back();

        if (col > base) {
            indent_stack_.push_back(col);
            pending_.push_back(make(TokenKind::Indent, cur_, cur_));
        }
        else if (col < base) {
            // May need multiple Dedents if indentation drops by more than one level.
            while (indent_stack_.size() > 1 && indent_stack_.back() > col) {
                indent_stack_.pop_back();
                pending_.push_back(make(TokenKind::Dedent, cur_, cur_));
            }
        }
        // If col == base: same level, no synthetic token needed.
        return;
    }
}

// ── Main scanner ──────────────────────────────────────────────────────────────

Token Lexer::next() {
    // Synthetic tokens queued by handle_indent() take priority.
    if (!pending_.empty()) {
        Token t = pending_.front();
        pending_.pop_front();
        return t;
    }

    skip_horiz();

    uint32_t b = cur_;
    char c = ch();

    // ── EOF ──────────────────────────────────────────────────────────────────
    if (c == '\0') {
        // File does not end with a newline: close any open indent levels now.
        while (indent_stack_.size() > 1) {
            indent_stack_.pop_back();
            pending_.push_back(make(TokenKind::Dedent, b, b));
        }
        if (!pending_.empty()) {
            Token t = pending_.front();
            pending_.pop_front();
            return t;
        }
        return make(TokenKind::EndOfFile, b, b);
    }

    // ── Newline — emit Newline token then process indentation ─────────────────
    if (c == '\n') {
        ++cur_;
        handle_indent(); // populates pending_ if indent/dedent
        return make(TokenKind::Newline, b, cur_);
    }

    // ── Hex colour ────────────────────────────────────────────────────────────
    if (c == '#') return hex_color();

    // ── Quoted string → Identifier ────────────────────────────────────────────
    if (c == '"') return quoted_string();

    // ── Single-character punctuators ─────────────────────────────────────────
    if (c == ':') { ++cur_; return make(TokenKind::Colon, b, cur_); }
    if (c == '=') { ++cur_; return make(TokenKind::Equals, b, cur_); }

    // ── Keywords / identifiers ────────────────────────────────────────────────
    if (is_ident_start(c)) return keyword_or_ident();

    // ── Numbers ───────────────────────────────────────────────────────────────
    if ((c >= '0' && c <= '9') || (c == '.' && ch(1) >= '0' && ch(1) <= '9'))
        return number();

    // ── Unknown character ─────────────────────────────────────────────────────
    ++cur_;
    return make(TokenKind::Error, b, cur_);
}

} // namespace lintel::parser
