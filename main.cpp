#include "lintel.h"

using namespace lintel;

int main() {
	Window window;

	root().push(ImageNode("image.jpg"));

	return window.run();
}
