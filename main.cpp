#include "lintel.h"

#include <iostream>

using namespace lintel;

int main() {
	Window window;
	root() = load("main.ltl");

	if (auto n = find<GraphNode>("graph")) {
		std::vector<float> a;
		std::vector<float> b;

		for (float i = 0.0f; i < 6.282f; i += 0.1f) {
			a.push_back(i);
			b.push_back(std::cosf(i));
		}

		n->push_series(L"points", a, b);
	}

	return window.run();
}
