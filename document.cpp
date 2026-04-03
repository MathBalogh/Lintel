#include "document.h"
#include "inode.h"
#include "iwindow.h"

#include <windowsx.h>
#include <chrono>

using steady_clock = std::chrono::steady_clock;
using time_point = std::chrono::steady_clock::time_point;

namespace lintel {

// ---------------------------------------------------------------------------
// Document - message queue
// ---------------------------------------------------------------------------

bool Document::try_pop(WindowMessage& out) {
    std::lock_guard lock(mut_);
    if (queue_.empty()) return false;
    out = queue_.front();
    queue_.pop();
    return true;
}

void Document::push(WindowMessage m) {
    std::lock_guard lock(mut_);
    if (queue_.size() < 256)
        queue_.push(m);
}

// ---------------------------------------------------------------------------
// Document - lifecycle
// ---------------------------------------------------------------------------

Document::Document() {
    root->doc_ = this;
}
Document::~Document() {
    shutdown();
}

void Document::start() {
    running_ = true;

    worker_ = std::thread([this] {
        time_point last_t = steady_clock::now();

        WindowMessage msg;
        while (running_) {
            time_point now_t = steady_clock::now();
            ui_tick_dts = std::chrono::duration<float>(now_t - last_t).count();
            last_t = now_t;

            while (running_ && try_pop(msg))
                process_message(msg.msg, msg.wp, msg.lp);

            process_default();

            if (thread_tick)
                thread_tick();
        }
    });
}

void Document::shutdown() {
    running_ = false;
    if (worker_.joinable())
        worker_.join();
}

void Document::resize_now() {
    auto* win = window.handle<IWindow>();
    if (!win) return;

    const unsigned int new_w = win->client_width();
    const unsigned int new_h = win->client_height();
    if (new_w == 0 || new_h == 0) return; // minimised

    std::lock_guard lock(render_mut_);

    win->resize_swapchain();
    win->rebuild_ui_texture(new_w, new_h);

    if (INode* r = root.handle<INode>()) {
        r->props.set(Key::Width, static_cast<float>(new_w));
        r->props.set(Key::Height, static_cast<float>(new_h));
    }
}

// ---------------------------------------------------------------------------
// Document::bind_root
// ---------------------------------------------------------------------------
//
// Stamps the root node and every node already in the tree with this document
// pointer.  Call once after the initial scene is constructed, before start().

void Document::stamp_document(INode* node, Document* doc) {
    if (!node) return;
    node->doc_ = doc;
    for (Node& child : node->children)
        stamp_document(child.handle<INode>(), doc);
}

void Document::bind_root() {
    stamp_document(root.handle<INode>(), this);
}

// ---------------------------------------------------------------------------
// Document - focus control
// ---------------------------------------------------------------------------

void Document::set_focus(WeakNode target) {
    if (focus.focused == target) return;

    if (focus.focused) {
        INode* prev = focus.focused.handle<INode>();
        if (prev)
            fire_with_context(*this, prev, focus.focused,
                              Event::Blur, 0.f, 0.f, MouseButton::None, {});
    }

    focus.focused = target;

    if (focus.focused) {
        INode* next = focus.focused.handle<INode>();
        if (next)
            fire_with_context(*this, next, focus.focused,
                              Event::Focus, 0.f, 0.f, MouseButton::None, {});
    }
}

void Document::focus_next() {
    std::vector<INode*> focusable;
    INode::collect_focusable(root.handle<INode>(), focusable);
    if (focusable.empty()) return;

    INode* current = focus.focused.handle<INode>();
    if (!current) {
        set_focus(WeakNode(static_cast<void*>(focusable.front())));
        return;
    }

    for (size_t i = 0; i < focusable.size(); ++i) {
        if (focusable[i] == current) {
            set_focus(WeakNode(static_cast<void*>(
                focusable[(i + 1) % focusable.size()])));
            return;
        }
    }
    set_focus(WeakNode(static_cast<void*>(focusable.front())));
}

// ---------------------------------------------------------------------------
// Document - public input dispatch
// ---------------------------------------------------------------------------
//
// These are the sole entry-points for input.  process_message() decodes raw
// Win32 messages and delegates here; the same methods can be called directly
// by tests or synthetic-input producers.

void Document::dispatch_mouse_move(float sx, float sy, Modifiers mods) {
    INode* root_impl = root.handle<INode>();

    input.mouse_screen_x = sx;
    input.mouse_screen_y = sy;

    if (root_impl)
        root_impl->update_hover(WeakNode(root), sx, sy);

    if (pointer.drag_active) {
        if (pointer.drag_src) {
            INode* src = pointer.drag_src.handle<INode>();
            if (src) {
                fire_with_context(*this, src, pointer.drag_src, Event::Drag,
                                  sx - src->content_x(), sy - src->content_y(),
                                  pointer.drag_btn, mods);
                src->bubble_up(Event::Drag);
            }
        }
    }
    else if (pointer.drag_pending) {
        const float ddx = sx - pointer.press_sx;
        const float ddy = sy - pointer.press_sy;
        if (ddx * ddx + ddy * ddy >=
            PointerState::k_drag_threshold * PointerState::k_drag_threshold) {

            pointer.drag_pending = false;
            pointer.drag_active = true;
            pointer.drag_src = pointer.pressed;
            pointer.drag_btn = pointer.press_btn;

            if (pointer.drag_src) {
                INode* src = pointer.drag_src.handle<INode>();
                if (src) {
                    fire_with_context(*this, src, pointer.drag_src,
                                      Event::DragStart,
                                      sx - src->content_x(),
                                      sy - src->content_y(),
                                      pointer.drag_btn, mods);
                    src->bubble_up(Event::DragStart);
                }
            }
        }
    }
    else {
        if (root_impl) {
            if (Node* hit = root_impl->find_hit(root, sx, sy)) {
                INode* hi = hit->handle<INode>();
                fire_with_context(*this, hi, WeakNode(hi), Event::MouseMove,
                                  sx - hi->content_x(), sy - hi->content_y(),
                                  pointer.press_btn, mods);
                hi->bubble_up(Event::MouseMove);
            }
        }
    }
}

void Document::dispatch_mouse_leave() {
    INode* root_impl = root.handle<INode>();
    if (root_impl)
        root_impl->update_hover(WeakNode(root), -1.f, -1.f);
}

void Document::dispatch_mouse_down(float sx, float sy,
                                   MouseButton btn, Modifiers mods) {
    INode* root_impl = root.handle<INode>();
    Node* hit = root_impl ? root_impl->find_hit(root, sx, sy) : nullptr;
    if (!hit) return;

    INode* hi = hit->handle<INode>();

    set_focus(hi->focusable_flag
              ? WeakNode(static_cast<void*>(hi))
              : WeakNode{});

    pointer.pressed = WeakNode(static_cast<void*>(hi));
    pointer.press_btn = btn;
    pointer.press_sx = sx;
    pointer.press_sy = sy;
    pointer.drag_pending = hi->draggable_flag;

    fire_with_context(*this, hi, WeakNode(hi), Event::MouseDown,
                      sx - hi->content_x(), sy - hi->content_y(),
                      btn, mods);
    hi->bubble_up(Event::MouseDown);
}

void Document::dispatch_mouse_up(float sx, float sy,
                                 MouseButton btn, Modifiers mods) {
    INode* root_impl = root.handle<INode>();

    if (pointer.drag_active && btn == pointer.drag_btn) {
        pointer.drag_active = false;
        pointer.drag_pending = false;

        if (pointer.drag_src) {
            INode* src = pointer.drag_src.handle<INode>();
            if (src) {
                fire_with_context(*this, src, pointer.drag_src,
                                  Event::DragEnd,
                                  sx - src->content_x(),
                                  sy - src->content_y(),
                                  btn, mods);
            }
            pointer.drag_src.reset();
        }
    }
    else {
        if (Node* hit = root_impl ? root_impl->find_hit(root, sx, sy) : nullptr) {
            INode* hi = hit->handle<INode>();

            fire_with_context(*this, hi, WeakNode(hi), Event::MouseUp,
                              sx - hi->content_x(), sy - hi->content_y(),
                              btn, mods);
            hi->bubble_up(Event::MouseUp);

            if (hi == pointer.pressed.handle<INode>()) {
                if (btn == MouseButton::Right) {
                    fire_with_context(*this, hi, WeakNode(hi),
                                      Event::RightClick,
                                      sx - hi->content_x(),
                                      sy - hi->content_y(),
                                      btn, mods);
                    hi->bubble_up(Event::RightClick);
                }
                else {
                    const ULONGLONG now = GetTickCount64();
                    const float     ddx = sx - pointer.last_click_sx;
                    const float     ddy = sy - pointer.last_click_sy;
                    const bool same_node = (pointer.last_click_node
                                            == static_cast<void*>(hi));
                    const bool close_pos = (ddx * ddx + ddy * ddy) < 256.f;
                    const bool in_time = (now - pointer.last_click_ms)
                        <= GetDoubleClickTime();

                    if (same_node && close_pos && in_time) {
                        fire_with_context(*this, hi,
                                          WeakNode(hit->handle()),
                                          Event::DoubleClick,
                                          sx - hi->content_x(),
                                          sy - hi->content_y(),
                                          btn, mods);
                        hi->bubble_up(Event::DoubleClick);
                        pointer.last_click_ms = 0;
                    }
                    else {
                        fire_with_context(*this, hi,
                                          WeakNode(hit->handle()),
                                          Event::Click,
                                          sx - hi->content_x(),
                                          sy - hi->content_y(),
                                          btn, mods);
                        hi->bubble_up(Event::Click);

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
}

void Document::dispatch_scroll(float sx, float sy,
                               float dx, float dy, Modifiers mods) {
    INode* root_impl = root.handle<INode>();
    if (!root_impl) return;

    if (Node* hit = root_impl->find_hit(root, sx, sy)) {
        INode* hi = hit->handle<INode>();
        fire_with_context(*this, hi, WeakNode(hi), Event::Scroll,
                          sx - hi->content_x(), sy - hi->content_y(),
                          MouseButton::None, mods, dx, dy);
        hi->bubble_up(Event::Scroll);
    }
}

void Document::dispatch_key_down(int vkey, bool repeat, Modifiers mods) {
    if (vkey == VK_TAB) {
        focus_next();
        return;
    }
    if (focus.focused) {
        INode* fi = focus.focused.handle<INode>();
        if (fi) {
            fire_key_context(*this, fi, focus.focused,
                             Event::KeyDown, vkey, repeat, mods);
            fi->bubble_up(Event::KeyDown);
        }
    }
}

void Document::dispatch_key_up(int vkey, Modifiers mods) {
    if (focus.focused) {
        INode* fi = focus.focused.handle<INode>();
        if (fi) {
            fire_key_context(*this, fi, focus.focused,
                             Event::KeyUp, vkey, false, mods);
            fi->bubble_up(Event::KeyUp);
        }
    }
}

void Document::dispatch_char(wchar_t ch, Modifiers mods) {
    if (focus.focused) {
        INode* fi = focus.focused.handle<INode>();
        if (fi) {
            fire_char_context(*this, fi, focus.focused, ch, mods);
            fi->bubble_up(Event::Char);
        }
    }
}

// ---------------------------------------------------------------------------
// Document::process_message
// ---------------------------------------------------------------------------
//
// Decodes raw Win32 messages and delegates to the typed dispatch_* methods.
// No event-dispatch logic lives here anymore.

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

void Document::process_message(UINT msg, WPARAM wp, LPARAM lp) {
    auto* win = window.handle<IWindow>();
    if (!win) return;

    INode* root_impl = root.handle<INode>();

    switch (msg) {

        case WM_SIZE:
        {
            // resize_swapchain() resizes the swap-chain back buffer immediately
            // and records a pending UI texture resize.  The offscreen texture
            // and root layout dimensions are updated lazily in process_default()
            // once the resize gesture has settled, avoiding mid-resize thrash.
            win->resize_swapchain();
            break;
        }

        case WM_MOUSEMOVE:
        {
            const float sx = static_cast<float>(GET_X_LPARAM(lp));
            const float sy = static_cast<float>(GET_Y_LPARAM(lp));

            if (!win->tracking_mouse) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, win->hwnd, 0 };
                TrackMouseEvent(&tme);
                win->tracking_mouse = true;
            }

            dispatch_mouse_move(sx, sy, get_modifiers());
            break;
        }

        case WM_MOUSELEAVE:
            win->tracking_mouse = false;
            dispatch_mouse_leave();
            break;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            dispatch_mouse_down(
                static_cast<float>(GET_X_LPARAM(lp)),
                static_cast<float>(GET_Y_LPARAM(lp)),
                btn_from_msg(msg), get_modifiers());
            break;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            dispatch_mouse_up(
                static_cast<float>(GET_X_LPARAM(lp)),
                static_cast<float>(GET_Y_LPARAM(lp)),
                btn_from_msg(msg), get_modifiers());
            break;

        case WM_MOUSEWHEEL:
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(win->hwnd, &pt);
            const float delta = static_cast<float>(
                GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            dispatch_scroll(static_cast<float>(pt.x),
                            static_cast<float>(pt.y),
                            0.f, delta, get_modifiers());
            break;
        }

        case WM_MOUSEHWHEEL:
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(win->hwnd, &pt);
            const float delta = static_cast<float>(
                GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            dispatch_scroll(static_cast<float>(pt.x),
                            static_cast<float>(pt.y),
                            delta, 0.f, get_modifiers());
            break;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            dispatch_key_down(static_cast<int>(wp),
                              (lp & (1 << 30)) != 0,
                              get_modifiers());
            break;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            dispatch_key_up(static_cast<int>(wp), get_modifiers());
            break;

        case WM_CHAR:
            dispatch_char(static_cast<wchar_t>(wp), get_modifiers());
            break;

    } // switch
}

// ---------------------------------------------------------------------------
// Document::process_default
// ---------------------------------------------------------------------------
//
// Frame loop:
//
//   1. Resize check – if ui_pending_w/h differ from the current logical size,
//      rebuild the offscreen texture and update the root layout immediately.
//      The alloc-rounding in rebuild_ui_texture means no D3D allocation occurs
//      unless the window grows beyond the current physical allocation.
//
//   2. Layout + Draw – run layout and draw the scene into the offscreen
//      UI texture.  The D2D context target is ui_d2d_target throughout.
//
//   3. Blit – redirect the D2D context to the back-buffer bitmap, draw the
//      offscreen texture 1:1 into the back buffer, then restore the target.
//      Because the offscreen texture and swap chain are always the same logical
//      size there is never any stretching.
//
//   4. Present – flip the swap chain.

void Document::process_default() {
    auto* win = window.handle<IWindow>();
    if (!win || !win->swapchain)             return;
    if (win->width == 0 || win->height == 0) return;

    auto* d2d = GPU.d2d_context.Get();
    if (!d2d) return;

    if (!win->ui_d2d_target) return;

    // -----------------------------------------------------------------------
    // 1. Layout (outside the lock — pure CPU work, no D2D/D3D calls)
    // -----------------------------------------------------------------------

    if (INode* r = root.handle<INode>()) {
        r->layout(0.f, 0.f, static_cast<float>(win->ui_width), static_cast<float>(win->ui_height));
    }

    // -----------------------------------------------------------------------
    // 2. Draw + Blit (locked against resize_now on the message thread)
    // -----------------------------------------------------------------------

    {
        std::lock_guard lock(render_mut_);

        // Re-check after acquiring: resize_now may have just rebuilt targets.
        if (!win->ui_d2d_target || !win->d2d_target) return;

        d2d->SetTarget(win->ui_d2d_target.Get());
        d2d->BeginDraw();
        d2d->Clear(D2D1::ColorF(0.09f, 0.09f, 0.09f, 1.f));

        if (INode* r = root.handle<INode>())
            r->draw(root, canvas);

        d2d->EndDraw();

        D2D1_RECT_F dest = D2D1::RectF(
            0.f, 0.f,
            static_cast<float>(win->ui_width),
            static_cast<float>(win->ui_height));

        d2d->SetTarget(win->d2d_target.Get());
        d2d->BeginDraw();
        d2d->DrawBitmap(
            win->ui_d2d_target.Get(),
            &dest,
            1.0f,
            D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
            nullptr,
            nullptr);
        d2d->EndDraw();

        d2d->SetTarget(win->ui_d2d_target.Get());
    }

    // -----------------------------------------------------------------------
    // 3. Present
    // -----------------------------------------------------------------------

    win->swapchain->Present(1, 0);
}

std::string to_string(const std::wstring& w) {
    if (w.empty()) return {};
    #ifdef _WIN32
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
    #else
    return std::string(w.begin(), w.end()); // ASCII fallback
    #endif
}
std::wstring to_wstring(const std::string& s) {
    if (s.empty()) return {};

    #ifdef _WIN32
    // Convert UTF‑8 → UTF‑16 using Win32 API
    int n = MultiByteToWideChar(CP_UTF8, 0,
                                s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
                        s.data(), static_cast<int>(s.size()),
                        out.data(), n);
    return out;
    #else
    // ASCII fallback: widen each byte
    std::wstring out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<wchar_t>(c));
    }
    return out;
    #endif
}

} // namespace lintel
