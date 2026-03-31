#include <C:\lib\lintel\include.h>
#include <C:\lib\udp\include.h>

#include <iostream>

using namespace lintel;

float n = 0.0f;

void push_message(WeakNode parent, const void* data, size_t len) {
    TextNode node;

    node.attr().set(property::BackgroundColor, Color(n += 0.1f, 0, 0));

    node.content().resize(len);
    std::memcpy(node.content().data(), data, len);

    parent->push(node);
}

int main() {
    Window win;

    udp::MulticastSocket sock;
    try {
        sock.join("239.255.0.1", 30001);
    } catch (const udp::SocketError& err) {
        std::cout << err.what() << '\n';
    }

    auto [node, sheet] = load("./test.ltl");
    win.root() = std::move(node);

    if (auto n = sheet.find<TextNode>("content")) {
        n->on(Event::Char, [&] (WeakNode node) {
            auto& n = node.as<TextNode>();
            if (win.key_char() == 13) {
                std::string str(n.content().begin(), n.content().end());
                sock.send(str);
                n.clear_content();
            }
        });
    }

    return win.run([&] () {
        if (auto opt = sock.try_receive()) {
            auto& msg = opt.value();
            if (auto n = sheet.find("messages")) {
                push_message(n, msg.data.data(), msg.data.size());
            }
        }
    });
}
