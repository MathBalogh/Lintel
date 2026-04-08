#include "inode.h"

#include <algorithm>
#include <cmath>

namespace lintel {

template class Owner<INode>;

// ===========================================================================
// Layout accessors
// ===========================================================================

UIValue INode::layout_width() const {
    return props.get(Key::Width);
}
UIValue INode::layout_height() const {
    return props.get(Key::Height);
}
float INode::layout_gap()   const {
    return props.get(Key::Gap);
}
float INode::layout_share() const {
    return props.get(Key::Share);
}

Edges INode::layout_padding() const {
    return Edges(
        props.get(Key::PaddingTop),
        props.get(Key::PaddingRight),
        props.get(Key::PaddingBottom),
        props.get(Key::PaddingLeft));
}
Edges INode::layout_margin() const {
    return Edges(
        props.get(Key::MarginTop),
        props.get(Key::MarginRight),
        props.get(Key::MarginBottom),
        props.get(Key::MarginLeft));
}

Direction INode::layout_direction() const {
    if (auto p = props.get(Key::Direction); p.is_enum())
        return Direction(p.get_enum());
    return DirectionCol;
}
Align INode::layout_align() const {
    if (auto p = props.get(Key::AlignItems); p.is_enum())
        return Align(p.get_enum());
    return AlignStretch;
}
Justify INode::layout_justify() const {
    if (auto p = props.get(Key::JustifyItems); p.is_enum())
        return Justify(p.get_enum());
    return JustifyStart;
}

float INode::inner_w()   const { return std::max(0.f, rect.w - layout_padding().horizontal()); }
float INode::inner_h()   const { return std::max(0.f, rect.h - layout_padding().vertical()); }
float INode::content_x() const { return rect.x + layout_padding().left; }
float INode::content_y() const { return rect.y + layout_padding().top; }

// ===========================================================================
// Tree utilities
// ===========================================================================

Node* INode::find_hit(Node& self, float sx, float sy) {
    if (!is_displayed()) return nullptr;
    if (sx < rect.x || sx > rect.x + rect.w ||
        sy < rect.y || sy > rect.y + rect.h)
        return nullptr;

    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (Node* hit = (*it).handle<INode>()->find_hit(*it, sx, sy))
            return hit;
    }
    return &self;
}
void INode::update_hover(NodeView self, float sx, float sy) {
    if (!is_displayed()) {
        // Ensure we clear stale hover state if this node was hidden mid-hover.
        if (mouse_inside && doc_) {
            mouse_inside = false;
            fire_with_context(*doc_, this, self, Event::MouseLeave,
                              sx - content_x(), sy - content_y(),
                              MouseButton::None, doc_->input.modifiers, 0.f, 0.f);
        }
        return;
    }
    const bool now_inside = (sx >= rect.x && sx <= rect.x + rect.w &&
                             sy >= rect.y && sy <= rect.y + rect.h);

    if (!doc_) return;
    const Modifiers mods = doc_->input.modifiers;

    if (now_inside && !mouse_inside) {
        mouse_inside = true;
        fire_with_context(*doc_, this, self, Event::MouseEnter,
                          sx - content_x(), sy - content_y(),
                          MouseButton::None, mods, 0.f, 0.f);
    }
    else if (!now_inside && mouse_inside) {
        mouse_inside = false;
        fire_with_context(*doc_, this, self, Event::MouseLeave,
                          sx - content_x(), sy - content_y(),
                          MouseButton::None, mods, 0.f, 0.f);
    }

    for (Node& child : children)
        child.handle<INode>()->update_hover(NodeView(child), sx, sy);
}

// ---------------------------------------------------------------------------
// bubble_up
//
// Re-fires an already-dispatched event up the ancestor chain, adjusting
// mouse_x/y at each step.  Uses doc_ inherited from the originating node.
// ---------------------------------------------------------------------------

