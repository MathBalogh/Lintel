// stylesheet.cpp
//
// Implements StyleSheet: the resolved, AST-independent style store.
//
// This file is the new home for all prop-dispatch logic.  Previously this
// lived in three almost-identical forms inside load.cpp:
//
//   • apply_one_prop()       - snap application during tree build
//   • animate_one_prop()     - animated application during event dispatch
//   • resolve_props()        - conversion of AST nodes → ResolvedProps
//
// All three collapsed into StyleSheet::dispatch_prop() with a Mode parameter.
// The ~120 lines of duplicated dispatch are now ~60 lines that both load() and
// runtime apply() share.

#include "stylesheet.h"
#include "inode.h"          // INode::apply, INode::animate_prop
#include "framework.h"      // FRAMEWORK.get_property(), get_event()

#include <charconv>
#include <cwchar>
#include <iostream>
#include <cwctype>

namespace lintel {

// ─── Internal helpers ─────────────────────────────────────────────────────────

namespace {

// Convert a wide string to UTF-8.  Used for transition spec parsing.
static std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    #ifdef _WIN32
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
    #else
    return std::string(w.begin(), w.end()); // ASCII fallback
    #endif
}

// ── Shorthand expansion: padding / margin ────────────────────────────────────
//
// Accepts either a single float (all sides) or a wstring of 1, 2, or 4
// space-separated values following the CSS convention:
//   "8"          → all sides 8
//   "4 8"        → vertical 4, horizontal 8
//   "4 8 4 8"    → top right bottom left

static Edges parse_edges(const PropValue& raw) {
    if (const float* f = std::get_if<float>(&raw))
        return Edges(*f);

    if (const std::wstring* ws = std::get_if<std::wstring>(&raw)) {
        std::vector<float> vals;
        size_t i = 0;
        while (i < ws->size()) {
            while (i < ws->size() && std::iswspace((*ws)[i])) ++i;
            if (i >= ws->size()) break;
            size_t j = i;
            while (j < ws->size() && !std::iswspace((*ws)[j])) ++j;

            std::wstring tok = ws->substr(i, j - i);
            std::string  narrow_tok = narrow(tok);
            float f = 0.f;
            std::from_chars(narrow_tok.data(),
                            narrow_tok.data() + narrow_tok.size(), f);
            vals.push_back(f);
            i = j;
        }
        if (vals.size() == 1) return Edges(vals[0]);
        if (vals.size() == 2) return Edges(vals[0], vals[1]);
        if (vals.size() == 4) return Edges(vals[0], vals[1], vals[2], vals[3]);
        std::cerr << "stylesheet: padding/margin expects 1, 2 or 4 values, got "
            << vals.size() << " - defaulting to 0\n";
    }
    return Edges(0.f);
}

// ── Transition installation ───────────────────────────────────────────────────
//
// Spec format (narrow string): "<property-name> <duration-seconds> <easing>"
// Example: "background-color 0.2 ease-out"

static void install_transition(Node& n, const std::wstring& spec_w) {
    const std::string spec = narrow(spec_w);

    std::vector<std::string_view> tokens;
    for (size_t i = 0; i < spec.size(); ) {
        while (i < spec.size() && std::isspace(static_cast<unsigned char>(spec[i]))) ++i;
        if (i >= spec.size()) break;
        size_t j = i;
        while (j < spec.size() && !std::isspace(static_cast<unsigned char>(spec[j]))) ++j;
        tokens.emplace_back(std::string_view(spec).substr(i, j - i));
        i = j;
    }
    if (tokens.empty()) return;

    Property prop = FRAMEWORK.get_property(std::string(tokens[0]));
    if (prop == 0) {
        std::cerr << "stylesheet: unknown property '" << tokens[0]
            << "' in transition spec - skipped\n";
        return;
    }

    TransitionSpec ts;
    if (tokens.size() >= 2)
        std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(),
                        ts.duration);
    if (tokens.size() >= 3) {
        const auto& ek = tokens[2];
        if (ek == "linear")      ts.easing = Easing::Linear;
        else if (ek == "ease-in")     ts.easing = Easing::EaseIn;
        else if (ek == "ease-out")    ts.easing = Easing::EaseOut;
        else if (ek == "ease-in-out") ts.easing = Easing::EaseInOut;
        else if (ek == "spring")      ts.easing = Easing::Spring;
    }
    n.handle<INode>()->transitions_[prop] = ts;
}

} // anonymous namespace

// ─── StyleSheet::dispatch_prop ────────────────────────────────────────────────
//
// Central dispatch for a single key/value pair.
// Mode::Snap   → INode::apply()         (immediate write to Attributes)
// Mode::Animate → INode::animate_prop() (through the tween system)

