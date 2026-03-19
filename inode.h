#pragma once

#include "core.h"

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

inline D2D1_RECT_F to_d2df(const Rect& r) {
    return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h);
}
inline D2D1_ROUNDED_RECT to_d2d_rr(const Rect& r, float radius) {
    return D2D1::RoundedRect(to_d2df(r), radius, radius);
}
inline ComPtr<ID2D1SolidColorBrush> make_brush(Color c) {
    ComPtr<ID2D1SolidColorBrush> b;
    GPU.d2d_context->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, c.a), &b);
    return b;
}

// ---------------------------------------------------------------------------
// LayoutProps
// ---------------------------------------------------------------------------
//
// Stores every layout-affecting parameter for a single node.  All values are
// resolved per-frame; nothing is cached here.
//
struct LayoutProps {
    float width = nan_f(); // NaN -> participate in flex-share distribution.
    float height = nan_f(); // NaN -> participate in flex-share distribution.

    Edges     padding = {};
    Edges     margin = {};
    Direction direction = Direction::Column;
    Align     align_items = Align::Stretch;  // Cross-axis child alignment.
    Justify   justify_items = Justify::Start;  // Main-axis child distribution.
    float     gap = 0.f;             // Pixel gap between children.
    float     share = 1.f;             // Main-axis share weight.
};

// ---------------------------------------------------------------------------
// INode — internal node implementation
// ---------------------------------------------------------------------------
//
// All tree state lives here.  The public Node class is just an owning handle
// to an INode.  Input dispatch (mouse hit-testing, hover tracking, keyboard
// routing, drag/focus management) is driven from Core::process_message and
// calls the methods below.
//
class INode {
public:
    virtual ~INode() = default;

    LayoutProps          lp;
    Rect                 rect = {};     // Computed by the layout pass.
    WeakNode             parent;           // Non-owning reference to parent.
    std::vector<Node>    children;
    Attributes           attr;

    bool focusable_flag = false; // Participates in Tab-order focus cycling.
    bool draggable_flag = false; // Initiates drag sequences.
    bool mouse_inside = false; // Set while the cursor is within rect.

    // -- Layout pipeline -------------------------------------------------

    /** @brief Compute rect.w / rect.h given the available slot dimensions. */
    virtual void measure(float avail_w, float avail_h);

    /** @brief Assign rect.x / rect.y and recursively arrange children. */
    virtual void arrange(float slot_x, float slot_y);

    /** @brief Convenience: measure then arrange in one call. */
    void layout(float slot_x, float slot_y, float avail_w, float avail_h);

    // -- Draw pipeline ---------------------------------------------------

    void draw_default(); // Renders background and border using attr values.
    virtual void draw(Node& self);

    // -- Geometry helpers ------------------------------------------------

    float inner_w()   const; // Content-area width  (rect.w − h-padding).
    float inner_h()   const; // Content-area height (rect.h − v-padding).
    float content_x() const; // Absolute X of the content-area origin.
    float content_y() const; // Absolute Y of the content-area origin.

    // -- Handler registration --------------------------------------------

    /** @brief Append a typed handler to the global registry. */
    void add_handler(Event type, Node::EventHandler fn);

    /** @brief Remove all handlers for type on this node. */
    void remove_handlers(Event type);

    /** @brief Fire all matching handlers via the global registry. */
    void fire(Node& self, Event type);

    // -- Tree utilities --------------------------------------------------

    /**
     * @brief Depth-first hit test.
     * @return Pointer to the Node handle of the deepest descendant (or self)
     *         whose rect contains (sx, sy), or nullptr on a miss.
     */
    Node* find_hit(Node& self, float sx, float sy);

    /**
     * @brief Full-tree hover update.
     * Re-evaluates mouse_inside for self and all descendants, firing
     * MouseEnter / MouseLeave as appropriate.
     * @param sx / sy  Window client coordinates.
     */
    void update_hover(Node& self, float sx, float sy);

    /**
     * @brief Bubble an event up the parent chain.
     * Does not fire on from_impl itself — the caller has already done that.
     * Re-localises mouse_x / mouse_y at each ancestor level.
     */
    static void bubble_up(INode* from_impl, Event type);

    // -- Focus utilities -------------------------------------------------

    /**
     * @brief Transfer keyboard focus to target.
     * Fires Blur on the outgoing node and Focus on the incoming node.
     * Passing a null WeakNode clears focus without firing Focus.
     */
    static void set_focus(WeakNode target);

    /**
     * @brief Advance keyboard focus to the next focusable node in document
     *        order, wrapping around.  Used by the Tab key handler.
     */
    static void focus_next(Node& tree_root);

private:
    // Layout sub-routines.
    void arrange_column();
    void arrange_row();
    float resolve_cross_x(INode* child, float inner_width);
    float resolve_cross_y(INode* child, float inner_height);

    // Focus helpers.
    static void collect_focusable(INode* node, std::vector<INode*>& out);
};

template class Impl<INode>;

} // namespace lintel
