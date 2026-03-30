#include "lintel.h"
#include <iostream>

using namespace lintel;

int main() {
	Window window;
	auto [ui, style] = load("./ui.ltl");
	window.root() = std::move(ui);
	return window.run();
}