void INode::bubble_up(Event type) {
    if (!doc_) return;

    NodeView cursor = parent;
    while (cursor) {
        INode* ancestor = cursor.handle<INode>();
        if (!ancestor) break;

        doc_->input.mouse_x = doc_->input.mouse_screen_x - ancestor->content_x();
        doc_->input.mouse_y = doc_->input.mouse_screen_y - ancestor->content_y();

        ancestor->invoke_handlers(cursor, type);
        cursor = ancestor->parent;
    }
}

// ===========================================================================
// Focus utilities
// ===========================================================================

/*static*/ void INode::collect_focusable(INode* node, std::vector<INode*>& out) {
    if (!node || !node->is_displayed()) return;
    if (node->focusable_flag) out.push_back(node);
    for (Node& child : node->children)
        collect_focusable(child.handle<INode>(), out);
}

// ===========================================================================
// Layout — skip guard
// ===========================================================================

bool INode::can_skip_measure(float avail_w, float avail_h) const {
    return props.is_clean() &&
        avail_w == cached_avail_w_ &&
        avail_h == cached_avail_h_;
}

// ===========================================================================
// Layout — measure sub-steps
//
// default_measure() is decomposed into three named sub-steps so that
// subclasses can override only the piece they need instead of copying the
// entire function.
// ===========================================================================

// ---------------------------------------------------------------------------
// measure_self_size
//
// Resolves this node's own rect.w / rect.h from layout_width/height and the
// available space passed in from the parent.  Does not touch children.
// ---------------------------------------------------------------------------

void INode::measure_self_size(float avail_w, float avail_h) {
    const Edges margin = layout_margin();

    {
        UIValue wv = layout_width();
        if (wv.is_pixels())  rect.w = wv.value;
        else if (wv.is_percent()) rect.w = (avail_w > 0.f) ? avail_w * wv.value : 0.f;
        else                      rect.w = std::max(0.f, avail_w - margin.horizontal());
    }
    {
        UIValue hv = layout_height();
        if (hv.is_pixels())  rect.h = hv.value;
        else if (hv.is_percent()) rect.h = (avail_h > 0.f) ? avail_h * hv.value : 0.f;
        else                      rect.h = std::max(0.f, avail_h - margin.vertical());
    }
}

// ---------------------------------------------------------------------------
// measure_children
//
// Calls measure() on every child with the correct available space for the
// current direction.  Must be called after measure_self_size() so that
// inner_w() / inner_h() are valid.
// ---------------------------------------------------------------------------

void INode::measure_children() {
    const bool  is_col = (layout_direction() == DirectionCol);
    const float cross_avail = is_col ? inner_w() : inner_h();
    const float main_avail = is_col ? inner_h() : inner_w();

    for (Node& child : children) {
        INode* ci = child.handle<INode>();
        if (!ci->is_displayed()) continue;

        const UIValue child_main = is_col ? ci->layout_height() : ci->layout_width();
        // Percent is resolvable from the parent's inner size just like pixels.
        const bool known_main = child_main.is_pixels() || child_main.is_percent();

        if (is_col) ci->measure(cross_avail, known_main ? main_avail : 0.f);
        else        ci->measure(known_main ? main_avail : 0.f, cross_avail);
    }
}

// ---------------------------------------------------------------------------
// compute_auto_main
//
// When this node's main axis is Auto, its size is derived from the sum of
// fixed-size children (padding + gap included).  Auto/Percent children are
// flexible and do not contribute.
// ---------------------------------------------------------------------------

