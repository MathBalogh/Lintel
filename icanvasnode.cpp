#include "icanvasnode.h"

#define SELF (handle<ICanvasNode>())

namespace lintel {

Geometry::Geometry() {
    allocate();
}
Geometry::~Geometry() = default;

Geometry::Geometry(Geometry&&) noexcept = default;
Geometry& Geometry::operator=(Geometry&&) noexcept = default;

IGeometryBuilder::IGeometryBuilder() {
    if (GPU.d2d_factory) {
        GPU.d2d_factory->CreatePathGeometry(&m_geometry);
        if (m_geometry)
            m_geometry->Open(&m_sink);
    }
}
IGeometryBuilder::~IGeometryBuilder() {
    if (m_sink) {
        m_sink->Close();
        m_sink.Reset();
    }
}
void IGeometryBuilder::begin_figure(const D2D1_POINT_2F& startPoint, D2D1_FIGURE_BEGIN figureBegin) {
    if (m_sink) m_sink->BeginFigure(startPoint, figureBegin);
}
void IGeometryBuilder::add_line(const D2D1_POINT_2F& point) {
    if (m_sink) m_sink->AddLine(point);
}
void IGeometryBuilder::add_bezier(const D2D1_BEZIER_SEGMENT& bezier) {
    if (m_sink) m_sink->AddBezier(bezier);
}
void IGeometryBuilder::add_quadratic_bezier(const D2D1_POINT_2F& point1, const D2D1_POINT_2F& point2) {
    if (m_sink) m_sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(point1, point2));
}
void IGeometryBuilder::add_arc(const D2D1_ARC_SEGMENT& arc) {
    if (m_sink) m_sink->AddArc(arc);
}
void IGeometryBuilder::end_figure(D2D1_FIGURE_END figureEnd) {
    if (m_sink) m_sink->EndFigure(figureEnd);
}
ComPtr<ID2D1PathGeometry> IGeometryBuilder::finalize() {
    if (m_sink) {
        m_sink->Close();
        m_sink.Reset();
    }
    return m_geometry;
}

GeometryBuilder::GeometryBuilder() { allocate(); }
GeometryBuilder::~GeometryBuilder() = default;
void GeometryBuilder::begin_figure(float x, float y) { iptr_->begin_figure({ x, y }); }
void GeometryBuilder::add_line(float x, float y) { iptr_->add_line({ x, y }); }
void GeometryBuilder::add_bezier(float x0, float y0, float x1, float y1, float x2, float y2) { iptr_->add_bezier({ x0, y0, x1, y1, x2, y2 }); }
void GeometryBuilder::add_quadratic_bezier(float x0, float y0, float x1, float y1) {
    iptr_->add_quadratic_bezier({ x0, y0 }, { x1, y1 });
}
void GeometryBuilder::add_arc(float x, float y, float size_x, float size_y, float angle, bool sweep_clockwise) {
    D2D1_ARC_SEGMENT seg{};
    seg.arcSize = D2D1_ARC_SIZE_SMALL;
    seg.size = { size_x, size_y };
    seg.point = { x, y };
    seg.rotationAngle = angle;
    seg.sweepDirection = sweep_clockwise ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
    iptr_->add_arc(seg);
}
Geometry GeometryBuilder::end_figure() {
    iptr_->end_figure();
    Geometry geo;
    geo->geometry = iptr_->finalize();
    return geo;
}

CanvasNode::CanvasNode(): Node(nullptr) {
    allocate<ICanvasNode>();
}

void CanvasNode::translate(float x, float y) {
    CANVAS.translate(x, y);
}
void CanvasNode::scale(float x, float y) {
    CANVAS.scale(x, y);
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
void CanvasNode::triangle(float x0, float y0, float x1, float y1, float x2, float y2) {
    CANVAS.fill_triangle(x0, y0, x1, y1, x2, y2, SELF->current_fill);
}
void CanvasNode::geometry(Geometry& geo) {
    CANVAS.fill_geometry(geo->geometry, SELF->current_fill);
}

void ICanvasNode::draw(Node& self, Canvas& canvas) {
    draw_default(canvas);

    if (on_draw) {
        ComPtr<ID2D1CommandList> cmd;
        GPU.d2d_context->CreateCommandList(&cmd);

        ComPtr<ID2D1Image> target;
        GPU.d2d_context->GetTarget(&target);
        GPU.d2d_context->SetTarget(cmd.Get());

        canvas.push_transform(D2D1::IdentityMatrix());
        on_draw(self.as<CanvasNode>());
        canvas.pop_transform();

        cmd->Close();

        GPU.d2d_context->SetTarget(target.Get());

        canvas.push_clip(rect);
        canvas.push_transform(D2D1::Matrix3x2F::Translation(rect.x, rect.y));
        GPU.d2d_context->DrawImage(cmd.Get(), D2D1::Point2F(0.0f, 0.0f), D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
        canvas.pop_transform();
        canvas.pop_clip();
    }
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
