#pragma once

#include "lintel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <iostream> // debug
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include "gpu.h"
#include "canvas.h"   // ← Canvas abstraction; included here so every node TU
//   that includes core.h automatically sees the type.

// Shortcuts to the singleton and its GPU / Canvas contexts.
#define CORE Core::get()
#define GPU  (Core::get().gpu)

namespace lintel {

// ---------------------------------------------------------------------------
// Forward declaration
// ---------------------------------------------------------------------------
//
// Bridges fire_with_context / fire_key_context / fire_char_context (defined
// below as inline helpers) to the EventRegistry without requiring core.h to
// include inode.h.  Implemented in inode.cpp.
//
void inode_invoke_handlers(class INode* impl, Node& handle, Event type);

// ---------------------------------------------------------------------------
// InputState
// ---------------------------------------------------------------------------
//
// Written by Core::process_message immediately before firing an event.
// Handlers read these via the Node query methods (mouse_x(), key_vkey(), …).
//
struct InputState {
    float mouse_x = 0.f; // Relative to the node under dispatch.
    float mouse_y = 0.f;
    float mouse_screen_x = 0.f; // Relative to the window client area.
    float mouse_screen_y = 0.f;
    float scroll_dx = 0.f; // Horizontal wheel delta (positive = right).
    float scroll_dy = 0.f; // Vertical   wheel delta (positive = up).

    MouseButton held = MouseButton::None;
    Modifiers   modifiers = {};

    int     key_vkey = 0;
    bool    key_repeat = false;
    wchar_t key_char = L'\0';
};

// ---------------------------------------------------------------------------
// FocusState
// ---------------------------------------------------------------------------

struct FocusState {
    WeakNode focused; // Node currently holding keyboard focus.
    WeakNode hovered; // Topmost node currently under the cursor.
};

// ---------------------------------------------------------------------------
// PointerState
// ---------------------------------------------------------------------------

struct PointerState {
    // Minimum cursor travel (px) before a press becomes a drag.
    static constexpr float k_drag_threshold = 4.f;

    WeakNode    pressed = {};             // Node where the last button was pressed.
    MouseButton press_btn = MouseButton::None;
    float       press_sx = 0.f;           // Screen coords of the press.
    float       press_sy = 0.f;

    bool     drag_pending = false;         // Press recorded; threshold not yet crossed.
    bool     drag_active = false;         // Drag sequence in progress.
    WeakNode drag_src = {};
    MouseButton drag_btn = MouseButton::None;

    WeakNode  last_click_node = {};        // For double-click detection.
    ULONGLONG last_click_ms = 0;
    float     last_click_sx = 0.f;
    float     last_click_sy = 0.f;
};

// ---------------------------------------------------------------------------
// EventRegistry
// ---------------------------------------------------------------------------
//
// Stores all (node, event-type) -> handler bindings.  Handlers are invoked in
// registration order.  The handler list is snapshot-copied before iteration so
// add / remove calls inside a handler are safe.
//
struct EventRegistry {
    struct Entry {
        Event              type;
        Node::EventHandler fn;
    };

    using HandlerList = std::vector<Entry>;
    std::unordered_map<class INode*, HandlerList> table;

    void add(class INode* node, Event type, Node::EventHandler fn) {
        table[node].push_back({ type, std::move(fn) });
    }

    void remove(class INode* node, Event type) {
        auto it = table.find(node);
        if (it == table.end()) return;

        HandlerList& list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(),
                   [type] (const Entry& e) { return e.type == type; }), list.end());

        if (list.empty()) table.erase(it);
    }

    // Invoke all matching handlers.  handle is the public Node& passed into
    // each callback.  The handler list is copied before iteration.
    void fire(class INode* node, Node& handle, Event type) const {
        auto it = table.find(node);
        if (it == table.end()) return;

        HandlerList snapshot = it->second; // copy — handlers may mutate table
        for (const Entry& e : snapshot) {
            if (e.type == type) e.fn(handle);
        }
    }

    void clear_node(class INode* node) { table.erase(node); }
};

// ---------------------------------------------------------------------------
// WindowMessage
// ---------------------------------------------------------------------------

struct WindowMessage {
    UINT   msg;
    WPARAM wp;
    LPARAM lp;
};

// ---------------------------------------------------------------------------
// AnimationQueue
// ---------------------------------------------------------------------------

struct AnimationItem {
    float* property_address;
    float begin;    // Initial value
    float end;      // Terminal value
    float duration; // Animation duration
    float t;        // Current time
};
struct Animator {
    std::vector<AnimationItem> items;
    std::vector<size_t> free_indices;

