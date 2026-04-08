#include "iimagenode.h"

namespace lintel {

// ---------------------------------------------------------------------------
// Bitmap management
// ---------------------------------------------------------------------------

void IImageNode::ensure_bitmap() {
    if (bitmap_ || image_path_.empty()) return;
    bitmap_ = CANVAS.load_bitmap(image_path_);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void IImageNode::measure(float avail_w, float avail_h) {
    if (can_skip_measure(avail_w, avail_h)) return;

    ensure_bitmap();

    // Resolve rect.w / rect.h from layout_width/height and available space,
    // exactly as the base does — pixels, percent, or auto-fills available space.
    // For image nodes, auto is then overridden below with the intrinsic bitmap
    // size if a bitmap is loaded; otherwise the node collapses to zero.
    measure_self_size(avail_w, avail_h);

    if (bitmap_) {
        const D2D1_SIZE_F sz = bitmap_->GetSize();
        const Edges       pad = layout_padding();

        // Auto axes adopt the bitmap's intrinsic size (+ padding).
        // Fixed axes (pixels/percent) were already resolved by measure_self_size
        // and are left untouched — the image will stretch to fill them in draw().
        if (layout_width().is_auto())  rect.w = sz.width + pad.horizontal();
        if (layout_height().is_auto()) rect.h = sz.height + pad.vertical();
    }
    else {
        // No bitmap yet: collapse auto axes to zero so the node doesn't
        // claim space it can't fill.
        if (layout_width().is_auto())  rect.w = 0.f;
        if (layout_height().is_auto()) rect.h = 0.f;
    }

    rect.w = std::max(0.f, rect.w);
    rect.h = std::max(0.f, rect.h);

    cached_avail_w_ = avail_w;
    cached_avail_h_ = avail_h;
    props.make_clean();
}

void IImageNode::arrange(float slot_x, float slot_y) {
    const Edges margin = layout_margin();
    rect.x = slot_x + margin.left;
    rect.y = slot_y + margin.top;
}
void IImageNode::apply_callback(Key key) {
    if (key == get_key("path")) {
        if (const auto* prop = props.get_wstring(key)) {
            image_path_ = to_string(*prop);
            invalidate_bitmap();
            props.make_dirty();
        }
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void IImageNode::draw(Node& /*self*/, Canvas& canvas) {
    draw_default(canvas); // background + border (if any)

    ensure_bitmap();
    if (!bitmap_) return;

    const Rect dest{ content_x(), content_y(), inner_w(), inner_h() };
    canvas.draw_image(bitmap_.Get(), dest);
}

// ---------------------------------------------------------------------------
// ImageNode public API
// ---------------------------------------------------------------------------

ImageNode::ImageNode(): Node(nullptr) {
    allocate<IImageNode>();
    handle<IImageNode>()->props.set(Key::Share, 0.f); // shrink-wrap by default
}

ImageNode::ImageNode(std::string_view path): Node(nullptr) {
    allocate<IImageNode>();
    IImageNode& n = *handle<IImageNode>();
    n.image_path_ = path;
    n.props.set(Key::Share, 0.f);
}

ImageNode& ImageNode::source(std::string_view path) {
    IImageNode& n = *handle<IImageNode>();
    if (n.image_path_ != path) {
        n.image_path_ = std::string(path);
        n.invalidate_bitmap();
        n.props.make_dirty();
    }
    return *this;
}

} // namespace lintel
