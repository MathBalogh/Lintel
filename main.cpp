#include "lintel.h"

using namespace lintel;

int main() {
	Window window;
	root() = load("main.ltl");
	return window.run();
}