    void push(const AnimationItem& item);
    void step(float delta_time);
};

// ---------------------------------------------------------------------------
// Core
// ---------------------------------------------------------------------------
//
// Singleton that owns the GPU context, the Canvas drawing abstraction, the
// event registry, and all global input / focus / pointer state.
//
// Member declaration order matters: gpu must precede canvas because Canvas
// stores a GpuContext& that is bound at construction time.
//
class Core {
    std::thread               worker_;
    std::mutex                mut_;
    std::queue<WindowMessage> queue_;
    std::atomic<bool>         running_{ false };

    bool try_pop(WindowMessage& out);

    void process_message(UINT msg, WPARAM wp, LPARAM lp);
    void process_default(); // Layout + draw + present.
public:
    Core();
    ~Core();

    static Core& get();

    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;

    GpuContext    gpu;

    // Canvas must be declared after gpu so its GpuContext& reference is bound
    // to an already-constructed object.  It is initialised in the Core
    // constructor's member-initialiser list before gpu.initialize() is called;
    // Canvas stores only the reference at that point so no GPU API is invoked
    // during its own construction.
    Canvas        canvas;

    InputState    input;
    FocusState    focus;
    PointerState  pointer;
    EventRegistry registry;
    Animator      animator;

    WeakImpl<Window> window;
    Node             root;

    // UI Delta-time in seconds
    float ui_tick_dts = 0.0f;

    std::unordered_map<std::string, WeakNode> named_nodes;
    void register_named(std::string name, WeakNode node) {
        named_nodes[std::move(name)] = node;
    }
    WeakNode get_named(const std::string& name) {
        if (auto it = named_nodes.find(name); it != named_nodes.end()) return it->second;
        return WeakNode(nullptr);
    }

    // Null out any weak references that point to node (called when a node
    // is about to be destroyed).
    void clear_node(class INode* node) {
        if (focus.focused == node) focus.focused.reset();
        if (focus.hovered == node) focus.hovered.reset();
        if (pointer.pressed == node) pointer.pressed.reset();
        if (pointer.drag_src == node) pointer.drag_src.reset();
        if (pointer.last_click_node == node) pointer.last_click_node.reset();
    }

    // Thread-safe message enqueue (called from the Win32 message thread).
    void push(WindowMessage m);

    // Start the worker thread.
    void start();
};

// ---------------------------------------------------------------------------
// Fire helpers
// ---------------------------------------------------------------------------
//
// Write the appropriate InputState fields then invoke the handlers for the
// given event type (and Event::Any) on impl via the global registry.
//

// Mouse and scroll events.
inline void fire_with_context(
    INode* impl, Node& handle,
    Event type, float local_x, float local_y,
    MouseButton btn, Modifiers mods,
    float scroll_dx = 0.f, float scroll_dy = 0.f) {
    InputState& inp = CORE.input;
    inp.mouse_x = local_x;
    inp.mouse_y = local_y;
    inp.held = btn;
    inp.modifiers = mods;
    inp.scroll_dx = scroll_dx;
    inp.scroll_dy = scroll_dy;
    // Clear keyboard fields so stale values are never visible in a mouse handler.
    inp.key_vkey = 0;
    inp.key_repeat = false;
    inp.key_char = L'\0';

    inode_invoke_handlers(impl, handle, type);
    inode_invoke_handlers(impl, handle, Event::Any);
}

// KeyDown / KeyUp events.
inline void fire_key_context(
    INode* impl, Node& handle,
    Event type, int vkey, bool repeat, Modifiers mods) {
    InputState& inp = CORE.input;
    inp.key_vkey = vkey;
    inp.key_repeat = repeat;
    inp.key_char = L'\0';
    inp.modifiers = mods;
    // Clear scroll so stale values are never visible in a key handler.
    inp.scroll_dx = 0.f;
    inp.scroll_dy = 0.f;

    inode_invoke_handlers(impl, handle, type);
    inode_invoke_handlers(impl, handle, Event::Any);
}

// Char events.
inline void fire_char_context(
    INode* impl, Node& handle,
    wchar_t ch, Modifiers mods) {
    InputState& inp = CORE.input;
    inp.key_char = ch;
    inp.key_vkey = 0;
    inp.key_repeat = false;
    inp.modifiers = mods;
    inp.scroll_dx = 0.f;
    inp.scroll_dy = 0.f;

    inode_invoke_handlers(impl, handle, Event::Char);
    inode_invoke_handlers(impl, handle, Event::Any);
}

} // namespace lintel
