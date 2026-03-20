#include "inode.h"

#include <algorithm>

namespace lintel {

// ===========================================================================
// inode_invoke_handlers
// ===========================================================================
//
// Single connection point between the fire_*_context helpers (core.h) and the
// EventRegistry.  core.h forward-declares this; we define it here because this
// translation unit is the only one that needs both core.h and inode.h together.
//
void inode_invoke_handlers(INode* impl, Node& handle, Event type) {
    CORE.registry.fire(impl, handle, type);
}

// ===========================================================================
// INode — handler registration
// ===========================================================================

void INode::add_handler(Event type, Node::EventHandler fn) {
    CORE.registry.add(this, type, std::move(fn));
}

void INode::remove_handlers(Event type) {
    CORE.registry.remove(this, type);
}

void INode::fire(Node& self, Event type) {
    CORE.registry.fire(this, self, type);
}

// ===========================================================================
// INode — geometry helpers
// ===========================================================================

float INode::inner_w()   const { return std::max(0.f, rect.w - lp.padding.horizontal()); }
float INode::inner_h()   const { return std::max(0.f, rect.h - lp.padding.vertical()); }
float INode::content_x() const { return rect.x + lp.padding.left; }
float INode::content_y() const { return rect.y + lp.padding.top; }

// ===========================================================================
// INode — tree utilities
// ===========================================================================

Node* INode::find_hit(Node& self, float sx, float sy) {
    if (sx < rect.x || sx > rect.x + rect.w ||
        sy < rect.y || sy > rect.y + rect.h)
        return nullptr;

    // Reverse iteration so the last-pushed (visually topmost) sibling wins
    // when bounding rects overlap.
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (Node* hit = (*it).handle<INode>()->find_hit(*it, sx, sy))
            return hit;
    }
    return &self;
}

void INode::update_hover(Node& self, float sx, float sy) {
    const bool now_inside = (sx >= rect.x && sx <= rect.x + rect.w &&
                             sy >= rect.y && sy <= rect.y + rect.h);
    const Modifiers mods = CORE.input.modifiers;

    if (now_inside && !mouse_inside) {
        mouse_inside = true;
        fire_with_context(this, self, Event::MouseEnter,
                          sx - content_x(), sy - content_y(),
                          MouseButton::None, mods);
    }
    else if (!now_inside && mouse_inside) {
        mouse_inside = false;
        fire_with_context(this, self, Event::MouseLeave,
                          sx - content_x(), sy - content_y(),
                          MouseButton::None, mods);
    }

    // Always recurse: a child may need a Leave even when the parent is still hit.
    for (Node& child : children)
        child.handle<INode>()->update_hover(child, sx, sy);
}

void INode::bubble_up(INode* from_impl, Event type) {
    // Walk up the parent chain.  At each level re-localise mouse_x / mouse_y
    // relative to that ancestor's content area, then fire the event.
    // Deliberately copies the weak pointer so the advance step is safe.
    WeakNode cursor = from_impl->parent;
    while (cursor) {
        INode* ancestor = cursor.handle<INode>();
        if (!ancestor) break;

        // Re-localise before firing so the handler sees coordinates relative
        // to this ancestor.
        CORE.input.mouse_x = CORE.input.mouse_screen_x - ancestor->content_x();
        CORE.input.mouse_y = CORE.input.mouse_screen_y - ancestor->content_y();

        // cursor.as<Node>() reinterprets the WeakImpl as a Node handle in
        // place (both types share an identical single-pointer layout).
        CORE.registry.fire(ancestor, cursor.as<Node>(), type);

        // Advance before the next iteration.
        cursor = ancestor->parent;
    }
}

// ===========================================================================
// INode — focus utilities
// ===========================================================================

void INode::set_focus(WeakNode target) {
    FocusState& fs = CORE.focus;

    // Nothing to do when focus is already on target.
    if (fs.focused == target) return;

    // Blur the outgoing node.
    if (fs.focused) {
        INode* prev = fs.focused.handle<INode>();
        if (prev) {
            fire_with_context(prev, fs.focused.as<Node>(), Event::Blur,
                              0.f, 0.f, MouseButton::None, {});
        }
    }

    fs.focused = target;

    // Focus the incoming node.
    if (fs.focused) {
        INode* next = fs.focused.handle<INode>();
        if (next) {
            fire_with_context(next, fs.focused.as<Node>(), Event::Focus,
                              0.f, 0.f, MouseButton::None, {});
        }
    }
}

void INode::collect_focusable(INode* node, std::vector<INode*>& out) {
    if (!node) return;
    if (node->focusable_flag) out.push_back(node);
    for (Node& child : node->children)
        collect_focusable(child.handle<INode>(), out);
}

