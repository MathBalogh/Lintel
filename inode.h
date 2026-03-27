#pragma once

#include "core.h"   // → lintel.h → types.h

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <variant>
#include <vector>

namespace lintel {

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

inline bool  is_auto(float v) { return std::isnan(v); }
inline float nan_f() { return std::numeric_limits<float>::quiet_NaN(); }

// ---------------------------------------------------------------------------
// Tween — one in-flight interpolation
// ---------------------------------------------------------------------------
//
// Unchanged from before except that 'range' can no longer hold an
// AnimateDescriptor: that type has been removed from PropValue and the
// per-call duration/easing override is now a separate animate_prop overload.

struct Tween {
    Property prop = 0;

    // Holds either {from,to} floats or {from,to} Colors.
    std::variant<std::pair<float, float>, std::pair<Color, Color>> range;

    float  elapsed = 0.f;
    float  duration = 0.15f;
    Easing easing = Easing::EaseOut;
};

// ---------------------------------------------------------------------------
// INode — internal node implementation
// ---------------------------------------------------------------------------

class INode {
public:
    virtual ~INode() = default;

    virtual void apply_notifier(Property p) {}
    void apply(Property p, PropValue val);

    // -- Handler storage -------------------------------------------------

    struct HandlerEntry {
        Event              type;
        Node::EventHandler fn;
    };
    using HandlerList = std::vector<HandlerEntry>;

    HandlerList       handlers_;

    // -- Core state -------------------------------------------------------

    Rect              rect = {};
    WeakNode          parent;
    std::vector<Node> children;
    Attributes        attr;

    bool focusable_flag = false;
    bool draggable_flag = false;
    bool mouse_inside = false;

    // -- Layout cache -----------------------------------------------------

    float cached_avail_w_ = -1.f;
    float cached_avail_h_ = -1.f;

    // -- Animation --------------------------------------------------------

    std::unordered_map<Property, TransitionSpec> transitions_;
    std::vector<Tween>                           tweens_;
    bool                                         has_active_tweens_ = false;

    // ── animate_prop — primary overload ──────────────────────────────────
    //
    // If transitions_ contains a spec for prop, a Tween is created (or the
    // existing one restarted from the current live value).  Otherwise the
    // value is snapped immediately via apply().
    //
    // PropValue replaces AttribValue here.  AnimateDescriptor is gone from
    // the variant; use the second overload below when a per-call duration or
    // easing override is needed.

    void animate_prop(Property p, const PropValue& target);

    // ── animate_prop — per-call duration / easing override ───────────────
    //
    // Equivalent to the old AnimateDescriptor path.  Preferable for
    // programmatic callers that know the easing at the call site.

    void animate_prop(Property p, float  target, float duration, Easing easing);
    void animate_prop(Property p, Color  target, float duration, Easing easing);

    // ── tick_tweens ──────────────────────────────────────────────────────

    void tick_tweens(float dt);

    // ── Layout accessors — read PropValue from attr ───────────────────────

    float     layout_width()     const;
    float     layout_height()    const;
    float     layout_gap()       const;
    float     layout_share()     const;
    Edges     layout_padding()   const;
    Edges     layout_margin()    const;
    Direction layout_direction() const;
    Align     layout_align()     const;
    Justify   layout_justify()   const;

    // -- Handler registration --------------------------------------------

    void add_handler(Event type, Node::EventHandler fn) {
        handlers_.push_back({ type, std::move(fn) });
    }

    void remove_handlers(Event type) {
        handlers_.erase(
            std::remove_if(handlers_.begin(), handlers_.end(),
            [type] (const HandlerEntry& e) { return e.type == type; }),
            handlers_.end());
    }

    void invoke_handlers(WeakNode handle, Event type) const {
        HandlerList snapshot = handlers_;
        for (const HandlerEntry& e : snapshot)
            if (e.type == type) e.fn(handle);
    }

    void fire(WeakNode self, Event type) {
        invoke_handlers(self, type);
    }

    // -- Layout pipeline -------------------------------------------------

    virtual void measure(float avail_w, float avail_h);
    virtual void arrange(float slot_x, float slot_y);
    void         layout(float slot_x, float slot_y, float avail_w, float avail_h);

    // -- Draw pipeline ---------------------------------------------------

    void draw_default(Canvas& canvas);
    virtual void draw(Node& self, Canvas& canvas);

    // -- Geometry helpers ------------------------------------------------

    float inner_w()   const;
    float inner_h()   const;
    float content_x() const;
    float content_y() const;

    // -- Tree utilities --------------------------------------------------

    Node* find_hit(Node& self, float sx, float sy);
    void   update_hover(WeakNode self, float sx, float sy);
    static void bubble_up(INode* from_impl, Event type);

    // -- Focus utilities -------------------------------------------------

    static void set_focus(WeakNode target);
    static void focus_next(Node& tree_root);

private:
    void   arrange_column();
    void   arrange_row();
    float  resolve_cross_x(INode* child, float inner_width);
    float  resolve_cross_y(INode* child, float inner_height);

    static void collect_focusable(INode* node, std::vector<INode*>& out);

    // Shared implementation for all animate_prop overloads.
    void animate_prop_impl(Property p, float target, float duration, Easing easing);
    void animate_prop_impl(Property p, Color  target, float duration, Easing easing);
};

template class Impl<INode>;

// ---------------------------------------------------------------------------
// Fire helpers — inline definitions
// ---------------------------------------------------------------------------

inline void fire_with_context(
    INode* impl, WeakNode handle,
    Event type, float local_x, float local_y,
    MouseButton btn, Modifiers mods,
    float scroll_dx = 0.f, float scroll_dy = 0.f) {
    InputState& inp = CORE.input;
    inp.mouse_x = local_x;
    inp.mouse_y = local_y;
    inp.held = btn;
    inp.modifiers = mods;
    inp.scroll_dx = scroll_dx;
    inp.scroll_dy = scroll_dy;
    inp.key_vkey = 0;
    inp.key_repeat = false;
    inp.key_char = L'\0';
    impl->invoke_handlers(handle, type);
    impl->invoke_handlers(handle, Event::Any);
}

inline void fire_key_context(
    INode* impl, WeakNode handle,
    Event type, int vkey, bool repeat, Modifiers mods) {
    InputState& inp = CORE.input;
    inp.key_vkey = vkey;
    inp.key_repeat = repeat;
    inp.key_char = L'\0';
    inp.modifiers = mods;
    inp.scroll_dx = 0.f;
    inp.scroll_dy = 0.f;
    impl->invoke_handlers(handle, type);
    impl->invoke_handlers(handle, Event::Any);
}

inline void fire_char_context(
    INode* impl, WeakNode handle,
    wchar_t ch, Modifiers mods) {
    InputState& inp = CORE.input;
    inp.key_char = ch;
    inp.key_vkey = 0;
    inp.key_repeat = false;
    inp.modifiers = mods;
    inp.scroll_dx = 0.f;
    inp.scroll_dy = 0.f;
    impl->invoke_handlers(handle, Event::Char);
    impl->invoke_handlers(handle, Event::Any);
}

} // namespace lintel
