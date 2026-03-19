#pragma once
// lexer.h
//
// Lexing, preprocessing, value parsing, and AST types for the cpage document language.
//
// Responsibilities
// ----------------
//  • Variable extraction and substitution  ($name = value)
//  • Line tokenisation (indent level + trimmed content)
//  • Value parsing (colours, numbers, strings, keywords)
//  • Shared AST node/property types consumed by the parser
//
#include "core.h"
#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lintel {

// ----------------------------------------------------------------
// AST types
// ----------------------------------------------------------------

struct Property {
    std::string key;
    AttribValue val;
};

// StyleDelta holds the properties that change when an event fires.
// Keyed by the event name string (e.g. "hover", "focus", "disabled").
using StyleDelta = std::vector<Property>;

struct ASTNode {
    std::string              name;
    std::string              type;
    std::vector<std::string> applies;    // ordered list of style names to apply
    std::vector<Property>    props;
    std::vector<ASTNode>     children;

    // event name -> property delta
    std::unordered_map<std::string, StyleDelta> event_styles;
};

// ----------------------------------------------------------------
// String utilities
// ----------------------------------------------------------------
namespace detail {

inline bool is_ident(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

inline std::string_view trim(std::string_view s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

inline int indent_of(std::string_view s) {
    int level = 0;
    int spaces = 0;
    for (char c : s) {
        if (c == '\t') { level++; spaces = 0; }
        else if (c == ' ') { if (++spaces == 4) { level++; spaces = 0; } }
        else                break;
    }
    return level;
}

} // namespace detail

// ----------------------------------------------------------------
// Preprocessor  (variables)
// ----------------------------------------------------------------
namespace detail {

using VarTable = std::vector<std::pair<std::string, std::string>>;

// Removes variable declaration lines from `lines`, returns the table.
// Longer names are sorted first so substitution avoids prefix collisions.
inline VarTable extract_vars(std::vector<std::string>& lines) {
    VarTable vars;
    std::vector<std::string> rest;
    for (auto& line : lines) {
        auto t = trim(line);
        if (t.starts_with('$')) {
            auto eq = t.find('=');
            if (eq != std::string_view::npos) {
                vars.push_back({
                    std::string(trim(t.substr(0, eq))),
                    std::string(trim(t.substr(eq + 1)))
                               });
                continue;
            }
        }
        rest.push_back(std::move(line));
    }
    std::sort(vars.begin(), vars.end(),
              [] (const auto& a, const auto& b) {
        return a.first.size() > b.first.size();
    });
    lines = std::move(rest);
    return vars;
}

inline void apply_vars(std::vector<std::string>& lines, const VarTable& vars) {
    for (auto& line : lines) {
        for (auto& [name, val] : vars) {
            size_t pos = 0;
            while ((pos = line.find(name, pos)) != std::string::npos) {
                size_t after = pos + name.size();
                if (after < line.size() && is_ident(line[after])) {
                    ++pos;
                    continue;
                }
                line.replace(pos, name.size(), val);
                pos += val.size();
            }
        }
    }
}

} // namespace detail

// ----------------------------------------------------------------
// Value parser
// ----------------------------------------------------------------
namespace detail {

// Forward-declared so register_value_function can live in parser.cpp
// while parse_value calls it.  The map is defined in parser.cpp.
using ValueFuncParser = std::function<AttribValue(std::string_view)>;
extern std::unordered_map<std::string, ValueFuncParser> s_value_func_registry;

inline Color parse_hex_color(std::string_view digits) {
    std::string h(digits);
    if (h.size() == 3)
        h = { h[0], h[0], h[1], h[1], h[2], h[2] };
    if (h.size() == 6)
        h += "ff";
    if (h.size() != 8)
        throw std::runtime_error("malformed hex color");
    auto byte = [&] (int off) {
        unsigned v = 0;
        std::from_chars(h.data() + off, h.data() + off + 2, v, 16);
        return static_cast<float>(v) / 255.f;
    };
    return Color::rgb(byte(0), byte(2), byte(4), byte(6));
}

// parse_value
// -----------
// #rrggbb[aa]            -> Color
// "string"               -> std::wstring
// func(args)             -> AttribValue  (registered function)
// number["px"]           -> float
// true | false           -> bool
// <other>                -> std::wstring (unquoted keyword)
//
inline AttribValue parse_value(std::string_view raw) {
    raw = trim(raw);
    if (raw.empty())
        return std::wstring{};

    if (raw[0] == '#')
        return parse_hex_color(raw.substr(1));

    if (raw.front() == '"' && raw.back() == '"') {
        auto inner = raw.substr(1, raw.size() - 2);
        return std::wstring(inner.begin(), inner.end());
    }

    size_t open = raw.find('(');
    if (open != std::string_view::npos &&
        raw.back() == ')' &&
        open > 0 &&
        is_ident(raw[0])) {
        std::string fname(trim(raw.substr(0, open)));
        auto it = s_value_func_registry.find(fname);
        if (it != s_value_func_registry.end()) {
            std::string_view args = trim(raw.substr(open + 1, raw.size() - open - 2));
            return it->second(args);
        }
    }

    if (raw.ends_with("px")) {
        float f{};
        auto n = raw.substr(0, raw.size() - 2);
        if (std::from_chars(n.data(), n.data() + n.size(), f).ec == std::errc())
            return f;
    }
    {
        float f{};
        if (std::from_chars(raw.data(), raw.data() + raw.size(), f).ec == std::errc())
            return f;
    }

    if (raw == "true")  return true;
    if (raw == "false") return false;

    return std::wstring(raw.begin(), raw.end());
}

} // namespace detail

// ----------------------------------------------------------------
// Lexer
// ----------------------------------------------------------------
struct Line {
    int         indent;
    std::string content;
};

inline std::vector<Line> lex_lines(const std::vector<std::string>& raw) {
    std::vector<Line> out;
    out.reserve(raw.size());
    for (const auto& r : raw) {
        std::string_view t = detail::trim(r);
        if (t.empty()) continue;
        out.push_back({ detail::indent_of(r), std::string(t) });
    }
    return out;
}

} // namespace lintel
