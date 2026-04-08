#include "lintel.h"

#include <iostream>

using namespace lintel;

int main() {
    Window win;

    auto [node, sheet] = load("./test.ltl");
    win.root() = std::move(node);

    View<TextNode> x, y;
    sheet.find({ {x.as<NodePtr>(), "x"}, {y.as<NodePtr>(), "y"}});

    return win.run();
}
