#include "inode.h"

#include <algorithm>
#include <cmath>

namespace lintel {

// ===========================================================================
// Layout accessors — read from attr with typed defaults
// ===========================================================================
//
// Enum-valued property::s (Direction, Align, Justify) are stored as float (integer
// cast) in the attribute map — the same convention used by TextAlign — because
// AttribValue has no enum slot.  The accessors cast back on read.
//

float INode::layout_width()  const {
    return attr.get_or<float>(property::Width, nan_f());
}
float INode::layout_height() const {
    return attr.get_or<float>(property::Height, nan_f());
}
float INode::layout_gap()   const {
    return attr.get_or<float>(property::Gap, 0.f);
}
float INode::layout_share() const {
    return attr.get_or<float>(property::Share, 1.f);
}

Edges INode::layout_padding() const {
    return Edges(
        attr.get_or<float>(property::PaddingTop, 0.f),
        attr.get_or<float>(property::PaddingRight, 0.f),
        attr.get_or<float>(property::PaddingBottom, 0.f),
        attr.get_or<float>(property::PaddingLeft, 0.f));
}

Edges INode::layout_margin() const {
    return Edges(
        attr.get_or<float>(property::MarginTop, 0.f),
        attr.get_or<float>(property::MarginRight, 0.f),
        attr.get_or<float>(property::MarginBottom, 0.f),
        attr.get_or<float>(property::MarginLeft, 0.f));
}

Direction INode::layout_direction() const {
    const float raw = attr.get_or<float>(
        property::Direction,
        static_cast<float>(static_cast<int>(Direction::Column)));
    return static_cast<Direction>(static_cast<int>(raw));
}

Align INode::layout_align() const {
    const float raw = attr.get_or<float>(
        property::AlignItems,
        static_cast<float>(static_cast<int>(Align::Stretch)));
    return static_cast<Align>(static_cast<int>(raw));
}

Justify INode::layout_justify() const {
    const float raw = attr.get_or<float>(
        property::JustifyItems,
        static_cast<float>(static_cast<int>(Justify::Start)));
    return static_cast<Justify>(static_cast<int>(raw));
}

// ===========================================================================
// INode — geometry helpers
// ===========================================================================

float INode::inner_w()   const { return std::max(0.f, rect.w - layout_padding().horizontal()); }
float INode::inner_h()   const { return std::max(0.f, rect.h - layout_padding().vertical()); }
float INode::content_x() const { return rect.x + layout_padding().left; }
float INode::content_y() const { return rect.y + layout_padding().top; }

// ===========================================================================
// INode — tree utilities
// ===========================================================================

Node* INode::find_hit(Node& self, float sx, float sy) {
    if (sx < rect.x || sx > rect.x + rect.w ||
        sy < rect.y || sy > rect.y + rect.h)
        return nullptr;

    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (Node* hit = (*it).handle<INode>()->find_hit(*it, sx, sy))
            return hit;
    }
    return &self;
}

void INode::update_hover(WeakNode self, float sx, float sy) {
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

    for (Node& child : children)
        child.handle<INode>()->update_hover(WeakNode(child), sx, sy);
}

void INode::bubble_up(INode* from_impl, Event type) {
    WeakNode cursor = from_impl->parent;
    while (cursor) {
        INode* ancestor = cursor.handle<INode>();
        if (!ancestor) break;

        CORE.input.mouse_x = CORE.input.mouse_screen_x - ancestor->content_x();
        CORE.input.mouse_y = CORE.input.mouse_screen_y - ancestor->content_y();

        ancestor->invoke_handlers(cursor, type);
        cursor = ancestor->parent;
    }
}

// ===========================================================================
// INode — focus utilities
// ===========================================================================

