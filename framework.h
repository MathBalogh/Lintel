#pragma once
#include "lintel.h"

namespace lintel {

class Framework {
    using NodeFactory = std::function<Node()>;

    std::unordered_map<std::string, Event> event_names_;
    std::unordered_map<std::string, Property> property_names_;
    std::unordered_map<std::string, NodeFactory> node_factories_;
public:
    Framework();

	static Framework& get();

    Event get_event(const std::string& name);

    Property register_property(const std::string& name);
    Property get_property(const std::string& name);

    template<typename T>
    void register_node(const std::string& name) {
        node_factories_[name] = [] () {
            return T();
        };
    }

    Node create_node(const std::string& name);
};

// Shorthand
#define FRAMEWORK (Framework::get())

} // namespace lintel
