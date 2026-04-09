#include "lintel.h"
using namespace lintel;

#include <iostream>

int main() {
	Window win;

	CanvasNode node;
	node.set(Key::Share, 1.0f);

	Geometry geo;
	{
        GeometryBuilder builder;
        
        // Start the figure
        builder.begin_figure(0.0f, 0.0f);

        // Straight line
        builder.add_line(50.0f, 0.0f);

        // Cubic bezier curve
        builder.add_bezier(
            60.0f, 20.0f,   // control point 1
            80.0f, 20.0f,   // control point 2
            100.0f, 0.0f    // end point
        );

        // Quadratic bezier curve
        builder.add_quadratic_bezier(
            120.0f, -20.0f, // control point
            140.0f, 0.0f    // end point
        );

        // Arc
        builder.add_arc(
            160.0f, 0.0f,   // center or endpoint (depends on implementation)
            30.0f, 30.0f,   // radii
            3.14159f / 2.0f,// 90 degrees
            true            // clockwise
        );

        // Close with another line for completeness
        builder.add_line(0.0f, 0.0f);

        // Finalize geometry
        geo = builder.end_figure();
	}

	node.on_draw([&] (CanvasNode& c) {
		c.fill({ 1, 0, 0 });
        c.translate(win.mouse_x(), win.mouse_y());
		c.geometry(geo);
	});

	win.root().push(node);

	return win.run();
}
