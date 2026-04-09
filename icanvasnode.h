#pragma once
#include "inode.h"

namespace lintel {

class IGeometry {
public:
    ComPtr<ID2D1PathGeometry> geometry;
};
template class Owner<IGeometry>;

class IGeometryBuilder {
public:
    IGeometryBuilder();
    ~IGeometryBuilder();

    void begin_figure(const D2D1_POINT_2F& startPoint, D2D1_FIGURE_BEGIN figureBegin = D2D1_FIGURE_BEGIN_FILLED);
    void add_line(const D2D1_POINT_2F& point);
    void add_bezier(const D2D1_BEZIER_SEGMENT& bezier);
    void add_quadratic_bezier(const D2D1_POINT_2F& point1, const D2D1_POINT_2F& point2);
    void add_arc(const D2D1_ARC_SEGMENT& arc);
    void end_figure(D2D1_FIGURE_END figureEnd = D2D1_FIGURE_END_CLOSED);

    // Must be called once; returns the finished geometry (sink is closed).
    ComPtr<ID2D1PathGeometry> finalize();

private:
    ComPtr<ID2D1PathGeometry> m_geometry;
    ComPtr<ID2D1GeometrySink> m_sink;
};
template class Owner<IGeometryBuilder>;

class ICanvasNode : public INode {
public:
	std::function<void(CanvasNode&)> on_draw = nullptr;
	Color current_fill = Color(0, 0, 0);

	void draw(Node& self, Canvas& canvas) override;
};

} // namespace lintel