/*static*/
void StyleSheet::dispatch_prop(Node& n, std::string_view key,
                               const PropValue& val, Mode mode) {

    INode* impl = n.handle<INode>();
    if (!impl) return;

    // ── Shorthands ───────────────────────────────────────────────────────────

    if (key == "padding") { n.padding(parse_edges(val)); return; }
    if (key == "margin") { n.margin(parse_edges(val));  return; }

    // ── transition (install only, never animated) ────────────────────────────

    if (key == "transition") {
        if (const std::wstring* w = std::get_if<std::wstring>(&val))
            install_transition(n, *w);
        return;
    }

    // ── Enum-valued properties ────────────────────────────────────────────────
    //
    // These are stored as float (int cast) in Attributes because PropValue
    // has no enum slot.  The wstring check below reads the declarative name
    // from the .lintel file; programmatic callers may also pass a float.

    if (key == "direction") {
        float fval = 0.f;
        if (const std::wstring* w = std::get_if<std::wstring>(&val)) {
            Direction d = (*w == L"row") ? Direction::Row : Direction::Column;
            fval = static_cast<float>(static_cast<int>(d));
        }
        else if (const float* f = std::get_if<float>(&val)) {
            fval = *f;
        }
        if (mode == Mode::Snap)
            impl->apply(property::Direction, fval);
        else
            impl->animate_prop(property::Direction, fval);
        return;
    }

    if (key == "align") {
        float fval = static_cast<float>(static_cast<int>(Align::Stretch));
        if (const std::wstring* w = std::get_if<std::wstring>(&val)) {
            Align a = Align::Stretch;
            if (*w == L"start")  a = Align::Start;
            else if (*w == L"center") a = Align::Center;
            else if (*w == L"end")    a = Align::End;
            fval = static_cast<float>(static_cast<int>(a));
        }
        else if (const float* f = std::get_if<float>(&val)) {
            fval = *f;
        }
        if (mode == Mode::Snap)
            impl->apply(property::AlignItems, fval);
        else
            impl->animate_prop(property::AlignItems, fval);
        return;
    }

    if (key == "justify") {
        float fval = static_cast<float>(static_cast<int>(Justify::Start));
        if (const std::wstring* w = std::get_if<std::wstring>(&val)) {
            Justify j = Justify::Start;
            if (*w == L"center")        j = Justify::Center;
            else if (*w == L"end")           j = Justify::End;
            else if (*w == L"space-between") j = Justify::SpaceBetween;
            else if (*w == L"space-around")  j = Justify::SpaceAround;
            fval = static_cast<float>(static_cast<int>(j));
        }
        else if (const float* f = std::get_if<float>(&val)) {
            fval = *f;
        }
        if (mode == Mode::Snap)
            impl->apply(property::JustifyItems, fval);
        else
            impl->animate_prop(property::JustifyItems, fval);
        return;
    }

    if (key == "text-align") {
        float fval = static_cast<float>(static_cast<int>(TextAlign::Left));
        if (const std::wstring* w = std::get_if<std::wstring>(&val)) {
            TextAlign ta = TextAlign::Left;
            if (*w == L"center")  ta = TextAlign::Center;
            else if (*w == L"right")   ta = TextAlign::Right;
            else if (*w == L"justify") ta = TextAlign::Justify;
            fval = static_cast<float>(static_cast<int>(ta));
        }
        else if (const float* f = std::get_if<float>(&val)) {
            fval = *f;
        }
        if (mode == Mode::Snap)
            impl->apply(property::TextAlign, fval);
        else
            impl->animate_prop(property::TextAlign, fval);
        return;
    }

    // ── Behaviour flags (not animatable) ────────────────────────────────────

    if (key == "focusable") {
        if (const bool* b = std::get_if<bool>(&val)) n.focusable(*b);
        return;
    }
    if (key == "draggable") {
        if (const bool* b = std::get_if<bool>(&val)) n.draggable(*b);
        return;
    }

    // ── Generic framework property ───────────────────────────────────────────

    Property p = FRAMEWORK.get_property(std::string(key));
    if (p == 0) {
        std::cerr << "stylesheet: unknown property '" << key << "' - skipped\n";
        return;
    }

    if (mode == Mode::Snap)
        impl->apply(p, val);
    else
        impl->animate_prop(p, val);
}

// ─── StyleSheet::apply_props / wire_handlers ──────────────────────────────────

/*static*/
void StyleSheet::apply_props(Node& n, const std::vector<Prop>& props, Mode mode) {
    for (const Prop& p : props)
        dispatch_prop(n, p.key, p.value, mode);
}

/*static*/
void StyleSheet::wire_handlers(Node& n, const std::vector<Handler>& handlers) {
    for (const Handler& h : handlers) {
        // Capture deltas by value so the lambda is self-contained even after
        // the StyleSheet or the AST is freed.  Use shared_ptr so nodes that
        // share the same style don't each copy the vector.
        auto shared_deltas =
            std::make_shared<const std::vector<Prop>>(h.deltas);

        n.on(h.event, [shared_deltas] (WeakNode self) {
            StyleSheet::animate(self.as(), *shared_deltas);
        });
    }
}

// ─── StyleSheet::animate ──────────────────────────────────────────────────────

/*static*/
void StyleSheet::animate(Node& n, const std::vector<Prop>& deltas) {
    apply_props(n, deltas, Mode::Animate);
}

// ─── StyleSheet::apply ────────────────────────────────────────────────────────

void StyleSheet::apply(Node& n, std::string_view style_name) const {
    auto it = styles_.find(std::string(style_name));
    if (it == styles_.end()) {
        std::cerr << "stylesheet: undefined style '" << style_name << "' - skipped\n";
        return;
    }
    const Style& s = it->second;
    apply_props(n, s.props, Mode::Snap);
    wire_handlers(n, s.handlers);
}

// ─── StyleSheet build API ─────────────────────────────────────────────────────

StyleSheet& StyleSheet::define(std::string name,
                               std::vector<Prop>    props,
                               std::vector<Handler> handlers) {
    Style& s = styles_[std::move(name)];
    s.props = std::move(props);
    s.handlers = std::move(handlers);
    return *this;
}

StyleSheet& StyleSheet::define_handler(const std::string& name,
                                       Event              event,
                                       std::vector<Prop>  deltas) {
    styles_[name].handlers.push_back({ event, std::move(deltas) });
    return *this;
}

// ─── Query ────────────────────────────────────────────────────────────────────

bool StyleSheet::has_style(std::string_view name) const {
    return styles_.count(std::string(name)) != 0;
}

const StyleSheet::Style* StyleSheet::find_style(std::string_view name) const {
    auto it = styles_.find(std::string(name));
    return it == styles_.end() ? nullptr : &it->second;
}

} // namespace lintel
