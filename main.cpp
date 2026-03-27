#include "lintel.h"
#include <iostream>

using namespace lintel;

int main() {
	Window window;
	
	auto [subtree, sheet] = load("./ui.ltl");
	root() = std::move(subtree);

	return window.run();
}
