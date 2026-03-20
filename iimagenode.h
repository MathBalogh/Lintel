#pragma once
#include "inode.h"

namespace lintel {

/**
 * IImageNode
 * @brief Internal implementation for lintel::ImageNode.
 *
 * Exactly mirrors the TextNode pattern:
 *   • path stored locally (not in attr)
 *   • bitmap cached via CORE.canvas.load_bitmap()
 *   • auto-size = intrinsic bitmap size + padding (like text metrics)
 *   • stretch-to-inner-rect when width/height are fixed
 */
class IImageNode : public INode {
public:
    std::string          image_path_;
    ComPtr<ID2D1Bitmap>  bitmap_;

    void invalidate_bitmap() { bitmap_.Reset(); }

    /**
     * Lazy-load (or reload after path change).  Called from measure() and draw().
     */
    void ensure_bitmap();

    // -----------------------------------------------------------------------
    void measure(float avail_w, float avail_h) override;
    void arrange(float slot_x, float slot_y)   override;
    void draw(Node& handle, Canvas& canvas)    override;
};

} // namespace lintel
