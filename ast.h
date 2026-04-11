#pragma once
#include "document.h"
#include <memory_resource>
#include <string>
#include <vector>

namespace lintel::parser {

struct Range {
    uint32_t begin = 0;
    uint32_t end = 0;
};

// ─── Token kinds ──────────────────────────────────────────────────────────────
// Sentinel values (__*Begin / __*End) enable O(1) range checks.
// Keep to_string() in ast.cpp in sync with this ordering.

enum class TokenKind {
    __TokenBegin,
    Null,
    EndOfFile,
    Error,
    __LiteralsBegin,
    Identifier, // bare word or quoted-string content (without quotes)
    Number,     // 1  3.14  1.0f  100px  50%
    Boolean,    // true | false
    HexColor,   // #rrggbb[aa]
    __LiteralsEnd,
    __KeywordsBegin,
    KwStyle,
    KwApply,
    KwTemplate,
    KwAs,       // as  - names an instance: button #F00 as myBtn
    __KeywordsEnd,
    __PunctuatorsBegin,
    Quote,      // kept in table for symmetry; lexer emits Identifier for "…"}
    Colon,
    Equals,
    LParen,     // '(' - template parameter list open
    RParen,     // ')' - template parameter list close
    Comma,      // ',' - parameter separator / list separator
    LBrace,     // '{' - block open
    RBrace,     // '}' - block close
    Dot,        // '.' - dotted property path (main-text.content)
    __PunctuatorsEnd,
    __TokenEnd
};

struct Token {
    TokenKind kind = TokenKind::Null;
    Range     range;
};

const char* to_string(TokenKind);
bool        is_keyword(TokenKind);

bool operator == (const std::string&, TokenKind);
bool operator == (const Token&, TokenKind);
bool operator != (const Token&, TokenKind);

// ─── AST node kinds ───────────────────────────────────────────────────────────

enum class NodeKind {
    IdentExpr, // bare identifier / keyword string
    NumExpr,   // numeric literal
    HexExpr,   // hex colour   (#rrggbb)
    BoolExpr,  // true | false
    CallExpr,  // func(a, b, …) - reserved for future function-value support
    ListExpr,  // comma-separated value list  e.g. "4, 8, 4, 8"

    ApplyExpr, // apply <style-name>

    VarDecl,   // <ident> = <value>      (top-level binding)
    NodeDecl,  // <tag> [<n>] : <block>
    PropDecl,  // <ident> = <value>      (inside NodeDecl / StyleDecl / OnDecl - supports dotted paths)
    StyleDecl, // style <n> : <block>
    OnDecl,    // on <event> : <block>
    TemplateDecl, // template <n> : <block> (reusable node structure)

    AST
};

// ─── Error ────────────────────────────────────────────────────────────────────

struct Error {
    Range       range;
    Range       line_column; // {line, column} - both 1-based
    std::string message;
};

// ─── Base node ────────────────────────────────────────────────────────────────

struct Node {
    const NodeKind kind;
    Range          contents;

    explicit Node(NodeKind k): kind(k) {}

    // Expand the source range to include a token or child node.
    void range(const Token& t) {
        if (t.range.begin < contents.begin) contents.begin = t.range.begin;
        if (t.range.end > contents.end)   contents.end = t.range.end;
    }
    void range(const Node* n) {
        if (!n) return;
        if (n->contents.begin < contents.begin) contents.begin = n->contents.begin;
        if (n->contents.end > contents.end)   contents.end = n->contents.end;
    }

    template<typename T>       T& as() { return *static_cast<T*>(this); }
    template<typename T> const T& as() const { return *static_cast<const T*>(this); }
};

// ─── Leaf expression nodes ────────────────────────────────────────────────────

struct IdentExpr : Node {
    std::string name;
    explicit IdentExpr(std::string_view v): Node(NodeKind::IdentExpr), name(v) {}
};
struct NumExpr : Node {
    std::string text;
    explicit NumExpr(std::string_view v): Node(NodeKind::NumExpr), text(v) {}

    float to_number() const;
};
struct HexExpr : Node {
    std::string value;

    explicit HexExpr(std::string_view v): Node(NodeKind::HexExpr), value(v) {}

    Color to_color() const;
};
struct BoolExpr : Node {
    bool value;
    explicit BoolExpr(bool v): Node(NodeKind::BoolExpr), value(v) {}
};

struct ListExpr : Node {
    std::vector<Node*> list;
    ListExpr(): Node(NodeKind::ListExpr) {}
};
struct CallExpr : Node {
    std::string        callee;
    std::vector<Node*> args;
    explicit CallExpr(std::string c): Node(NodeKind::CallExpr), callee(std::move(c)) {}
};

// ─── Declaration / statement nodes ───────────────────────────────────────────

struct ApplyExpr : Node {
    std::string style;
    explicit ApplyExpr(std::string s): Node(NodeKind::ApplyExpr), style(std::move(s)) {}
};

struct VarDecl : Node {
    std::string name;
    Node* value = nullptr;
    explicit VarDecl(std::string n): Node(NodeKind::VarDecl), name(std::move(n)) {}
};

struct NodeDecl : Node {
    std::string        tag;   // "node", "text", "graph", "root", or any registered type
    std::string        id;    // optional instance identifier:  button #F00 as myBtn
    std::vector<Node*> args;  // positional template arguments: button(#FF0000, #00FF00)
    std::vector<Node*> props; // PropDecl | NodeDecl | ApplyExpr | OnDecl
    explicit NodeDecl(std::string tag)
        : Node(NodeKind::NodeDecl), tag(std::move(tag)) {}
};

struct PropDecl : Node {
    std::string property;     // supports dotted paths: "main-text.content"
    Node* value = nullptr;
    explicit PropDecl(std::string prop)
        : Node(NodeKind::PropDecl), property(std::move(prop)) {}
};

struct StyleDecl : Node {
    std::string        name;
    std::vector<Node*> props;
    explicit StyleDecl(std::string n): Node(NodeKind::StyleDecl), name(std::move(n)) {}
};

struct OnDecl : Node {
    std::string        event;
    std::vector<Node*> props;
    explicit OnDecl(std::string ev): Node(NodeKind::OnDecl), event(std::move(ev)) {}
};

// template myBtn node(bg, border):
//   ^name   ^base ^params
struct TemplateDecl : Node {
    std::string              name;    // the template's own identifier
    std::string              base;    // base node type  (node / text / graph / …)
    std::vector<std::string> params;  // parameter names declared in (… )
    std::vector<Node*>       props;
    explicit TemplateDecl(std::string n, std::string b)
        : Node(NodeKind::TemplateDecl), name(std::move(n)), base(std::move(b)) {}
};

// ─── Root ─────────────────────────────────────────────────────────────────────

struct AST : Node {
    std::pmr::monotonic_buffer_resource* data = nullptr;
    std::vector<Node*>                   nodes;
    std::string_view                     source;
    std::vector<Error>                   errors;

    // Allocate a node into the monotonic arena - nodes share the AST's lifetime.
    template<class T, class... Args>
    T* make(Args&&... args) {
        std::pmr::polymorphic_allocator<std::byte> alloc(data);
        void* mem = alloc.allocate_bytes(sizeof(T), alignof(T));
        return new (mem) T{ std::forward<Args>(args)... };
    }

    AST(): Node(NodeKind::AST) {}
    ~AST() { release(); }

    void release() {
        if (data) { data->release(); delete data; }
        data = nullptr;
        nodes.clear();
    }
    void allocate(size_t reserve) {
        release();
        data = new std::pmr::monotonic_buffer_resource(reserve);
    }
};

} // namespace lintel::parser
