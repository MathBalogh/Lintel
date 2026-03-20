#pragma once

#include "core.h"   // also pulls in canvas.h via the include chain

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
// Drawing contract
// ----------------
// Every draw() override receives a Canvas& that owns the full D2D / DWrite
// API surface for the current frame.  Nodes must not call GPU.d2d_context or
// GPU.dwrite_factory from inside draw(); all rendering goes through Canvas.
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
    //
    // canvas is the frame-scoped drawing surface owned by Core.  It is passed
    // down the tree so every node draws through the same abstraction.
    //

    /**
     * @brief Draw background fill and border using the node's attr values.
     * Called at the top of draw() overrides before child content.
     */
    void draw_default(Canvas& canvas);

    /**
     * @brief Full draw pass: draw_default then recurse into children.
     * Overrides must call draw_default(canvas) and forward canvas to children.
     */
    virtual void draw(Node& self, Canvas& canvas);

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
