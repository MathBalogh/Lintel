#include "core.h"
#include "inode.h"
#include "iwindow.h"

#include <windowsx.h> // GET_X_LPARAM, GET_Y_LPARAM, GET_WHEEL_DELTA_WPARAM

#include <chrono>
using steady_clock = std::chrono::steady_clock;
using time_point = std::chrono::steady_clock::time_point;

namespace lintel {

// ---------------------------------------------------------------------------
// Internal helpers (file-local)
// ---------------------------------------------------------------------------

static Modifiers get_modifiers() noexcept {
    return Modifiers{
        (GetKeyState(VK_SHIFT) & 0x8000) != 0,
        (GetKeyState(VK_CONTROL) & 0x8000) != 0,
        (GetKeyState(VK_MENU) & 0x8000) != 0,
    };
}

static MouseButton btn_from_msg(UINT msg) noexcept {
    switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: return MouseButton::Left;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: return MouseButton::Right;
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: return MouseButton::Middle;
        default:                                return MouseButton::None;
    }
}

// ---------------------------------------------------------------------------
// Core singleton
// ---------------------------------------------------------------------------

// canvas(gpu) binds the Canvas reference before gpu.initialize() runs.
// Canvas stores only the reference during its own construction, so no GPU
// API is called until the first draw pass.
Core::Core(): canvas(gpu) {
    gpu.initialize();
}
Core::~Core() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

