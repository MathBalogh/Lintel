#include "lintel.h"

#include <iostream>

using namespace lintel;

int main() {
    Window win;

    auto [node, sheet] = load("./test.ltl");
    win.root() = std::move(node);

    if (auto n = sheet.find<GraphNode>("x")) {
        auto& s = n->series("x");
        for (size_t i = 0; i < 100; ++i) s.push(i, i);

        auto& u = n->series("y");
        for (size_t i = 0; i < 100; ++i) u.push(i, i);

        n->series("z");
    }

    return win.run();
}
