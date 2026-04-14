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
enum class TokenKind {
    __TokenBegin,
    Null,
    EndOfFile,
    Error,
    __LiteralsBegin,
    Identifier,
    Number,
    Boolean,
    HexColor,
    __LiteralsEnd,
    __KeywordsBegin,
    KwStyle,
    KwApply,
    KwTemplate,
    KwAs,
    __KeywordsEnd,
    __PunctuatorsBegin,
    Quote,
    Colon,
    Equals,
    LParen,
    RParen,
    Comma,
    LBrace,
    RBrace,
    Dot,
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
    IdentExpr,
    NumExpr,
    HexExpr,
    BoolExpr,
    CallExpr,
    ListExpr,

    ApplyExpr,     // apply <style-name>   ← style-name is now a Node*

    VarDecl,
    NodeDecl,
    PropDecl,
    StyleDecl,
    OnDecl,
    TemplateDecl,

    AST
};

// ─── Error ────────────────────────────────────────────────────────────────────

struct Error {
    Range       range;
    Range       line_column;
    std::string message;
};

// ─── Base node ────────────────────────────────────────────────────────────────

struct Node {
    const NodeKind kind;
    Range          contents;

    explicit Node(NodeKind k): kind(k) {}

    void range(const Token& t);
    void range(const Node* n);

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

// ─── Declaration nodes ────────────────────────────────────────────────────────

struct ApplyExpr : Node {           // CHANGED: now holds Node* instead of std::string
    Node* style_node = nullptr;
    explicit ApplyExpr(): Node(NodeKind::ApplyExpr) {}
};

struct VarDecl : Node {
    std::string name;
    Node* value = nullptr;
    explicit VarDecl(std::string n): Node(NodeKind::VarDecl), name(std::move(n)) {}
};

struct NodeDecl : Node {
    std::string        tag;
    std::string        id;
    std::vector<Node*> args;
    std::vector<Node*> props;
    explicit NodeDecl(std::string tag)
        : Node(NodeKind::NodeDecl), tag(std::move(tag)) {}
};

struct PropDecl : Node {
    std::string property;
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

struct TemplateDecl : Node {
    std::string              name;
    std::string              base;
    std::vector<std::string> params;
    std::vector<Node*>       props;
    explicit TemplateDecl(std::string n, std::string b)
        : Node(NodeKind::TemplateDecl), name(std::move(n)), base(std::move(b)) {}
};

struct AST : Node {
    std::pmr::monotonic_buffer_resource* data = nullptr;
    std::vector<Node*>                   nodes;
    std::string_view                     source;
    std::vector<Error>                   errors;

    template<class T, class... Args>
    T* make(Args&&... args) {
        std::pmr::polymorphic_allocator<std::byte> alloc(data);
        void* mem = alloc.allocate_bytes(sizeof(T), alignof(T));
        return new (mem) T{ std::forward<Args>(args)... };
    }

    AST(): Node(NodeKind::AST) {}
    ~AST() { release(); }

    void release();
    void allocate(size_t reserve);
};

} // namespace lintel::parser
