#include "lintel.h"

#include <iostream>

using namespace lintel;

int main() {
    Window win;

    auto [node, sheet] = load("./test.ltl");
    win.root() = std::move(node);

    if (auto n = sheet.find<TextNode>("text")) {
        n->properties()
            .set(Key::Editable, true)
            .set(Key::TextColor, Color(0, 0, 0));
    }

    return win.run();
}
