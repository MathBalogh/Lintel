// stylesheet.cpp
//
// Implements StyleSheet: the resolved, AST-independent style store.
//
// Now fully compatible with the new templated Attributes + UIValue system.
// PropValue / std::variant has been removed everywhere.

#include "inode.h"          // INode::apply, INode::animate_prop + UIValue

#include <charconv>
#include <cwchar>
#include <iostream>
#include <cwctype>

namespace lintel {

// ─── Internal helpers ─────────────────────────────────────────────────────────

namespace {

// ── UIValue parser (width / height) ─────────────────────────────────────────
// Accepts numbers (legacy), "auto", "100px", "50%", "50.5%"

static UIValue parse_ui_value(const Property& raw) {
    if (raw.is_ui())
        return raw.get_ui();

    if (raw.is_wstring()) {
        std::wstring s = raw.get_wstring();
        // trim whitespace (simple)
        while (!s.empty() && std::iswspace(s.front())) s.erase(s.begin());
        while (!s.empty() && std::iswspace(s.back())) s.pop_back();

        if (s == L"auto")
            return UIValue::make_auto();

        // percent?
        if (!s.empty() && s.back() == L'%') {
            s.pop_back();
            std::string narrow = to_string(s);
            float pct = 0.f;
            std::from_chars(narrow.data(), narrow.data() + narrow.size(), pct);
            return UIValue::pct(pct / 100.f);
        }

        // pixels (with or without "px" suffix)
        if (!s.empty() && (std::iswdigit(s[0]) || s[0] == L'-' || s[0] == L'.')) {
            if (s.size() >= 3 && s.substr(s.size() - 2) == L"px")
                s = s.substr(0, s.size() - 2);

            std::string narrow = to_string(s);
            float px = 0.f;
            std::from_chars(narrow.data(), narrow.data() + narrow.size(), px);
            return UIValue::px(px);
        }

        std::cerr << "stylesheet: bad UIValue '" << to_string(s) << "' - defaulting to auto\n";
    }
    return UIValue::make_auto();
}

// ── Shorthand expansion: padding / margin ────────────────────────────────────

static Edges parse_edges(const Property& raw) {
    if (raw.is_float())
        return Edges(raw.get_float());

    if (raw.is_wstring()) {
        const std::wstring& ws = raw.get_wstring();
        std::vector<float> vals;
        size_t i = 0;
        while (i < ws.size()) {
            while (i < ws.size() && std::iswspace(ws[i])) ++i;
            if (i >= ws.size()) break;
            size_t j = i;
            while (j < ws.size() && !std::iswspace(ws[j])) ++j;

            std::wstring tok = ws.substr(i, j - i);
            std::string narrow_tok = to_string(tok);
            float f = 0.f;
            std::from_chars(narrow_tok.data(), narrow_tok.data() + narrow_tok.size(), f);
            vals.push_back(f);
            i = j;
        }
        if (vals.size() == 1) return Edges(vals[0]);
        if (vals.size() == 2) return Edges(vals[0], vals[1]);
        if (vals.size() == 4) return Edges(vals[0], vals[1], vals[2], vals[3]);
        std::cerr << "stylesheet: padding/margin expects 1, 2 or 4 values, got " << vals.size() << " - defaulting to 0\n";
    }
    return Edges(0.f);
}

} // anonymous namespace

// ─── StyleSheet::dispatch_prop ────────────────────────────────────────────────

/*static*/
void StyleSheet::dispatch_prop(Node& n, std::string_view key, const Property& val) {
    INode* impl = n.handle<INode>();
    if (!impl) return;

    // ── Shorthands ───────────────────────────────────────────────────────────

    if (key == "padding") { n.padding(parse_edges(val)); return; }
    if (key == "margin") { n.margin(parse_edges(val));  return; }

    // ── width / height now use UIValue ───────────────────────────────────────

    if (key == "width") {
        UIValue uv = parse_ui_value(val);
        n.width(uv);
        return;
    }
    if (key == "height") {
        UIValue uv = parse_ui_value(val);
        n.height(uv);
        return;
    }

    // ── Enum-valued properties (still stored as float) ───────────────────────

    if (key == "direction") {
        Direction num = DirectionCol;
        if (val.is_wstring()) {
            num = (val.get_wstring() == L"row")
                ? DirectionRow
                : DirectionCol;
        }
        else if (val.is_enum()) {
            num = (Direction) val.get_enum();
        }
        impl->apply(Key::Direction, num);
        return;
    }

    if (key == "align") {
        Align num = AlignStretch;
        if (val.is_wstring()) {
            Align a = AlignStretch;
            if (val.get_wstring() == L"start") a = AlignStart;
            else if (val.get_wstring() == L"center") a = AlignCenter;
            else if (val.get_wstring() == L"end") a = AlignEnd;
            num = a;
        }
        else if (val.is_enum()) {
            num = (Align) val.get_enum();
        }
        impl->apply(Key::AlignItems, num);
        return;
    }

    if (key == "justify") {
        unsigned int fval = (unsigned int) Justify::JustifyStart;
        if (val.is_wstring()) {
            Justify j = Justify::JustifyStart;
            if (val == L"center") j = Justify::JustifyCenter;
            else if (val == L"end") j = Justify::JustifyEnd;
            else if (val == L"space-between") j = Justify::JustifySpaceBetween;
            else if (val == L"space-around") j = Justify::JustifySpaceAround;
            fval = (unsigned int) j;
        }
        else if (val.is_enum()) {
            fval = val.get_enum();
        }
        impl->apply(Key::JustifyItems, fval);
        return;
    }

    if (key == "text-align") {
        unsigned int fval = (unsigned int) TextAlign::TextAlignLeft;
        if (val.is_wstring()) {
            TextAlign ta = TextAlign::TextAlignLeft;
            if (val == L"center") ta = TextAlign::TextAlignCenter;
            else if (val == L"right") ta = TextAlign::TextAlignRight;
            else if (val == L"justify") ta = TextAlign::TextAlignJustify;
            fval = (unsigned int) ta;
        }
        else if (val.is_enum()) {
            fval = val.get_enum();
        }
        impl->apply(Key::TextAlign, fval);
        return;
    }

    // ── Behaviour flags (not animatable) ────────────────────────────────────

    if (key == "focusable") {
        if (val.is_bool()) n.focusable(val);
        return;
    }
    if (key == "draggable") {
        if (val.is_bool()) n.draggable(val);
        return;
    }

    // ── Generic framework property ───────────────────────────────────────────

    Key p = get_key(std::string(key));
    if (p == Key::Null) {
        std::cerr << "stylesheet: unknown property '" << key << "' - skipped\n";
        return;
    }

    impl->apply(p, val);
}

// ─── StyleSheet::apply_props / wire_handlers / animate / apply ────────────────

/*static*/
void StyleSheet::apply_props(Node& n, const std::vector<Prop>& props) {
    for (const Prop& p : props)
        dispatch_prop(n, p.key, p.value);
}

/*static*/
void StyleSheet::wire_handlers(Node& n, const std::vector<Handler>& handlers) {
    for (const Handler& h : handlers) {
        auto shared_deltas = std::make_shared<const std::vector<Prop>>(h.deltas);
        n.on(h.event, [h] (NodePtr self) {
            StyleSheet::apply_props(self.as(), h.deltas);
        });
    }
}

void StyleSheet::apply(Node& n, std::string_view style_name) const {
    auto it = styles_.find(std::string(style_name));
    if (it == styles_.end()) {
        std::cerr << "stylesheet: undefined style '" << style_name << "' - skipped\n";
        return;
    }
    const Style& s = it->second;
    apply_props(n, s.props);
    wire_handlers(n, s.handlers);
}

// ─── StyleSheet build API ─────────────────────────────────────────────────────

StyleSheet& StyleSheet::define(std::string name, std::vector<Prop> props, std::vector<Handler> handlers) {
    Style& s = styles_[std::move(name)];
    s.props = std::move(props);
    s.handlers = std::move(handlers);
    return *this;
}

StyleSheet& StyleSheet::define_handler(const std::string& name, Event event, std::vector<Prop> deltas) {
    styles_[name].handlers.push_back({ event, std::move(deltas) });
    return *this;
}

void StyleSheet::register_node(const std::string& name, NodePtr node) {
    named_[name] = node;
}
NodePtr StyleSheet::find(const char* name) {
    if (auto it = named_.find(name); it != named_.end())
        return it->second;
    return NodePtr(nullptr);
}
void StyleSheet::find(std::initializer_list<std::pair<NodePtr&, const char*>> list) {
    for (auto& [node, name] : list) {
        node = find(name);
    }
}

bool StyleSheet::has_style(std::string_view name) const {
    return styles_.count(std::string(name)) != 0;
}

const StyleSheet::Style* StyleSheet::find_style(std::string_view name) const {
    auto it = styles_.find(std::string(name));
    return it == styles_.end() ? nullptr : &it->second;
}

} // namespace lintel
