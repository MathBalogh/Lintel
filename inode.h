#pragma once

#include "core.h"   // pulls in lintel.h (Prop, Attributes, etc.) via the chain

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
// Created by INode::animate_prop when an event delta fires on a prop that has
// a TransitionSpec.  Destroyed automatically when elapsed >= duration.
//
// range holds either {from,to} floats or {from,to} Colors so both value
// types are handled without virtual dispatch.
//
struct Tween {
    Property prop = 0;
    std::variant<std::pair<float, float>, std::pair<Color, Color>> range;
    float  elapsed = 0.f;
    float  duration = 0.15f;
    Easing easing = Easing::EaseOut;
};

// ---------------------------------------------------------------------------
// INode — internal node implementation
// ---------------------------------------------------------------------------
//
// LayoutProps has been removed.  Every layout-affecting parameter is now read
// from INode::attr via the Prop enum (Prop::Width, Prop::PaddingTop, …).
// Accessor methods below provide typed wrappers with sensible defaults:
//
//   layout_width()    → Prop::Width,  default nan_f() (auto)
//   layout_height()   → Prop::Height, default nan_f() (auto)
//   layout_padding()  → Prop::Padding{Top,Right,Bottom,Left}, default 0
//   layout_margin()   → Prop::Margin{Top,Right,Bottom,Left},  default 0
//   layout_gap()      → Prop::Gap,    default 0
//   layout_share()    → Prop::Share,  default 1
//   layout_direction()→ Prop::Direction, default Direction::Column
//   layout_align()    → Prop::AlignItems,  default Align::Stretch
//   layout_justify()  → Prop::JustifyItems, default Justify::Start
//
// The public Node fluent setters (width(), padding(), …) call attr.set() with
// the appropriate Prop key so the parser and programmatic API share one path.
//
// layout_dirty lives inside Attributes and is set automatically by attr.set()
// whenever a box-model or layout-behavior property changes.  measure() checks
// and clears it; if the flag is clear and the available space matches the
// cached values from the previous frame the entire measure sub-tree is skipped.
//
class INode {
public:
    virtual ~INode() = default;

    virtual void apply_notifier(Property p) {}
    void apply(Property p, AttribValue val);

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

    // -- Layout cache (used by measure's early-exit guard) ----------------
    //
    // Stores the avail_w / avail_h values passed to the previous successful
    // measure call.  If attr.layout_dirty is false, no active tweens are
    // running, and the available space matches, measure() returns immediately.
    //
    float cached_avail_w_ = -1.f;
    float cached_avail_h_ = -1.f;

    // -- Animation ---------------------------------------------------------
    //
    // transitions_: keyed by Prop uint32_t; set once at tree-build time via
    //   the "transition = <prop> <duration> <easing>" document directive.
    //
    // tweens_: live in-flight interpolations.  Created by animate_prop();
    //   advanced by tick_tweens() each frame; erased when complete.
    //
    // has_active_tweens_: true while tweens_ is non-empty; lets measure()'s
    //   early-exit guard skip the dirty-flag fast path when animating.
    //
    std::unordered_map<Property, TransitionSpec> transitions_;
    std::vector<Tween>                           tweens_;
    bool                                         has_active_tweens_ = false;

    /**
     * @brief Set or animate prop toward target.
     *
     * If transitions_ contains a spec for prop, a Tween is created (or the
     * existing one is restarted from the current live value) and the prop will
     * be interpolated each frame until complete.  Otherwise the value is
     * applied immediately via attr.set().
     *
     * An AnimateDescriptor in target overrides the node-level TransitionSpec
     * for this one call.
     */
    void animate_prop(Property p, const AttribValue& target);

    /**
     * @brief Advance all active tweens by dt seconds; recurse into children.
     * Called once per frame by Core::process_default before the layout pass.
     */
    void tick_tweens(float dt);

    // ── Layout accessors — read Prop values from attr ───────────────────

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
};

template class Impl<INode>;

// ---------------------------------------------------------------------------
// Fire helpers — inline definitions
// ---------------------------------------------------------------------------

inline void fire_with_context(
    INode* impl, WeakNode handle,
    Event type, float local_x, float local_y,
    MouseButton btn, Modifiers mods,
    float scroll_dx = 0.0f, float scroll_dy = 0.0f) {
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