void INode::compute_auto_main(float avail_w, float avail_h) {
    const bool    is_col = (layout_direction() == DirectionCol );
    const UIValue parent_main = is_col ? layout_height() : layout_width();

    if (!parent_main.is_auto()) return;

    // If the parent was given real available space it already filled it in
    // measure_self_size; shrink-wrapping would wrongly override that.
    const float avail_main = is_col ? avail_h : avail_w;
    if (avail_main > 0.f) return;

    const Edges padding = layout_padding();
    const float gap_val = layout_gap();

    // Count only visible children so gap math matches what arrange_axis sees.
    const std::vector<INode*> vis = visible_children();
    const int n = static_cast<int>(vis.size());

    float total = (is_col ? padding.vertical() : padding.horizontal())
        + (n > 1 ? gap_val * (n - 1) : 0.f);

    for (INode* ci : vis) {
        const UIValue child_main = is_col ? ci->layout_height() : ci->layout_width();
        const Edges   cm = ci->layout_margin();

        if (child_main.is_pixels() || child_main.is_percent())
            total += is_col ? ci->rect.h + cm.vertical()
            : ci->rect.w + cm.horizontal();
    }

    if (is_col) rect.h = total;
    else        rect.w = total;
}

// ---------------------------------------------------------------------------
// default_measure — orchestrates the three sub-steps
// ---------------------------------------------------------------------------

void INode::default_measure(float avail_w, float avail_h) {
    if (can_skip_measure(avail_w, avail_h)) return;

    measure_self_size(avail_w, avail_h);

    if (!children.empty()) {
        measure_children();
        compute_auto_main(avail_w, avail_h);
    }

    cached_avail_w_ = avail_w;
    cached_avail_h_ = avail_h;
    props.make_clean();
}

// ===========================================================================
// Layout — arrange
// ===========================================================================

void INode::default_arrange(float slot_x, float slot_y) {
    const Edges margin = layout_margin();
    rect.x = slot_x + margin.left;
    rect.y = slot_y + margin.top;

    if (!children.empty())
        arrange_axis(AxisLayout{ layout_direction() == DirectionCol });
}

void INode::measure(float avail_w, float avail_h) { default_measure(avail_w, avail_h); }
void INode::arrange(float slot_x, float slot_y) { default_arrange(slot_x, slot_y); }

void INode::layout(float slot_x, float slot_y, float avail_w, float avail_h) {
    measure(avail_w, avail_h);
    arrange(slot_x, slot_y);
}

// ===========================================================================
// AxisLayout — axis-agnostic child accessors
// ===========================================================================

UIValue AxisLayout::child_main(INode* ci) const { return is_col ? ci->layout_height() : ci->layout_width(); }
UIValue AxisLayout::child_cross(INode* ci) const { return is_col ? ci->layout_width() : ci->layout_height(); }

float& AxisLayout::child_main_rect(INode* ci) const { return is_col ? ci->rect.h : ci->rect.w; }
float& AxisLayout::child_cross_rect(INode* ci) const { return is_col ? ci->rect.w : ci->rect.h; }

float AxisLayout::child_main_margin(INode* ci) const {
    const Edges cm = ci->layout_margin();
    return is_col ? cm.vertical() : cm.horizontal();
}
float AxisLayout::child_cross_margin(INode* ci) const {
    const Edges cm = ci->layout_margin();
    return is_col ? cm.horizontal() : cm.vertical();
}

float AxisLayout::inner_main(INode* p) const { return is_col ? p->inner_h() : p->inner_w(); }
float AxisLayout::inner_cross(INode* p) const { return is_col ? p->inner_w() : p->inner_h(); }

float AxisLayout::content_main(INode* p) const { return is_col ? p->content_y() : p->content_x(); }
float AxisLayout::content_cross(INode* p) const { return is_col ? p->content_x() : p->content_y(); }

// ===========================================================================
// arrange_axis — unified column / row arrangement
//
// Replaces the old arrange_column() + arrange_row() pair.  The two were
// structurally identical; the only differences were which axis was "main" and
// which was "cross".  AxisLayout abstracts those away so the algorithm lives
// once.
//
// Two paths:
//   • Shares path  — at least one child has a non-zero flex share.
//                    The share pool is divided proportionally; justify is
//                    irrelevant.
//   • Justify path — no flex children.  Free space is distributed according
//                    to layout_justify() (Start, Center, End, SpaceBetween,
//                    SpaceAround).
// ===========================================================================