void INode::focus_next(Node& tree_root) {
    std::vector<INode*> focusable;
    collect_focusable(tree_root.handle<INode>(), focusable);
    if (focusable.empty()) return;

    INode* current = CORE.focus.focused.handle<INode>();
    if (!current) {
        set_focus(WeakNode(static_cast<void*>(focusable.front())));
        return;
    }

    for (size_t i = 0; i < focusable.size(); ++i) {
        if (focusable[i] == current) {
            set_focus(WeakNode(static_cast<void*>(
                focusable[(i + 1) % focusable.size()])));
            return;
        }
    }
    set_focus(WeakNode(static_cast<void*>(focusable.front())));
}

// ===========================================================================
// INode — layout
// ===========================================================================
//
// Flex-style layout with two axes: a main axis (the layout direction) and a
// cross axis (perpendicular).  Children with explicit sizes are "fixed"; the
// remaining space on the main axis is distributed proportionally among
// "share" children by their lp.share weights.
//

void INode::measure(float avail_w, float avail_h) {
    // If an explicit size is set use it; otherwise claim the full available
    // space (the parent will have already subtracted the child's margin).
    rect.w = is_auto(lp.width) ? std::max(0.f, avail_w - lp.margin.horizontal()) : lp.width;
    rect.h = is_auto(lp.height) ? std::max(0.f, avail_h - lp.margin.vertical()) : lp.height;

    if (children.empty()) return;

    const bool  is_col = (lp.direction == Direction::Column);
    const float cross_avail = is_col ? inner_w() : inner_h();
    const float main_avail = is_col ? inner_h() : inner_w();

    // Give each child a preliminary size so the parent's own auto dimension
    // can be resolved below.
    for (Node& child : children) {
        INode* ci = child.handle<INode>();
        const bool fixed_main = is_col ? !is_auto(ci->lp.height) : !is_auto(ci->lp.width);
        if (is_col)
            ci->measure(cross_avail, fixed_main ? main_avail : 0.f);
        else
            ci->measure(fixed_main ? main_avail : 0.f, cross_avail);
    }

    // When our own main-axis size is auto, shrink-wrap to the sum of the
    // fixed children (share children contribute 0 at this stage).
    if (is_auto(is_col ? lp.height : lp.width)) {
        const int   n = static_cast<int>(children.size());
        float total_gap = n > 1 ? lp.gap * (n - 1) : 0.f;
        float total_main = (is_col ? lp.padding.vertical() : lp.padding.horizontal())
            + total_gap;

        for (Node& child : children) {
            INode* ci = child.handle<INode>();
            const bool fixed_main = is_col ? !is_auto(ci->lp.height) : !is_auto(ci->lp.width);
            if (fixed_main) {
                total_main += is_col
                    ? ci->rect.h + ci->lp.margin.vertical()
                    : ci->rect.w + ci->lp.margin.horizontal();
            }
        }

        if (is_col) rect.h = total_main;
        else        rect.w = total_main;
    }
}

void INode::arrange(float slot_x, float slot_y) {
    rect.x = slot_x + lp.margin.left;
    rect.y = slot_y + lp.margin.top;

    if (!children.empty()) {
        if (lp.direction == Direction::Column) arrange_column();
        else                                   arrange_row();
    }
}

void INode::layout(float slot_x, float slot_y, float avail_w, float avail_h) {
    measure(avail_w, avail_h);
    arrange(slot_x, slot_y);
}

// ---------------------------------------------------------------------------
// arrange_column — main axis = Y
// ---------------------------------------------------------------------------

void INode::arrange_column() {
    const float iw = inner_w();
    const float ih = inner_h();
    const int   n = static_cast<int>(children.size());

    // -- Pass 1: separate fixed-height children from share children --------
    float fixed_main = lp.gap * std::max(0, n - 1);
    float total_shares = 0.f;

    for (Node& child : children) {
        INode* ci = child.handle<INode>();
        if (!is_auto(ci->lp.height) || ci->lp.share == 0.f)
            fixed_main += ci->rect.h + ci->lp.margin.vertical();
        else
            total_shares += ci->lp.share;
    }

    const float share_pool = std::max(0.f, ih - fixed_main);

    // -- Pass 2: place children -------------------------------------------

    if (total_shares > 0.f) {
        // Share children absorb all remaining space; justify is ignored.
        float cursor = content_y();
        for (Node& child : children) {
            INode* ci = child.handle<INode>();
            if (is_auto(ci->lp.height) && ci->lp.share > 0.f) {
                ci->rect.h = std::max(0.f,
                                      (ci->lp.share / total_shares) * share_pool
                                      - ci->lp.margin.vertical());
            }
            ci->arrange(resolve_cross_x(ci, iw), cursor);
            cursor += ci->rect.h + ci->lp.margin.vertical() + lp.gap;
        }
    }
    else {
        // All children have fixed heights; apply justify_items.
        const float free_h = ih - fixed_main;
        float cursor = content_y();
        float extra_gap = 0.f;

        switch (lp.justify_items) {
            case Justify::Center:
                cursor += free_h * 0.5f;
                break;
            case Justify::End:
                cursor += free_h;
                break;
            case Justify::SpaceBetween:
                if (n > 1) extra_gap = free_h / static_cast<float>(n - 1);
                break;
            case Justify::SpaceAround:
                extra_gap = free_h / static_cast<float>(n);
                cursor += extra_gap * 0.5f;
                break;
            default: // Start
                break;
        }

        for (Node& child : children) {
            INode* ci = child.handle<INode>();
            ci->arrange(resolve_cross_x(ci, iw), cursor);
            cursor += ci->rect.h + ci->lp.margin.vertical() + lp.gap + extra_gap;
        }
    }
}

