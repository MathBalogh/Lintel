#include "iimagenode.h"

namespace lintel {

// ---------------------------------------------------------------------------
// Bitmap management
// ---------------------------------------------------------------------------

void IImageNode::ensure_bitmap() {
    if (bitmap_ || image_path_.empty()) return;
    bitmap_ = CORE.canvas.load_bitmap(image_path_);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void IImageNode::measure(float avail_w, float avail_h) {
    // Early exit (same as every other node)
    if (!attr.layout_dirty &&
        avail_w == cached_avail_w_ &&
        avail_h == cached_avail_h_)
        return;

    ensure_bitmap();

    // Base auto logic (we override for intrinsic size)
    rect.w = is_auto(layout_width()) ? 0.f : layout_width();
    rect.h = is_auto(layout_height()) ? 0.f : layout_height();

    if (bitmap_) {
        const D2D1_SIZE_F sz = bitmap_->GetSize();
        const Edges pad = layout_padding();

        if (is_auto(layout_width()))
            rect.w = sz.width + pad.horizontal();
        if (is_auto(layout_height()))
            rect.h = sz.height + pad.vertical();
    }

    rect.w = std::max(0.f, rect.w);
    rect.h = std::max(0.f, rect.h);

    cached_avail_w_ = avail_w;
    cached_avail_h_ = avail_h;
    attr.layout_dirty = false;
}

void IImageNode::arrange(float slot_x, float slot_y) {
    const Edges margin = layout_margin();
    rect.x = slot_x + margin.left;
    rect.y = slot_y + margin.top;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void IImageNode::draw(Node& /*self*/, Canvas& canvas) {
    draw_default(canvas);                 // background + border (if any)

    ensure_bitmap();
    if (!bitmap_) return;

    const Rect dest{ content_x(), content_y(), inner_w(), inner_h() };
    canvas.draw_image(bitmap_.Get(), dest);
}

// ---------------------------------------------------------------------------
// ImageNode public API
// ---------------------------------------------------------------------------

ImageNode::ImageNode(): Node(nullptr) {
    impl_allocate<IImageNode>();
    handle<IImageNode>()->attr.set(property::Share, 0.f); // shrink-wrap by default
}

ImageNode::ImageNode(std::string_view path): Node(nullptr) {
    impl_allocate<IImageNode>();
    IImageNode& n = *handle<IImageNode>();
    n.image_path_ = path;
    n.attr.set(property::Share, 0.f);
}

ImageNode& ImageNode::source(std::string_view path) {
    IImageNode& n = *handle<IImageNode>();
    if (n.image_path_ != path) {
        n.image_path_ = std::string(path);
        n.invalidate_bitmap();
        n.attr.layout_dirty = true;   // force re-measure (size may change)
    }
    return *this;
}

} // namespace lintel