// ---------------------------------------------------------------------------
// justify_cursor_and_gap
//
// Given the amount of free space along the main axis and the justify mode,
// returns the initial cursor offset and any extra per-item gap.  Pure helper,
// no side effects.
// ---------------------------------------------------------------------------

static std::pair<float, float> justify_cursor_and_gap(Justify justify, float free_space, int n) {
    float offset = 0.f;
    float extra_gap = 0.f;

    switch (justify) {
        case JustifyCenter:
            offset = free_space * 0.5f;
            break;
        case JustifyEnd:
            offset = free_space;
            break;
        case JustifySpaceBetween:
            if (n > 1) extra_gap = free_space / static_cast<float>(n - 1);
            break;
        case JustifySpaceAround:
            extra_gap = free_space / static_cast<float>(n);
            offset = extra_gap * 0.5f;
            break;
        default: // Start
            break;
    }

    return { offset, extra_gap };
}


// Helper accessor so resolve_cross can read the is_col flag via a free func
// without duplicating the logic.  Not exposed in the header.
static bool is_col(const AxisLayout& ax) { return ax.is_col; }

// ---------------------------------------------------------------------------
// resolve_cross
//
// Returns the slot coordinate along the cross axis for child `ci`, applying
// Align::Stretch to ci's cross-size when appropriate.
// ---------------------------------------------------------------------------

float INode::resolve_cross(AxisLayout ax, INode* ci) {
    const Edges  margin = ci->layout_margin();
    const float  cross_m = ax.child_cross_margin(ci);
    const float  isize = ax.inner_cross(this);
    const float  avail = isize - cross_m;

    if (layout_align() == AlignStretch && ax.child_cross(ci).is_auto())
        ax.child_cross_rect(ci) = std::max(0.f, avail);

    const float  csize = ax.child_cross_rect(ci);
    const float  origin = ax.content_cross(this);

    switch (layout_align()) {
        case AlignCenter:
            return origin + (avail - csize) * 0.5f;
        case AlignEnd:
            // Inset from the far edge by the far-side margin.
            return origin + isize - (is_col(ax) ? margin.right : margin.bottom) - csize;
        case AlignStretch:
            return origin;
        default: // Start
            return origin;
    }
}

void INode::arrange_axis(AxisLayout ax) {
    const float im = ax.inner_main(this);
    const float gap_val = layout_gap();

    // Exclude hidden children from all layout math.
    const std::vector<INode*> vis = visible_children();
    const int n = static_cast<int>(vis.size());

    // ----- First pass: measure fixed space and accumulate shares ----------

    float fixed_main = gap_val * std::max(0, n - 1);
    float total_shares = 0.f;

    for (INode* ci : vis) {
        const UIValue mv = ax.child_main(ci);
        const float   mm = ax.child_main_margin(ci);

        if (mv.is_pixels())  fixed_main += ax.child_main_rect(ci) + mm;
        else if (mv.is_percent()) fixed_main += (mv.value * im) + mm;
        else { // Auto
            if (ci->layout_share() == 0.f) fixed_main += ax.child_main_rect(ci) + mm;
            else                           total_shares += ci->layout_share();
        }
    }

    const float share_pool = std::max(0.f, im - fixed_main);

    // ----- Second pass: position children --------------------------------

    float cursor = ax.content_main(this);

    if (total_shares > 0.f) {
        // Shares path: distribute the pool proportionally, no justify.
        for (INode* ci : vis) {
            const UIValue mv = ax.child_main(ci);
            const float   mm = ax.child_main_margin(ci);

            if (mv.is_percent())
                ax.child_main_rect(ci) = std::max(0.f, mv.value * im);
            else if (mv.is_auto() && ci->layout_share() > 0.f)
                ax.child_main_rect(ci) = std::max(0.f,
                                                  (ci->layout_share() / total_shares) * share_pool - mm);
            // Pixels and Auto-with-share=0 keep their measured size.

            const float cross_slot = resolve_cross(ax, ci);
            if (ax.is_col) ci->arrange(cross_slot, cursor);
            else           ci->arrange(cursor, cross_slot);

            cursor += ax.child_main_rect(ci) + mm + gap_val;
        }
    }
    else {
        // Justify path: distribute free space per layout_justify().
        const float free_space = im - fixed_main;
        const auto [offset, extra_gap] =
            justify_cursor_and_gap(layout_justify(), free_space, n);
        cursor += offset;

        for (INode* ci : vis) {
            const UIValue mv = ax.child_main(ci);
            const float   mm = ax.child_main_margin(ci);

            // Resolve any remaining Percent sizes (Pixels already set).
            if (mv.is_percent())
                ax.child_main_rect(ci) = std::max(0.f, mv.value * im);

            const float cross_slot = resolve_cross(ax, ci);
            if (ax.is_col) ci->arrange(cross_slot, cursor);
            else           ci->arrange(cursor, cross_slot);

            cursor += ax.child_main_rect(ci) + mm + gap_val + extra_gap;
        }
    }
}