void INode::set_focus(WeakNode target) {
    FocusState& fs = CORE.focus;
    if (fs.focused == target) return;

    if (fs.focused) {
        INode* prev = fs.focused.handle<INode>();
        if (prev)
            fire_with_context(prev, fs.focused, Event::Blur,
                              0.f, 0.f, MouseButton::None, {});
    }

    fs.focused = target;

    if (fs.focused) {
        INode* next = fs.focused.handle<INode>();
        if (next)
            fire_with_context(next, fs.focused, Event::Focus,
                              0.f, 0.f, MouseButton::None, {});
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

void INode::measure(float avail_w, float avail_h) {
    // ── Early exit: nothing changed since the last layout pass ────────────
    //
    // Skip the subtree when:
    //   1. No box-model / layout-behavior property:: has been written.
    //   2. No tween is currently animating a layout property:: on this subtree.
    //   3. The parent is offering exactly the same available space.
    //
    if (!attr.layout_dirty &&
        !has_active_tweens_ &&
        avail_w == cached_avail_w_ &&
        avail_h == cached_avail_h_)
        return;

    const Edges margin = layout_margin();

    rect.w = is_auto(layout_width())
        ? std::max(0.f, avail_w - margin.horizontal())
        : layout_width();
    rect.h = is_auto(layout_height())
        ? std::max(0.f, avail_h - margin.vertical())
        : layout_height();

    if (children.empty()) {
        cached_avail_w_ = avail_w;
        cached_avail_h_ = avail_h;
        attr.layout_dirty = false;
        return;
    }

    const bool  is_col = (layout_direction() == Direction::Column);
    const float cross_avail = is_col ? inner_w() : inner_h();
    const float main_avail = is_col ? inner_h() : inner_w();

    // Give each child a preliminary size so the parent's auto dimension can
    // be resolved in the shrink-wrap pass below.
    for (Node& child : children) {
        INode* ci = child.handle<INode>();
        const bool fixed_main = is_col ? !is_auto(ci->layout_height())
            : !is_auto(ci->layout_width());
        if (is_col)
            ci->measure(cross_avail, fixed_main ? main_avail : 0.f);
        else
            ci->measure(fixed_main ? main_avail : 0.f, cross_avail);
    }

    // Shrink-wrap our own auto main-axis dimension to the sum of fixed children.
    if (is_auto(is_col ? layout_height() : layout_width())) {
        const Edges padding = layout_padding();
        const int   n = static_cast<int>(children.size());
        const float gap_val = layout_gap();
        float total_gap = n > 1 ? gap_val * (n - 1) : 0.f;
        float total_main = (is_col ? padding.vertical() : padding.horizontal())
            + total_gap;

        for (Node& child : children) {
            INode* ci = child.handle<INode>();
            const bool fixed_main = is_col ? !is_auto(ci->layout_height())
                : !is_auto(ci->layout_width());
            if (fixed_main) {
                const Edges cm = ci->layout_margin();
                total_main += is_col
                    ? ci->rect.h + cm.vertical()
                    : ci->rect.w + cm.horizontal();
            }
        }

        if (is_col) rect.h = total_main;
        else        rect.w = total_main;
    }

    cached_avail_w_ = avail_w;
    cached_avail_h_ = avail_h;
    attr.layout_dirty = false;
}

void INode::arrange(float slot_x, float slot_y) {
    const Edges margin = layout_margin();
    rect.x = slot_x + margin.left;
    rect.y = slot_y + margin.top;

    if (!children.empty()) {
        if (layout_direction() == Direction::Column) arrange_column();
        else                                          arrange_row();
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
    const float gap_val = layout_gap();

    // -- Pass 1: fixed vs share children ----------------------------------
    float fixed_main = gap_val * std::max(0, n - 1);
    float total_shares = 0.f;

    for (Node& child : children) {
        INode* ci = child.handle<INode>();
        if (!is_auto(ci->layout_height()) || ci->layout_share() == 0.f)
            fixed_main += ci->rect.h + ci->layout_margin().vertical();
        else
            total_shares += ci->layout_share();
    }

    const float share_pool = std::max(0.f, ih - fixed_main);

    // -- Pass 2: place children -------------------------------------------

    if (total_shares > 0.f) {
        float cursor = content_y();
        for (Node& child : children) {
            INode* ci = child.handle<INode>();
            if (is_auto(ci->layout_height()) && ci->layout_share() > 0.f) {
                ci->rect.h = std::max(0.f,
                                      (ci->layout_share() / total_shares) * share_pool
                                      - ci->layout_margin().vertical());
            }
            ci->arrange(resolve_cross_x(ci, iw), cursor);
            cursor += ci->rect.h + ci->layout_margin().vertical() + gap_val;
        }
    }
    else {
        const float free_h = ih - fixed_main;
        float cursor = content_y();
        float extra_gap = 0.f;

        switch (layout_justify()) {
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
            default:
                break;
        }

        for (Node& child : children) {
            INode* ci = child.handle<INode>();
            ci->arrange(resolve_cross_x(ci, iw), cursor);
            cursor += ci->rect.h + ci->layout_margin().vertical() + gap_val + extra_gap;
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
    const float gap_val = layout_gap();

    // -- Pass 1: fixed vs share children ----------------------------------
    float fixed_main = gap_val * std::max(0, n - 1);
    float total_shares = 0.f;

    for (Node& child : children) {
        INode* ci = child.handle<INode>();
        if (!is_auto(ci->layout_width()) || ci->layout_share() == 0.f)
            fixed_main += ci->rect.w + ci->layout_margin().horizontal();
        else
            total_shares += ci->layout_share();
    }

    const float share_pool = std::max(0.f, iw - fixed_main);

    // -- Pass 2: place children -------------------------------------------

    if (total_shares > 0.f) {
        float cursor = content_x();
        for (Node& child : children) {
            INode* ci = child.handle<INode>();
            if (is_auto(ci->layout_width()) && ci->layout_share() > 0.f) {
                ci->rect.w = std::max(0.f,
                                      (ci->layout_share() / total_shares) * share_pool
                                      - ci->layout_margin().horizontal());
            }
            ci->arrange(cursor, resolve_cross_y(ci, ih));
            cursor += ci->rect.w + ci->layout_margin().horizontal() + gap_val;
        }
    }
    else {
        const float free_w = iw - fixed_main;
        float cursor = content_x();
        float extra_gap = 0.f;

        switch (layout_justify()) {
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
            default:
                break;
        }

        for (Node& child : children) {
            INode* ci = child.handle<INode>();
            ci->arrange(cursor, resolve_cross_y(ci, ih));
            cursor += ci->rect.w + ci->layout_margin().horizontal() + gap_val + extra_gap;
        }
    }
}

// ---------------------------------------------------------------------------
// Cross-axis position resolvers
// ---------------------------------------------------------------------------

float INode::resolve_cross_x(INode* ci, float iw) {
    const Edges margin = ci->layout_margin();
    const float avail = iw - margin.horizontal();
    switch (layout_align()) {
        case Align::Center:
            return content_x() + margin.left + (avail - ci->rect.w) * 0.5f;
        case Align::End:
            return content_x() + iw - margin.right - ci->rect.w;
        case Align::Stretch:
            ci->rect.w = std::max(0.f, avail);
            return content_x() + margin.left;
        default: // Start
            return content_x() + margin.left;
    }
}

float INode::resolve_cross_y(INode* ci, float ih) {
    const Edges margin = ci->layout_margin();
    const float avail = ih - margin.vertical();
    switch (layout_align()) {
        case Align::Center:
            return content_y() + margin.top + (avail - ci->rect.h) * 0.5f;
        case Align::End:
            return content_y() + ih - margin.bottom - ci->rect.h;
        case Align::Stretch:
            ci->rect.h = std::max(0.f, avail);
            return content_y() + margin.top;
        default: // Start
            return content_y() + margin.top;
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
    const float radius = attr.get_or<float>(property::CornerRadius, 0.f);

    if (const Color* bg = attr.get<Color>(property::BackgroundColor))
        canvas.fill_rect(rect, *bg, radius);

    if (attr.has(property::BorderColor) && attr.has(property::BorderWeight)) {
        const Color& bc = *attr.get<Color>(property::BorderColor);
        const float  bw = attr.get_or<float>(property::BorderWeight, 1.f);
        canvas.stroke_rect(rect, bc, bw, radius);
    }
}

// ===========================================================================
// Node — constructors / destructor / move
// ===========================================================================

Node::Node() { impl_allocate(); }
Node::Node(std::nullptr_t) {}

Node::~Node() {
    if (iptr_)
        CORE.clear_node(iptr_);
}

Node::Node(Node&& other) noexcept
    : Impl<INode>(std::move(other)) {}

Node& Node::operator=(Node&& other) noexcept {
    if (this != &other) {
        if (iptr_) CORE.clear_node(iptr_);
        Impl<INode>::operator=(std::move(other));
    }
    return *this;
}

// ===========================================================================
// Node — tree mutation
// ===========================================================================

Node& Node::push() {
    Node& child = iptr_->children.emplace_back();
    child.handle<INode>()->parent = *this;
    return child;
}

Node& Node::push(Node&& incoming) {
    if (!incoming) return *this;

    if (incoming.handle<INode>()->parent) {
        WeakNode old_parent = incoming.handle<INode>()->parent;
        old_parent->remove(incoming);
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
//
// Every setter writes to attr using the canonical property:: key.  This is the
// single path shared by the programmatic API, the parser, and event-delta
// handlers.  layout_dirty is set automatically by Attributes::set() for the
// box-model and layout-behavior property::erties.
//

Attributes& Node::attr() { return iptr_->attr; }
Node& Node::attr(const Attributes& s) { iptr_->attr = s; return *this; }

Node& Node::share(float s) { iptr_->attr.set(property::Share, s); return *this; }
Node& Node::width(float w) { iptr_->attr.set(property::Width, w); return *this; }
Node& Node::height(float h) { iptr_->attr.set(property::Height, h); return *this; }

Node& Node::padding(Edges e) {
    iptr_->attr.set(property::PaddingTop, e.top);
    iptr_->attr.set(property::PaddingRight, e.right);
    iptr_->attr.set(property::PaddingBottom, e.bottom);
    iptr_->attr.set(property::PaddingLeft, e.left);
    return *this;
}

Node& Node::margin(Edges e) {
    iptr_->attr.set(property::MarginTop, e.top);
    iptr_->attr.set(property::MarginRight, e.right);
    iptr_->attr.set(property::MarginBottom, e.bottom);
    iptr_->attr.set(property::MarginLeft, e.left);
    return *this;
}

Node& Node::row() {
    iptr_->attr.set(property::Direction,
                    static_cast<float>(static_cast<int>(Direction::Row)));
    return *this;
}

Node& Node::column() {
    iptr_->attr.set(property::Direction,
                    static_cast<float>(static_cast<int>(Direction::Column)));
    return *this;
}

Node& Node::align(Align a) {
    iptr_->attr.set(property::AlignItems,
                    static_cast<float>(static_cast<int>(a)));
    return *this;
}

Node& Node::justify(Justify j) {
    iptr_->attr.set(property::JustifyItems,
                    static_cast<float>(static_cast<int>(j)));
    return *this;
}

Node& Node::gap(float g) { iptr_->attr.set(property::Gap, g); return *this; }
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

float* Node::margin_bottom() {
    return iptr_->attr.float_ref(property::MarginBottom);
}

Rect  Node::rect()    const { return iptr_->rect; }
float Node::mouse_x() const { return CORE.input.mouse_screen_x - iptr_->content_x(); }
float Node::mouse_y() const { return CORE.input.mouse_screen_y - iptr_->content_y(); }

static float ease(float t, Easing e) noexcept {
    t = (t < 0.f) ? 0.f : (t > 1.f) ? 1.f : t;
    switch (e) {
        case Easing::EaseIn:    return t * t;
        case Easing::EaseOut:   return t * (2.f - t);
        case Easing::EaseInOut: return t < 0.5f ? 2.f * t * t
            : -1.f + (4.f - 2.f * t) * t;
        case Easing::Spring:
        {
            const float e6 = std::exp(-6.f * t);
            const float c8 = std::cos(8.f * t);
            return 1.f - e6 * c8;
        }
        default: return t; // Linear
    }
}

// ---------------------------------------------------------------------------
// animate_prop_impl — shared tween construction
// ---------------------------------------------------------------------------
//
// Finds or creates the TransitionSpec, cancels any existing tween for the
// property, and pushes a new Tween onto the node's list.
//
// The 'duration' parameter takes precedence over any node-level transition
// spec; pass -1.f to use the node-level spec (or snap if none).

void INode::animate_prop_impl(Property p, float target,
                              float duration, Easing easing) {
    // Cancel any existing tween for this property.
    tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(),
                  [p] (const Tween& tw) { return tw.prop == p; }), tweens_.end());

    Tween tw;
    tw.prop = p;
    tw.elapsed = 0.f;
    tw.duration = duration;
    tw.easing = easing;

    const float from = attr.get_or<float>(p, 0.f);
    tw.range = std::pair<float, float>{ from, target };

    tweens_.push_back(std::move(tw));
    has_active_tweens_ = true;
}

void INode::animate_prop_impl(Property p, Color target,
                              float duration, Easing easing) {
    tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(),
                  [p] (const Tween& tw) { return tw.prop == p; }), tweens_.end());

    Tween tw;
    tw.prop = p;
    tw.elapsed = 0.f;
    tw.duration = duration;
    tw.easing = easing;

    const Color from = attr.get_or<Color>(p, Color(0.f, 0.f, 0.f, 0.f));
    tw.range = std::pair<Color, Color>{ from, target };

    tweens_.push_back(std::move(tw));
    has_active_tweens_ = true;
}

// ---------------------------------------------------------------------------
// animate_prop — primary overload (PropValue target, node-level spec)
// ---------------------------------------------------------------------------

void INode::animate_prop(Property p, const PropValue& target) {
    const float* fp = std::get_if<float>(&target);
    const Color* cp = std::get_if<Color>(&target);

    // Non-interpolable types (bool, wstring): snap immediately.
    if (!fp && !cp) {
        attr.set(p, target);
        return;
    }

    // Look up node-level transition spec.
    auto spec_it = transitions_.find(p);
    if (spec_it == transitions_.end()) {
        // No spec: snap.
        attr.set(p, target);
        return;
    }

    const TransitionSpec& spec = spec_it->second;

    if (fp) animate_prop_impl(p, *fp, spec.duration, spec.easing);
    else    animate_prop_impl(p, *cp, spec.duration, spec.easing);
}

// ---------------------------------------------------------------------------
// animate_prop — per-call duration / easing overloads
// ---------------------------------------------------------------------------
//
// These replace the old AnimateDescriptor path.  Use them when the animation
// parameters are determined at the call site rather than being declared in the
// .lintel file's transition block.
//
// Example:
//   impl->animate_prop(property::BackgroundColor,
//                      Color(0.2f, 0.6f, 1.f, 1.f),
//                      0.3f, Easing::Spring);

void INode::animate_prop(Property p, float target,
                         float duration, Easing easing) {
    animate_prop_impl(p, target, duration, easing);
}

void INode::animate_prop(Property p, Color target,
                         float duration, Easing easing) {
    animate_prop_impl(p, target, duration, easing);
}

// ---------------------------------------------------------------------------
// tick_tweens — unchanged except PropValue is used in attr.set() calls
// ---------------------------------------------------------------------------

void INode::tick_tweens(float dt) {
    has_active_tweens_ = false;

    for (auto it = tweens_.begin(); it != tweens_.end(); ) {
        it->elapsed += dt;
        const float raw_t = (it->duration > 0.f)
            ? it->elapsed / it->duration : 1.f;
        const bool  done = (raw_t >= 1.f);
        const float t = ease(done ? 1.f : raw_t, it->easing);

        if (auto* ff = std::get_if<std::pair<float, float>>(&it->range)) {
            const float v = ff->first + (ff->second - ff->first) * t;
            attr.set(it->prop, v);   // PropValue constructed from float
        }
        else if (auto* cc = std::get_if<std::pair<Color, Color>>(&it->range)) {
            const Color& a = cc->first;
            const Color& b = cc->second;
            attr.set(it->prop, Color(
                a.r + (b.r - a.r) * t,
                a.g + (b.g - a.g) * t,
                a.b + (b.b - a.b) * t,
                a.a + (b.a - a.a) * t));
        }

        if (done)
            it = tweens_.erase(it);
        else {
            has_active_tweens_ = true;
            ++it;
        }
    }

    for (Node& child : children) {
        INode* ci = child.handle<INode>();
        ci->tick_tweens(dt);
        if (ci->has_active_tweens_) has_active_tweens_ = true;
    }
}

// ---------------------------------------------------------------------------
// apply — snap write, fires apply_notifier
// ---------------------------------------------------------------------------

void INode::apply(Property p, PropValue val) {
    attr.set(p, val);
    apply_notifier(p);
}

} // namespace lintel
