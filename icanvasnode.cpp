#include "icanvasnode.h"

#define SELF (handle<ICanvasNode>())

namespace lintel {

CanvasNode::CanvasNode(): Node(nullptr) {
    allocate<ICanvasNode>();
}

void CanvasNode::fill(Color c) {
    SELF->current_fill = c;
}
void CanvasNode::rect(float x, float y, float w, float h, float r) {
    CANVAS.fill_rect(Rect(x, y, w, h), SELF->current_fill, r);
}
void CanvasNode::ellipse(float cx, float cy, float rx, float ry) {
    CANVAS.fill_ellipse(cx, cy, rx, ry, SELF->current_fill);
}
void CanvasNode::line(float x0, float y0, float x1, float y1, float w) {
    CANVAS.draw_line(x0, y0, x1, y1, SELF->current_fill, w);
}

} // namespace lintel
