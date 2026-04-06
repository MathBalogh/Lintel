#pragma once

#include "lintel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "canvas.h"

namespace lintel {

// ---------------------------------------------------------------------------
// InputState
// ---------------------------------------------------------------------------

struct InputState {
    float mouse_x = 0.f;
    float mouse_y = 0.f;
    float mouse_screen_x = 0.f;
    float mouse_screen_y = 0.f;
    float scroll_dx = 0.f;
    float scroll_dy = 0.f;

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
    WeakNode focused;
    WeakNode hovered;
};

// ---------------------------------------------------------------------------
// PointerState
// ---------------------------------------------------------------------------

struct PointerState {
    static constexpr float k_drag_threshold = 4.f;

    WeakNode    pressed = {};
    MouseButton press_btn = MouseButton::None;
    float       press_sx = 0.f;
    float       press_sy = 0.f;

    bool        drag_pending = false;
    bool        drag_active = false;
    WeakNode    drag_src = {};
    MouseButton drag_btn = MouseButton::None;

    WeakNode  last_click_node = {};
    ULONGLONG last_click_ms = 0;
    float     last_click_sx = 0.f;
    float     last_click_sy = 0.f;
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
// Document
// ---------------------------------------------------------------------------
//
// Owns everything that belongs to one UI scene: the root node, all input /
// focus / pointer state, the named-node registry, the message queue, and the
// worker thread that drives layout → draw → present.
//
// One Window constructs one Document, sets Document::window, then calls
// Document::start().  Win32 messages are forwarded via Document::push().
//
// Input dispatch
// --------------
// The public dispatch_* methods are the sole entry-points for injecting
// synthetic or real input.  They mirror the Win32 messages that
// process_message() handles internally, but with coordinates and button
// identity already decoded so the caller (Window, test harness, …) does not
// need to know anything about INode internals.
//
// Node ownership and document back-pointer
// -----------------------------------------
// Every INode holds a raw Document* (doc_) that is null until the node
// enters the tree owned by this Document.  The pointer propagates
// automatically: Document::bind_root() stamps the root and all existing
// children; Node::push() stamps any newly-adopted node and its subtree.
// Node::remove() clears the pointer on the subtree being detached.
// INode::~INode calls Document::clear_node() through this pointer so that
// no manual CORE global is needed.
//
class Document {
    std::thread               worker_;
    std::mutex                mut_;         // guards the message queue
    std::mutex                render_mut_;  // guards swap-chain / D2D targets
    std::queue<WindowMessage> queue_;
    std::atomic<bool>         running_{ false };

    bool try_pop(WindowMessage& out);
    void process_message(UINT msg, WPARAM wp, LPARAM lp);
    void process_default();

public:
    Document();
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    Canvas canvas;

    // -- Scene -----------------------------------------------------------

    Node root;

    // Stamp root (and any nodes already in the tree) with this document.
    // Call once, after the initial scene is built and before start().
    void bind_root();

    // -- Input state -----------------------------------------------------

    InputState   input;
    FocusState   focus;
    PointerState pointer;

    float ui_tick_dts = 0.0f;
    float program_elapsed_s();

    // -- Named-node registry ---------------------------------------------

    std::unordered_map<std::string, WeakNode> named_nodes;

    void     register_named(std::string name, WeakNode node) {
        named_nodes[std::move(name)] = node;
    }
    WeakNode get_named(const std::string& name) {
        auto it = named_nodes.find(name);
        return it != named_nodes.end() ? it->second : WeakNode(nullptr);
    }

    // -- Weak-ref cleanup ------------------------------------------------
    //
    // Called from INode's destructor (via doc_) to null any document-level
    // WeakNode references that point at the node being torn down.

    void clear_node(class INode* node) {
        if (focus.focused == node) focus.focused.reset();
        if (focus.hovered == node) focus.hovered.reset();
        if (pointer.pressed == node) pointer.pressed.reset();
        if (pointer.drag_src == node) pointer.drag_src.reset();
        if (pointer.last_click_node == node) pointer.last_click_node.reset();
    }

    // -- Window back-pointer ---------------------------------------------
    //
    // Set by Window before start() is called.
    WeakImpl<class IWindow> window;

    // Optional per-frame callback (supplied by Window::run()).
    std::function<void()> thread_tick;

    // -- Public input dispatch -------------------------------------------
    //
    // These are the high-level interface that Window (and tests) call instead
    // of raw process_message().  Each method performs hit-testing, fires the
    // relevant events on the target node(s), and updates pointer/focus state.
    //
    // Coordinates are in client (window-local) pixels.

    void dispatch_mouse_move(float sx, float sy, Modifiers mods);
    void dispatch_mouse_leave();
    void dispatch_mouse_down(float sx, float sy, MouseButton btn, Modifiers mods);
    void dispatch_mouse_up(float sx, float sy, MouseButton btn, Modifiers mods);
    void dispatch_scroll(float sx, float sy, float dx, float dy, Modifiers mods);
    void dispatch_key_down(int vkey, bool repeat, Modifiers mods);
    void dispatch_key_up(int vkey, Modifiers mods);
    void dispatch_char(wchar_t ch, Modifiers mods);

    // -- Focus control ---------------------------------------------------
    //
    // Set keyboard focus to an arbitrary node (or clear it with a null/empty
    // WeakNode).  Fires Blur on the old focus and Focus on the new one.

    void set_focus(WeakNode target);

    // Advance focus to the next focusable node in document order (Tab).
    void focus_next();

    // -- Lifecycle -------------------------------------------------------

    void push(WindowMessage m); // thread-safe, called from Win32 thread
    void start();               // launch worker thread
    void shutdown();            // stop worker thread (blocking)

    // Called directly from WndProc (message thread) on WM_SIZE.
    // Acquires render_mut_ so it never races with an in-flight draw.
    void resize_now();

    // Recursively stamp every INode in a subtree with a document pointer.
    static void stamp_document(class INode* node, Document* doc);
};

// ---------------------------------------------------------------------------
// Fire-helper forward declarations
// ---------------------------------------------------------------------------
//
// Inline definitions live at the bottom of inode.h (where INode is complete).
// Every helper takes Document& so it writes to the correct InputState.
//

inline void fire_with_context(
    Document& doc,
    class INode* impl, WeakNode handle,
    Event type, float local_x, float local_y,
    MouseButton btn, Modifiers mods,
    float scroll_dx = 0.f, float scroll_dy = 0.f);

inline void fire_key_context(
    Document& doc,
    class INode* impl, WeakNode handle,
    Event type, int vkey, bool repeat, Modifiers mods);

inline void fire_char_context(
    Document& doc,
    class INode* impl, WeakNode handle,
    wchar_t ch, Modifiers mods);

} // namespace lintel
