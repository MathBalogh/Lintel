#include "framework.h"

namespace lintel {

Framework::Framework() {
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
        { "background-color", property::BackgroundColor },
        { "border-color", property::BorderColor },
        { "border-weight", property::BorderWeight },
        { "border-radius", property::BorderRadius },
        { "text-color", property::TextColor },
        { "font-size", property::FontSize },
        { "font-family", property::FontFamily },
        { "width", property::Width },
        { "height", property::Height },
        { "padding-top", property::PaddingTop },
        { "padding-right", property::PaddingRight },
        { "padding-bottom", property::PaddingBottom },
        { "padding-left", property::PaddingLeft },
        { "margin-top", property::MarginTop },
        { "margin-right", property::MarginRight },
        { "margin-bottom", property::MarginBottom },
        { "margin-left", property::MarginLeft },
        { "gap", property::Gap },
        { "share", property::Share },
        { "direction", property::Direction },
        { "align-items", property::AlignItems },
        { "justify-items", property::JustifyItems },
        { "bold", property::Bold },
        { "italic", property::Italic },
        { "wrap", property::Wrap },
        { "editable", property::Editable },
        { "grid-color", property::GridColor },
        { "grid-weight",property::GridWeight },
        { "label-color", property::LabelColor },
        { "label-font-size", property::LabelFontSize },
        { "opacity", property::Opacity },
    };

    register_property("content");
}

Framework& Framework::get() {
	static Framework instance;
	return instance;
}

Event Framework::get_event(const std::string& name) {
	if (auto it = event_names_.find(name); it != event_names_.end()) return it->second;
	return Event::Null;
}

Property Framework::register_property(const std::string& name) {
	return (property_names_[name] = (Property) property_names_.size());
}
Property Framework::get_property(const std::string& name) {
	if (auto it = property_names_.find(name); it != property_names_.end()) return it->second;
	return 0;
}

Node Framework::create_node(const std::string& name) {
    if (auto it = node_factories_.find(name); it != node_factories_.end()) return it->second();
    return Node(nullptr);
}

} // namespace lintel
