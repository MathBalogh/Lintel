#pragma once
#include "inode.h"

namespace lintel {

class ICanvasNode : public INode {
public:
	std::function<void(CanvasNode&)> on_draw = nullptr;
	Color current_fill = Color(0, 0, 0);

	void transpose(float& x, float& y) {
		x += rect.x;
		y += rect.y;
	}

	void draw(Node& self, Canvas& canvas) override;
};

} // namespace lintel