Core& Core::get() {
    static Core instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Message queue
// ---------------------------------------------------------------------------

bool Core::try_pop(WindowMessage& out) {
    std::lock_guard lock(mut_);
    if (queue_.empty()) return false;
    out = queue_.front();
    queue_.pop();
    return true;
}

void Core::push(WindowMessage m) {
    std::lock_guard lock(mut_);
    // Soft cap - prevents unbounded growth when the worker falls behind.
    if (queue_.size() < 256)
        queue_.push(m);
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

void Core::start() {
    running_ = true;

    worker_ = std::thread([this] {
        time_point last_t = steady_clock::now();

        WindowMessage msg;
        while (running_) {
            // Compute delta-time
            time_point now_t = steady_clock::now();
            ui_tick_dts = std::chrono::duration<float>(now_t - last_t).count();
            last_t = now_t;

            // Drain all pending messages first so the render reflects the
            // latest state rather than an intermediate one.
            while (running_ && try_pop(msg))
                process_message(msg.msg, msg.wp, msg.lp);

            process_default();

            if (window->handle()->thread_main)
                window->handle()->thread_main();
        }
    });
}

void Core::shutdown() {
    running_ = false;
    while (worker_.joinable())
        worker_.join();
}

// ---------------------------------------------------------------------------
// process_message - full Win32 input dispatch
// ---------------------------------------------------------------------------

void Core::process_message(UINT msg, WPARAM wp, LPARAM lp) {
    if (!window) return;
    auto* win = window.handle<IWindow>();
    if (!win) return;

    INode* root_impl = root.handle<INode>();

    switch (msg) {

        // -----------------------------------------------------------------------
        // Window resize
        // -----------------------------------------------------------------------

        case WM_SIZE:
        {
            root_impl->attr.set(property::Width, (float) win->client_width());
            root_impl->attr.set(property::Height, (float) win->client_height());
            win->resize_swapchain();
            break;
        }

        // -----------------------------------------------------------------------
        // Mouse move - hover update, drag progression, and normal MouseMove
        // -----------------------------------------------------------------------

        case WM_MOUSEMOVE:
        {
            const float sx = static_cast<float>(GET_X_LPARAM(lp));
            const float sy = static_cast<float>(GET_Y_LPARAM(lp));
            const Modifiers mods = get_modifiers();

            input.mouse_screen_x = sx;
            input.mouse_screen_y = sy;

            // Subscribe to WM_MOUSELEAVE so we can clear hover state.
            if (!win->tracking_mouse) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, win->hwnd, 0 };
                TrackMouseEvent(&tme);
                win->tracking_mouse = true;
            }

            // Update per-node mouse_inside flags; fire MouseEnter / MouseLeave.
            if (root_impl)
                root_impl->update_hover(WeakNode(root), sx, sy);

            // -- Drag --
            if (pointer.drag_active) {
                // Ongoing drag: fire Drag on the source node.
                if (pointer.drag_src) {
                    INode* src = pointer.drag_src.handle<INode>();
                    if (src) {
                        fire_with_context(src, pointer.drag_src, Event::Drag,
                                          sx - src->content_x(), sy - src->content_y(),
                                          pointer.drag_btn, mods);
                        INode::bubble_up(src, Event::Drag);
                    }
                }
            }
            else if (pointer.drag_pending) {
                // Check whether the drag threshold has been crossed.
                const float dx = sx - pointer.press_sx;
                const float dy = sy - pointer.press_sy;
                if (dx * dx + dy * dy >=
                    PointerState::k_drag_threshold * PointerState::k_drag_threshold) {
                    pointer.drag_pending = false;
                    pointer.drag_active = true;
                    pointer.drag_src = pointer.pressed;
                    pointer.drag_btn = pointer.press_btn;

                    if (pointer.drag_src) {
                        INode* src = pointer.drag_src.handle<INode>();
                        if (src) {
                            fire_with_context(src, pointer.drag_src, Event::DragStart,
                                              sx - src->content_x(), sy - src->content_y(),
                                              pointer.drag_btn, mods);
                            INode::bubble_up(src, Event::DragStart);
                        }
                    }
                }
            }
            else {
                // Normal move: route to the hit node.
                if (root_impl) {
                    if (Node* hit = root_impl->find_hit(root, sx, sy)) {
                        INode* hi = hit->handle<INode>();
                        fire_with_context(hi, WeakNode(hi), Event::MouseMove,
                                          sx - hi->content_x(), sy - hi->content_y(),
                                          pointer.press_btn, mods);
                        INode::bubble_up(hi, Event::MouseMove);
                    }
                }
            }
            break;
        }

        // -----------------------------------------------------------------------
        // Mouse leave - clear all hover state
        // -----------------------------------------------------------------------

        case WM_MOUSELEAVE:
            win->tracking_mouse = false;
            // Pass a point guaranteed to miss every node rect.
            if (root_impl)
                root_impl->update_hover(WeakNode(root), -1.f, -1.f);
            break;

            // -----------------------------------------------------------------------
            // Button down - press, optional focus transfer
            // -----------------------------------------------------------------------

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        {
            const float sx = static_cast<float>(GET_X_LPARAM(lp));
            const float sy = static_cast<float>(GET_Y_LPARAM(lp));
            const Modifiers mods = get_modifiers();
            const MouseButton btn = btn_from_msg(msg);

            Node* hit = root_impl ? root_impl->find_hit(root, sx, sy) : nullptr;
            if (hit) {
                INode* hi = hit->handle<INode>();

                // Transfer keyboard focus on click.
                INode::set_focus(
                    hi->focusable_flag
                    ? WeakNode(static_cast<void*>(hi))
                    : WeakNode{}
                );

                pointer.pressed = WeakNode(static_cast<void*>(hi));
                pointer.press_btn = btn;
                pointer.press_sx = sx;
                pointer.press_sy = sy;
                pointer.drag_pending = hi->draggable_flag;

                fire_with_context(hi, WeakNode(hi), Event::MouseDown,
                                  sx - hi->content_x(), sy - hi->content_y(), btn, mods);
                INode::bubble_up(hi, Event::MouseDown);
            }
            break;
        }

        // -----------------------------------------------------------------------
        // Button up - release, click, double-click, drag end
        // -----------------------------------------------------------------------

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        {
            const float sx = static_cast<float>(GET_X_LPARAM(lp));
            const float sy = static_cast<float>(GET_Y_LPARAM(lp));
            const Modifiers mods = get_modifiers();
            const MouseButton btn = btn_from_msg(msg);

            if (pointer.drag_active && pointer.drag_btn == btn) {
                // End the drag sequence.
                pointer.drag_active = false;
                if (pointer.drag_src) {
                    INode* src = pointer.drag_src.handle<INode>();
                    if (src) {
                        fire_with_context(src, pointer.drag_src, Event::DragEnd,
                                          sx - src->content_x(), sy - src->content_y(), btn, mods);
                    }
                }
                pointer.drag_src.reset();
            }
            else {
                // Normal release.
                if (Node* hit = root_impl ? root_impl->find_hit(root, sx, sy) : nullptr) {
                    INode* hi = hit->handle<INode>();

                    fire_with_context(hi, WeakNode(hi), Event::MouseUp,
                                      sx - hi->content_x(), sy - hi->content_y(), btn, mods);
                    INode::bubble_up(hi, Event::MouseUp);

                    // Click fires only when the release is on the same node as the press.
                    if (hi == pointer.pressed.handle<INode>()) {
                        if (btn == MouseButton::Right) {
                            fire_with_context(hi, WeakNode(hi), Event::RightClick,
                                              sx - hi->content_x(), sy - hi->content_y(), btn, mods);
                            INode::bubble_up(hi, Event::RightClick);
                        }
                        else {
                            // Double-click detection.
                            const ULONGLONG now = GetTickCount64();
                            const float ddx = sx - pointer.last_click_sx;
                            const float ddy = sy - pointer.last_click_sy;
                            const bool  same_node = (pointer.last_click_node == static_cast<void*>(hi));
                            const bool  close_pos = (ddx * ddx + ddy * ddy) < (16.f * 16.f);
                            const bool  in_time = (now - pointer.last_click_ms) <= GetDoubleClickTime();

                            if (same_node && close_pos && in_time) {
                                fire_with_context(hi, WeakNode(hit->handle()), Event::DoubleClick,
                                                  sx - hi->content_x(), sy - hi->content_y(), btn, mods);
                                INode::bubble_up(hi, Event::DoubleClick);
                                pointer.last_click_ms = 0; // reset so a third click is a new single
                            }
                            else {
                                fire_with_context(hi, WeakNode(hit->handle()), Event::Click,
                                                  sx - hi->content_x(), sy - hi->content_y(), btn, mods);
                                INode::bubble_up(hi, Event::Click);

                                pointer.last_click_node = WeakNode(static_cast<void*>(hi));
                                pointer.last_click_ms = now;
                                pointer.last_click_sx = sx;
                                pointer.last_click_sy = sy;
                            }
                        }
                    }
                }
            }

            pointer.drag_pending = false;
            if (btn == pointer.press_btn) {
                pointer.pressed.reset();
                pointer.press_btn = MouseButton::None;
            }
            break;
        }

        // -----------------------------------------------------------------------
        // Vertical scroll
        // -----------------------------------------------------------------------

        case WM_MOUSEWHEEL:
        {
            const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            const Modifiers mods = get_modifiers();

            // WM_MOUSEWHEEL gives screen coordinates; convert to client space.
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(win->hwnd, &pt);
            const float sx = static_cast<float>(pt.x);
            const float sy = static_cast<float>(pt.y);

            if (root_impl) {
                if (Node* hit = root_impl->find_hit(root, sx, sy)) {
                    INode* hi = hit->handle<INode>();
                    fire_with_context(hi, WeakNode(hi), Event::Scroll,
                                      sx - hi->content_x(), sy - hi->content_y(),
                                      MouseButton::None, mods,
                                      0.f, delta);
                    INode::bubble_up(hi, Event::Scroll);
                }
            }
            break;
        }

        // -----------------------------------------------------------------------
        // Horizontal scroll
        // -----------------------------------------------------------------------

        case WM_MOUSEHWHEEL:
        {
            const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            const Modifiers mods = get_modifiers();

            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(win->hwnd, &pt);
            const float sx = static_cast<float>(pt.x);
            const float sy = static_cast<float>(pt.y);

            if (root_impl) {
                if (Node* hit = root_impl->find_hit(root, sx, sy)) {
                    INode* hi = hit->handle<INode>();
                    fire_with_context(hi, WeakNode(hi), Event::Scroll,
                                      sx - hi->content_x(), sy - hi->content_y(),
                                      MouseButton::None, mods,
                                      delta, 0.f);
                    INode::bubble_up(hi, Event::Scroll);
                }
            }
            break;
        }

        // -----------------------------------------------------------------------
        // Key down - Tab advances focus; all others route to the focused node
        // -----------------------------------------------------------------------

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            const int     vkey = static_cast<int>(wp);
            const bool    repeat = (lp & (1 << 30)) != 0;
            const Modifiers mods = get_modifiers();

            if (vkey == VK_TAB) {
                INode::focus_next(root);
                break;
            }

            if (focus.focused) {
                INode* fi = focus.focused.handle<INode>();
                if (fi) {
                    fire_key_context(fi, focus.focused, Event::KeyDown, vkey, repeat, mods);
                    INode::bubble_up(fi, Event::KeyDown);
                }
            }
            break;
        }

        // -----------------------------------------------------------------------
        // Key up
        // -----------------------------------------------------------------------

        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            const int     vkey = static_cast<int>(wp);
            const Modifiers mods = get_modifiers();

            if (focus.focused) {
                INode* fi = focus.focused.handle<INode>();
                if (fi) {
                    fire_key_context(fi, focus.focused, Event::KeyUp, vkey, false, mods);
                    INode::bubble_up(fi, Event::KeyUp);
                }
            }
            break;
        }

        // -----------------------------------------------------------------------
        // Character input - routes to the focused node
        // -----------------------------------------------------------------------

        case WM_CHAR:
        {
            const wchar_t ch = static_cast<wchar_t>(wp);
            const Modifiers mods = get_modifiers();

            if (focus.focused) {
                INode* fi = focus.focused.handle<INode>();
                if (fi) {
                    fire_char_context(fi, focus.focused, ch, mods);
                    INode::bubble_up(fi, Event::Char);
                }
            }
            break;
        }

    } // switch (msg)
}

