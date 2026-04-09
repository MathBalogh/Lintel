#include "icanvasnode.h"
#include "igeometry.h"

#define SELF (handle<ICanvasNode>())

namespace lintel {

CanvasNode::CanvasNode(): Node(nullptr) {
    allocate<ICanvasNode>();
}

void CanvasNode::fill(Color c) {
    SELF->current_fill = c;
}
void CanvasNode::rect(float x, float y, float w, float h, float r) {
    SELF->transpose(x, y);
    CANVAS.fill_rect(Rect(x, y, w, h), SELF->current_fill, r);
}
void CanvasNode::ellipse(float cx, float cy, float rx, float ry) {
    SELF->transpose(cx, cy);
    CANVAS.fill_ellipse(cx, cy, rx, ry, SELF->current_fill);
}
void CanvasNode::line(float x0, float y0, float x1, float y1, float w) {
    SELF->transpose(x0, y0);
    SELF->transpose(x1, y1);
    CANVAS.draw_line(x0, y0, x1, y1, SELF->current_fill, w);
}
void CanvasNode::triangle(float x0, float y0, float x1, float y1, float x2, float y2) {
    SELF->transpose(x0, y0);
    SELF->transpose(x1, y1);
    SELF->transpose(x2, y2);
    CANVAS.fill_triangle(x0, y0, x1, y1, x2, y2, SELF->current_fill);
}

void CanvasNode::geometry(Geometry& geom) {
    CANVAS.fill_geometry(geom->path, SELF->current_fill);
}

void ICanvasNode::draw(Node& self, Canvas& canvas) {
    draw_default(canvas);

    canvas.push_clip(rect);
    if (on_draw)
        on_draw(self.as<CanvasNode>());
    canvas.pop_clip();
}
void CanvasNode::on_draw(std::function<void(CanvasNode&)> fn) {
    SELF->on_draw = fn;
}

float CanvasNode::get_width() const {
    return SELF->rect.w;
}
float CanvasNode::get_height() const {
    return SELF->rect.h;
}

} // namespace lintel