// ---------------------------------------------------------------------------
// arrange_row — main axis = X
// ---------------------------------------------------------------------------

void INode::arrange_row() {
    const float iw = inner_w();
    const float ih = inner_h();
    const int   n = static_cast<int>(children.size());

    // -- Pass 1: fixed vs share children -----------------------------------
    float fixed_main = lp.gap * std::max(0, n - 1);
    float total_shares = 0.f;

    for (Node& child : children) {
        INode* ci = child.handle<INode>();
        if (!is_auto(ci->lp.width) || ci->lp.share == 0.f)
            fixed_main += ci->rect.w + ci->lp.margin.horizontal();
        else
            total_shares += ci->lp.share;
    }

    const float share_pool = std::max(0.f, iw - fixed_main);

    // -- Pass 2: place children -------------------------------------------

    if (total_shares > 0.f) {
        float cursor = content_x();
        for (Node& child : children) {
            INode* ci = child.handle<INode>();
            if (is_auto(ci->lp.width) && ci->lp.share > 0.f) {
                ci->rect.w = std::max(0.f,
                                      (ci->lp.share / total_shares) * share_pool
                                      - ci->lp.margin.horizontal());
            }
            ci->arrange(cursor, resolve_cross_y(ci, ih));
            cursor += ci->rect.w + ci->lp.margin.horizontal() + lp.gap;
        }
    }
    else {
        const float free_w = iw - fixed_main;
        float cursor = content_x();
        float extra_gap = 0.f;

        switch (lp.justify_items) {
            case Justify::Center:
                cursor += free_w * 0.5f;
                break;
            case Justify::End:
                cursor += free_w;
                break;
            case Justify::SpaceBetween:
                if (n > 1) extra_gap = free_w / static_cast<float>(n - 1);
                break;
            case Justify::SpaceAround:
                extra_gap = free_w / static_cast<float>(n);
                cursor += extra_gap * 0.5f;
                break;
            default: // Start
                break;
        }

        for (Node& child : children) {
            INode* ci = child.handle<INode>();
            ci->arrange(cursor, resolve_cross_y(ci, ih));
            cursor += ci->rect.w + ci->lp.margin.horizontal() + lp.gap + extra_gap;
        }
    }
}

// ---------------------------------------------------------------------------
// Cross-axis position resolvers
// ---------------------------------------------------------------------------
//
// Both helpers return the slot_x / slot_y to pass to the child's arrange().
// For Stretch they also write the child's cross dimension directly into
// ci->rect so the child sees the correct size without a second measure call.
//

float INode::resolve_cross_x(INode* ci, float iw) {
    const float avail = iw - ci->lp.margin.horizontal();
    switch (lp.align_items) {
        case Align::Center:
            return content_x() + ci->lp.margin.left + (avail - ci->rect.w) * 0.5f;
        case Align::End:
            return content_x() + iw - ci->lp.margin.right - ci->rect.w;
        case Align::Stretch:
            ci->rect.w = std::max(0.f, avail);
            return content_x() + ci->lp.margin.left;
        default: // Start
            return content_x() + ci->lp.margin.left;
    }
}

float INode::resolve_cross_y(INode* ci, float ih) {
    const float avail = ih - ci->lp.margin.vertical();
    switch (lp.align_items) {
        case Align::Center:
            return content_y() + ci->lp.margin.top + (avail - ci->rect.h) * 0.5f;
        case Align::End:
            return content_y() + ih - ci->lp.margin.bottom - ci->rect.h;
        case Align::Stretch:
            ci->rect.h = std::max(0.f, avail);
            return content_y() + ci->lp.margin.top;
        default: // Start
            return content_y() + ci->lp.margin.top;
    }
}

