#include "lintel.h"
using namespace lintel;

#include <iostream>

int main() {
	Window win;

	StyleSheet ss;
	win.root() = load("test.ltl", ss);

	return win.run();
}
