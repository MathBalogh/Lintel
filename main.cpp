#include "lintel.h"

#include <iostream>

using namespace lintel;

int main() {
    Window win;

    auto [node, sheet] = load("./test.ltl");
    win.root() = std::move(node);

    return win.run();
}
