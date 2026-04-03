#include "lintel.h"
#include <unordered_map>

namespace lintel {

namespace {

class Registry {
    std::unordered_map<std::string, Event> event_names_;
    std::unordered_map<std::string, Key> property_names_;
    std::unordered_map<std::string, Node(*)()> node_factories_;
public:
    Registry() {
        event_names_ = {
            { "mouse-enter", Event::MouseEnter },
            { "mouse-leave", Event::MouseLeave },
            { "mouse-move", Event::MouseMove },
            { "mouse-down", Event::MouseDown },
            { "mouse-up", Event::MouseUp },
            { "click", Event::Click },
            { "double-click", Event::DoubleClick },
            { "right-click", Event::RightClick },
            { "scroll", Event::Scroll },
            { "focus", Event::Focus },
            { "blur", Event::Blur },
            { "key-down", Event::KeyDown },
            { "key-up", Event::KeyUp },
            { "char", Event::Char },
            { "drag-start", Event::DragStart },
            { "drag", Event::Drag },
            { "drag-end", Event::DragEnd },
            { "any", Event::Any },
        };
        property_names_ = {
            { "background-color", Key::BackgroundColor },
            { "border-color", Key::BorderColor },
            { "border-weight", Key::BorderWeight },
            { "border-radius", Key::BorderRadius },
            { "text-color", Key::TextColor },
            { "font-size", Key::FontSize },
            { "font-family", Key::FontFamily },
            { "width", Key::Width },
            { "height", Key::Height },
            { "padding-top", Key::PaddingTop },
            { "padding-right", Key::PaddingRight },
            { "padding-bottom", Key::PaddingBottom },
            { "padding-left", Key::PaddingLeft },
            { "margin-top", Key::MarginTop },
            { "margin-right", Key::MarginRight },
            { "margin-bottom", Key::MarginBottom },
            { "margin-left", Key::MarginLeft },
            { "gap", Key::Gap },
            { "share", Key::Share },
            { "direction", Key::Direction },
            { "align-items", Key::AlignItems },
            { "justify-items", Key::JustifyItems },
            { "bold", Key::Bold },
            { "italic", Key::Italic },
            { "wrap", Key::Wrap },
            { "editable", Key::Editable },
            { "vertical-center", Key::VerticalCenter },
            { "scrollbar", Key::Scrollbar },
            { "grid-color", Key::GridColor },
            { "grid-weight",Key::GridWeight },
            { "label-color", Key::LabelColor },
            { "label-font-size", Key::LabelFontSize },
            { "opacity", Key::Opacity },
        };

        register_key("content");
        register_key("path");
    }

    Event get_event(const std::string& name) {
        if (auto it = event_names_.find(name); it != event_names_.end()) return it->second;
        return Event::Null;
    }

    Key register_key(const std::string& name) {
        return (property_names_[name] = (unsigned int) property_names_.size());
    }
    Key get_key(const std::string& name) {
        if (auto it = property_names_.find(name); it != property_names_.end()) return it->second;
        return Key();
    }

    void register_node(const std::string& name, Node(*factory)()) {
        node_factories_.insert({ name, factory });
    }
    Node create_node(const std::string& name) {
        if (auto it = node_factories_.find(name); it != node_factories_.end()) return it->second();
        return Node(nullptr);
    }

};

} // namespace

static Registry& reg() {
    static Registry instance;
    return instance;
}

Event get_event(const std::string& name) {
    return reg().get_event(name);
}

Key register_key(const std::string& name) {
    return reg().register_key(name);
}

Key get_key(const std::string& name) {
    return reg().get_key(name);
}

void register_node(const std::string& name, Node(*factory)()) {
    return reg().register_node(name, factory);
}
Node create_node(const std::string& name) {
    return reg().create_node(name);
}

} // namespace lintel