// ===========================================================================
// Draw
// ===========================================================================

void INode::draw(Node& self, Canvas& canvas) {
    if (!is_displayed()) return;
    draw_default(canvas);
    for (Node& child : children)
        child.handle<INode>()->draw(child, canvas);
}
void INode::draw_default(Canvas& canvas) {
    const float radius = props.get(Key::BorderRadius);

    if (const Color* bg = props.get_color(Key::BackgroundColor))
        canvas.fill_rect(rect, *bg, radius);

    if (props.has(Key::BorderColor) && props.has(Key::BorderWeight)) {
        const Color& bc = *props.get_color(Key::BorderColor);
        const float  bw = props.get(Key::BorderWeight);
        canvas.stroke_rect(rect, bc, bw, radius);
    }
}

// ===========================================================================
// Node — constructors / destructor / move
// ===========================================================================
//
// The destructor no longer calls CORE.clear_node() directly.  That cleanup
// now happens inside INode::~INode() via the doc_ back-pointer, so the Node
// shell has no special teardown work to do.

Node::Node() { allocate(); }
Node::Node(std::nullptr_t) {}
Node::~Node() = default;

Node::Node(Node&& other) noexcept
    : Owner<INode>(std::move(other)) {}

Node& Node::operator=(Node&& other) noexcept {
    if (this != &other) {
        iptr_->doc_->stamp_document(other.handle(), iptr_->doc_);
        Owner<INode>::operator=(std::move(other));
    }
    return *this;
}

// ===========================================================================
// Node — tree mutation
// ===========================================================================
//
// push() stamps doc_ on newly-adopted nodes so they immediately participate
// in document cleanup and event dispatch.
//
// remove() clears doc_ on the departing subtree so detached nodes never
// fire into a document that no longer owns them.

Node& Node::push() {
    Node& child = iptr_->children.emplace_back();
    INode* ci = child.handle<INode>();
    ci->parent = *this;
    ci->doc_ = iptr_->doc_;  // inherit document
    return child;
}

Node& Node::push(Node& incoming) {
    if (!incoming) return *this;

    iptr_->propagate_dirty();

    INode* inc = incoming.handle<INode>();
    if (inc->parent) {
        NodeView old_parent = inc->parent;
        old_parent->remove(incoming);
    }

    iptr_->children.push_back(std::move(incoming));
    INode* ci = iptr_->children.back().handle<INode>();
    ci->parent = *this;

    Document::stamp_document(ci, iptr_->doc_);
    return iptr_->children.back();
}