// ===========================================================================
// INode — draw
// ===========================================================================

void INode::draw(Node& self, Canvas& canvas) {
    draw_default(canvas);
    for (Node& child : children)
        child.handle<INode>()->draw(child, canvas);
}

void INode::draw_default(Canvas& canvas) {
    const float radius = attr.get_or<float>(attribs::corner_radius, 0.f);

    // Background fill
    if (const Color* bg = attr.get<Color>(attribs::background_color))
        canvas.fill_rect(rect, *bg, radius);

    // Border stroke — only drawn when both color and weight are set.
    if (attr.has(attribs::border_color) && attr.has(attribs::border_weight)) {
        const Color& bc = *attr.get<Color>(attribs::border_color);
        const float  bw = attr.get_or<float>(attribs::border_weight, 1.f);
        canvas.stroke_rect(rect, bc, bw, radius);
    }
}

// ===========================================================================
// Node — constructors / destructor / move
// ===========================================================================

Node::Node() { impl_allocate(); }
Node::Node(std::nullptr_t) {} // leaves iptr_ = nullptr

Node::~Node() {
    if (iptr_) {
        // Remove all references in global state before the INode is deleted.
        CORE.registry.clear_node(iptr_);
        CORE.clear_node(iptr_);
    }
    // Impl::~Impl() calls impl_release() -> delete iptr_.
}

Node::Node(Node&& other) noexcept
    : Impl<INode>(std::move(other)) {}

Node& Node::operator=(Node&& other) noexcept {
    if (this != &other) {
        if (iptr_) {
            CORE.registry.clear_node(iptr_);
            CORE.clear_node(iptr_);
        }
        Impl<INode>::operator=(std::move(other));
    }
    return *this;
}

// ===========================================================================
// Node — tree mutation
// ===========================================================================

Node& Node::push() {
    Node& child = iptr_->children.emplace_back();
    child.handle<INode>()->parent = *this; // WeakNode = Impl<INode>& (copies iptr_)
    return child;
}

Node& Node::push(Node&& incoming) {
    if (!incoming) return *this;

    // Detach from the old parent if needed.
    if (incoming.handle<INode>()->parent) {
        WeakNode old_parent = incoming.handle<INode>()->parent;
        old_parent.as<Node>().remove(incoming);
    }

    iptr_->children.push_back(std::move(incoming));
    iptr_->children.back().handle<INode>()->parent = *this;
    return iptr_->children.back();
}

Node Node::remove(Node& child) {
    if (!child) return Node(nullptr);

    auto& vec = iptr_->children;
    for (size_t i = 0; i < vec.size(); ++i) {
        if (vec[i].handle<INode>() == child.handle<INode>()) {
            Node owner = std::move(vec[i]);
            vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(i));
            if (INode* oi = owner.handle<INode>())
                oi->parent.reset();
            return owner;
        }
    }
    return Node(nullptr); // child not found in this node
}

Node* Node::child(size_t index) {
    if (index < iptr_->children.size())
        return &iptr_->children[index];
    return nullptr;
}

// ===========================================================================
// Node — style / layout setters (fluent)
// ===========================================================================

Attributes& Node::attr() { return iptr_->attr; }
Node& Node::attr(const Attributes& s) { iptr_->attr = s; return *this; }

Node& Node::share(float s) { iptr_->lp.share = s; return *this; }
Node& Node::width(float w) { iptr_->lp.width = w; return *this; }
Node& Node::height(float h) { iptr_->lp.height = h; return *this; }
Node& Node::padding(Edges e) { iptr_->lp.padding = e; return *this; }
Node& Node::margin(Edges e) { iptr_->lp.margin = e; return *this; }
Node& Node::row() { iptr_->lp.direction = Direction::Row;    return *this; }
Node& Node::column() { iptr_->lp.direction = Direction::Column; return *this; }
Node& Node::align(Align a) { iptr_->lp.align_items = a; return *this; }
Node& Node::justify(Justify j) { iptr_->lp.justify_items = j; return *this; }
Node& Node::gap(float g) { iptr_->lp.gap = g; return *this; }
Node& Node::focusable(bool f) { iptr_->focusable_flag = f; return *this; }
Node& Node::draggable(bool d) { iptr_->draggable_flag = d; return *this; }

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

float* Node::margin_bottom() { return &iptr_->lp.margin.bottom; }
Rect Node::rect() const { return iptr_->rect; }

float Node::mouse_x()        const { return CORE.input.mouse_screen_x - iptr_->content_x(); }
float Node::mouse_y()        const { return CORE.input.mouse_screen_y - iptr_->content_y(); }

} // namespace lintel
