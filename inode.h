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
// AxisLayout - axis-agnostic view into a child's geometry
//
// Wraps an INode* together with a Column/Row flag so that arrange_axis() can
// operate on either axis without duplicating logic.  All members are thin
// forwarding helpers; no state is owned here.
// ---------------------------------------------------------------------------

struct AxisLayout {
    // True  → Column (main axis = Y, cross axis = X).
    // False → Row    (main axis = X, cross axis = Y).
    bool is_col;

    // --- child accessors -------------------------------------------------

    UIValue child_main(INode* ci) const;   // height (col) or width (row)
    UIValue child_cross(INode* ci) const;   // width  (col) or height (row)

    float& child_main_rect(INode* ci) const; // ci->rect.h or ci->rect.w
    float& child_cross_rect(INode* ci) const; // ci->rect.w or ci->rect.h

    // margin contribution along main / cross axis
    float child_main_margin(INode* ci) const; // cm.vertical()  or cm.horizontal()
    float child_cross_margin(INode* ci) const; // cm.horizontal() or cm.vertical()

    // --- parent inner-size accessors ------------------------------------

    float inner_main(INode* parent) const; // inner_h (col) or inner_w (row)
    float inner_cross(INode* parent) const; // inner_w (col) or inner_h (row)

    // Starting position along main / cross axis inside the content area
    float content_main(INode* parent) const; // content_y (col) or content_x (row)
    float content_cross(INode* parent) const; // content_x (col) or content_y (row)
};

// ---------------------------------------------------------------------------
// INode - internal node implementation
// ---------------------------------------------------------------------------

class INode {
public:
    // Destructor: clears document-level weak refs that point at this node.
    // Safe to call even when doc_ is null (node was never in a document).
    virtual ~INode() {
        if (doc_) doc_->clear_node(this);
    }

    virtual void apply_callback(Key key) {}
    void apply(Key key, Property val) {
        props.set(key, val);
        apply_callback(key);
    }

    // -- Document back-pointer -------------------------------------------
    //
    // Null until the node is adopted into a Document's tree (via
    // Document::bind_root() or Node::push()).  Cleared when the subtree is
    // detached via Node::remove().  Never owned — the Document outlives all
    // of its nodes.

    Document* doc_ = nullptr;

    // -- Handler storage -------------------------------------------------

    struct HandlerEntry {
        Event              type;
        Node::EventHandler fn;
    };
    using HandlerList = std::vector<HandlerEntry>;

    HandlerList handlers_;

    // -- Core state -------------------------------------------------------

    Rect              rect = {};
    WeakNode          parent;
    std::vector<Node> children;
    Properties        props;

    bool focusable_flag = false;
    bool draggable_flag = false;
    bool mouse_inside = false;

    // Mark layout_dirty as true for all ancestors of this node
    void propagate_dirty();

    // -- Layout cache -----------------------------------------------------

    float cached_avail_w_ = -1.f;
    float cached_avail_h_ = -1.f;

    // ── Layout accessors -------------------------------------------------

    UIValue   layout_width()     const;
    UIValue   layout_height()    const;

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

    bool can_skip_measure(float avail_w, float avail_h) const;

    // Computes rect.w / rect.h from layout_width/height and the available space.
    void measure_self_size(float avail_w, float avail_h);

    // Calls measure() on every child with the correct available space for
    // the current direction.  Must be called after measure_self_size().
    void measure_children();

    // When this node is Auto in its main axis, sums fixed-size children to
    // derive the node's main-axis size.  No-op if the main axis is not Auto.
    void compute_auto_main(float avail_w, float avail_h);

    void default_measure(float avail_w, float avail_h);
    void default_arrange(float slot_x, float slot_y);

    virtual void measure(float avail_w, float avail_h);
    virtual void arrange(float slot_x, float slot_y);
    void         layout(float slot_x, float slot_y, float avail_w, float avail_h);

    // -- Draw pipeline ---------------------------------------------------

    void         draw_default(Canvas& canvas);
    virtual void draw(Node& self, Canvas& canvas);

    // -- Geometry helpers ------------------------------------------------

    float inner_w()   const;
    float inner_h()   const;
    float content_x() const;
    float content_y() const;

    // -- Tree utilities --------------------------------------------------

    Node* find_hit(Node& self, float sx, float sy);

    // update_hover is called by Document::dispatch_mouse_move / leave.
    // It fires MouseEnter / MouseLeave events on nodes whose mouse_inside
    // state changes.  Uses doc_ to reach InputState.
    void update_hover(WeakNode self, float sx, float sy);

    // bubble_up re-fires an already-dispatched event up the parent chain,
    // adjusting mouse coordinates at each ancestor.
    // Uses doc_ (inherited from the originating node).
    void bubble_up(Event type);

    // collect_focusable gathers all focusable nodes in document order.
    static void collect_focusable(INode* node, std::vector<INode*>& out);

private:
    // ---------------------------------------------------------------------------
    // Axis-unified arrangement
    //
    // arrange_axis() implements the full column/row arrangement algorithm in
    // terms of AxisLayout, eliminating the near-duplicate arrange_column() and
    // arrange_row() functions.  It handles both the "shares" path and the
    // "justify" path.
    //
    // Cross-axis placement is delegated to resolve_cross(), which works for
    // both axes via the AxisLayout wrapper.
    // ---------------------------------------------------------------------------

    void  arrange_axis(AxisLayout ax);

    // Returns the slot coordinate along the cross axis for `ci`, stretching
    // ci's cross-size if Align::Stretch is active.
    float resolve_cross(AxisLayout ax, INode* ci);
};
extern template class Impl<INode>;

// ---------------------------------------------------------------------------
// Fire helpers - inline free functions
//
// These write to doc.input so every handler callback sees coherent state,
// then invoke the handlers on the target node.
// ---------------------------------------------------------------------------

inline void fire_with_context(
    Document& doc,
    INode* impl, WeakNode handle,
    Event type, float local_x, float local_y,
    MouseButton btn, Modifiers mods,
    float scroll_dx, float scroll_dy) {
    InputState& inp = doc.input;
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
    Document& doc,
    INode* impl, WeakNode handle,
    Event type, int vkey, bool repeat, Modifiers mods) {
    InputState& inp = doc.input;
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
    Document& doc,
    INode* impl, WeakNode handle,
    wchar_t ch, Modifiers mods) {
    InputState& inp = doc.input;
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
