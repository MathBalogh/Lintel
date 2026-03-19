#include "lintel.h"

#include <iostream>

using namespace lintel;

int main() {
	Window window;
	root() = load("main.ltl");

	if (auto n = find("test")) {
		n->attr().set(attribs::background_color, Color(0, 0, 0));

		n->on(Event::Click, [] (Node& self) {
			Color* col = self.attr().get<Color>(attribs::background_color);
			if (col->r == 0.0f) {
				animate(&col->r, 0.0f, 1.0f, 1.0f);
			}
			else if (col->r == 1.0f) {
				animate(&col->r, 1.0f, 0.0f, 1.0f);
			}
		});
	}

	return window.run();
}
