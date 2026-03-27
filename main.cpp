#include "lintel.h"
#include <iostream>

using namespace lintel;

int main() {
	Window window;
	
	auto [subtree, sheet] = load("./test.ltl");
	root() = std::move(subtree);

	std::wstring c;
	for (size_t i = 0; i < 512; ++i)
		c += std::to_wstring(i) + L" ";
	find<TextNode>("txt")->content(c);

	return window.run();
}