Node Node::remove(Node& child) {
    if (!child) return Node(nullptr);

    iptr_->propagate_dirty();

    auto& vec = iptr_->children;
    for (size_t i = 0; i < vec.size(); ++i) {
        if (vec[i].handle<INode>() == child.handle<INode>()) {
            Node owner = std::move(vec[i]);
            vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(i));
            if (INode* oi = owner.handle<INode>()) {
                oi->parent.reset();
                Document::stamp_document(oi, nullptr);
            }
            return owner;
        }
    }

    return Node(nullptr);
}

Node* Node::child(size_t index) {
    if (index < iptr_->children.size())
        return &iptr_->children[index];
    return nullptr;
}

// ===========================================================================
// Node — style / layout setters (fluent)
// ===========================================================================

Node& Node::set(Key key, Property value) {
    iptr_->apply(key, value);
    return *this;
}

Node& Node::padding(const Edges& e) {
    iptr_->props.set(Key::PaddingTop, e.top);
    iptr_->props.set(Key::PaddingRight, e.right);
    iptr_->props.set(Key::PaddingBottom, e.bottom);
    iptr_->props.set(Key::PaddingLeft, e.left);
    return *this;
}
Node& Node::margin(const Edges& e) {
    iptr_->props.set(Key::MarginTop, e.top);
    iptr_->props.set(Key::MarginRight, e.right);
    iptr_->props.set(Key::MarginBottom, e.bottom);
    iptr_->props.set(Key::MarginLeft, e.left);
    return *this;
}
Node& Node::width(UIValue v) {
    iptr_->props.set(Key::Width, v);
    return *this;
}
Node& Node::height(UIValue v) {
    iptr_->props.set(Key::Height, v);
    return *this;
}

Node& Node::focusable(bool f) { iptr_->focusable_flag = f; return *this; }
Node& Node::draggable(bool d) { iptr_->draggable_flag = d; return *this; }
Node& Node::display(bool visible) { iptr_->set_display(visible); return *this; }

// ===========================================================================
// Node — children
// ===========================================================================

Node& Node::clear_children() {
    // Destroying each child Node fires INode::~INode → doc_->clear_node(),
    // which nulls out focused / hovered / pressed refs in the document.
    iptr_->children.clear();
    iptr_->props.make_dirty();
    return *this;
}

void Node::dirty() { iptr_->props.make_dirty(); }

// ===========================================================================
// Node — event binding
// ===========================================================================

Node& Node::on(Event type, EventHandler fn) {
    iptr_->add_handler(type, std::move(fn));
    return *this;
}

void Node::clear_on_of(Event type) {
    iptr_->remove_handlers(type);
}

// ===========================================================================
// Node — query
// ===========================================================================

Rect  Node::rect()    const { return iptr_->rect; }
float Node::mouse_x() const { return iptr_->doc_->input.mouse_screen_x - iptr_->rect.x; }
float Node::mouse_y() const { return iptr_->doc_->input.mouse_screen_y - iptr_->rect.y; }

// ===========================================================================
// propagate_dirty
// ===========================================================================


// Used by layout, draw, and input traversals so hidden-child skips are
// expressed once here rather than scattered across every loop.
//
// Returns a small vector of non-owning INode* pointers.  Allocation is
// avoided for the common all-visible case via the overload that accepts a
// pre-allocated buffer, but the simple form is fine for callers that already
// allocate per-frame.
std::vector<INode*> INode::visible_children() {
    std::vector<INode*> out;
    out.reserve(children.size());
    for (Node& child : children) {
        INode* ci = child.handle<INode>();
        if (ci && ci->is_displayed()) out.push_back(ci);
    }
    return out;
}

void INode::self_dirty() {
    props.make_dirty();
}
void INode::propagate_dirty() {
    NodeView node = NodeView(this);
    while (node) {
        node->handle()->props.make_dirty();
        node = node->handle()->parent;
    }
}

} // namespace lintel
