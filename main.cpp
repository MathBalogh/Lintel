#include "lintel.h"

#include <iostream>

using namespace lintel;

int main() {
    Window win;

    auto [node, sheet] = load("./test.ltl");
    win.root() = std::move(node);

    if (auto n = sheet.find<TextNode>("text")) {
        n->on_char([] (wchar_t ch) {
            if (ch == '\r') return false;
            return true;
        });
    }

    return win.run();
}
