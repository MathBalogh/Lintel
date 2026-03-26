#include "lintel.h"
#include <iostream>

using namespace lintel;

int main() {
	Window window;
	
	root() = load("main.ltl");

	return window.run();
}
