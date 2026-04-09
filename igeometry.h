#pragma once

#include "canvas.h"

namespace lintel {

class IGeometry {
public:
	ComPtr<ID2D1PathGeometry> path;

	void from(const std::vector<float>& points) {
		GPU.d2d_factory->CreatePathGeometry(&path);
		ComPtr<ID2D1GeometrySink> sink;
		path->Open(&sink);

		D2D1_POINT_2F start = D2D1::Point2F(points[0], points[1]);
		sink->BeginFigure(start, D2D1_FIGURE_BEGIN_FILLED);

		for (size_t i = 2; i < points.size(); i += 2) {
			D2D1_POINT_2F pt = D2D1::Point2F(points[i], points[i + 1]);
			sink->AddLine(pt);
		}

		sink->EndFigure(D2D1_FIGURE_END_CLOSED);
		sink->Close();
	}
};
template class Owner<IGeometry>;

Geometry::Geometry(const std::vector<float>& points) {
	allocate();
	handle()->from(points);
}

} // namespace lintel