// ---------------------------------------------------------------------------
// process_default - layout, draw, present
// ---------------------------------------------------------------------------
//
// Called by the worker thread after each message-flush.  Present(1, 0) syncs
// to vsync so this naturally throttles to the display refresh rate.
//
void Core::process_default() {
    if (!window) return;
    auto* win = window.handle<IWindow>();
    if (!win || !win->swapchain)          return;
    if (win->width == 0 || win->height == 0) return; // minimised

    auto* d2d = GPU.d2d_context.Get();
    if (!d2d) return;

    // -- Animate ---------------------------------------------------------
    //
    // Advance all active tweens by the elapsed frame time.  This must run
    // before layout so that any tween writing a layout-affecting prop (e.g.
    // Width, MarginBottom) is already resolved when measure() runs.
    //
    if (INode* r = root.handle<INode>())
        r->tick_tweens(ui_tick_dts);

    // -- Layout ----------------------------------------------------------
    if (INode* r = root.handle<INode>())
        r->layout(0.f, 0.f,
                  static_cast<float>(win->width),
                  static_cast<float>(win->height));

    // -- Draw ------------------------------------------------------------
    //
    // canvas is a member of Core and is valid for the entire lifetime of the
    // application.  Passing it by reference into draw() means every node in
    // the tree receives the same Canvas for the frame, keeping all D2D calls
    // behind a single abstraction boundary.
    //
    d2d->BeginDraw();
    d2d->Clear(D2D1::ColorF(0.09f, 0.09f, 0.09f, 1.f));

    if (INode* r = root.handle<INode>())
        r->draw(root, canvas);

    // Ignore D2DERR_RECREATE_TARGET for now; a full device-lost recovery path
    // would call win->rebuild_targets() and retry here.
    d2d->EndDraw();

    // -- Present ---------------------------------------------------------
    if (window)
        win->swapchain->Present(1, 0); // SyncInterval = 1 -> vsync
}

} // namespace lintel
