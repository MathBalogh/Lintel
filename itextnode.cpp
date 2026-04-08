#include "itextnode.h"

#include <cwctype>
#include <vector>
#include <algorithm>

#define SELF (*handle<ITextNode>())

namespace lintel {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static DWRITE_TEXT_ALIGNMENT dwrite_alignment(TextAlign a) {
    switch (a) {
        case TextAlign::TextAlignCenter:  return DWRITE_TEXT_ALIGNMENT_CENTER;
        case TextAlign::TextAlignRight:   return DWRITE_TEXT_ALIGNMENT_TRAILING;
        case TextAlign::TextAlignJustify: return DWRITE_TEXT_ALIGNMENT_JUSTIFIED;
        default:                          return DWRITE_TEXT_ALIGNMENT_LEADING;
    }
}

// ---------------------------------------------------------------------------
// Style synchronisation
// ---------------------------------------------------------------------------

void ITextNode::apply_callback(Key key) {
    bool changed = false;

    if (key == get_key("content")) {
        if (const auto* str = props.find(key, Property::Type::WString)) {
            content = str->get_wstring();
            content_height_ = 0.f;
            scroll_offset_y = 0.f;
        }
        return;
    }

    switch (key.index) {
        case Key::FontFamily:
            if (const auto* v = props.find(Key::FontFamily, Property::Type::WString))
                if (v->get_wstring() != font_family) { font_family = v->get_wstring(); changed = true; }
            break;
        case Key::FontSize:
            if (const auto* v = props.get_float(Key::FontSize))
                if (*v != font_size) { font_size = *v; changed = true; }
            break;
        case Key::TextColor:
            if (const Color* v = props.get_color(Key::TextColor)) text_color = *v;
            break;
        case Key::Bold:
            if (const auto* v = props.find(Key::Bold, Property::Type::Bool))
                if (v->get_bool() != bold) { bold = *v; changed = true; }
            break;
        case Key::Italic:
            if (const auto* v = props.find(Key::Italic, Property::Type::Bool))
                if (v->get_bool() != italic_val) { italic_val = *v; changed = true; }
            break;
        case Key::Wrap:
            if (const auto* v = props.find(Key::Wrap, Property::Type::Bool))
                if (v->get_bool() != wrap) { wrap = *v; changed = true; }
            break;
        case Key::TextAlign:
            if (const auto* v = props.find(Key::TextAlign, Property::Type::Enum)) {
                TextAlign ta = (TextAlign) v->get_enum();
                if (ta != text_align_val) { text_align_val = ta; changed = true; }
            }
            break;
        case Key::VerticalCenter:
        {
            bool prev = vertical_center;
            vertical_center = props.get(Key::VerticalCenter);
            if (prev != vertical_center) invalidate_format();
            break;
        }
        case Key::Scrollbar:
        {
            bool prev = scrollbar_enabled;
            scrollbar_enabled = props.get(Key::Scrollbar);
            if (prev != scrollbar_enabled) props.make_dirty();
            break;
        }
        case Key::Editable:
            editable = props.get(Key::Editable);
            break;
        default:
            props.make_dirty();
            break;
    }

    if (changed) { props.make_dirty(); invalidate_format(); }
}

// ---------------------------------------------------------------------------
// Event wiring
// ---------------------------------------------------------------------------

void ITextNode::wire_events(Node& handle) {
    handle.on(Event::Focus, [this] (WeakNode) {
        has_focus = true;
    });

    handle.on(Event::Blur, [this] (WeakNode) {
        has_focus = false;
        caret_pos = selection_anchor = 0;
        lmb_selecting = scrollbar_dragging = false;
    });

    handle.on(Event::MouseDown, [this] (WeakNode self) {
        float mx = self->mouse_x(), my = self->mouse_y();

        if (is_scrollbar_visible() && is_in_scrollbar(mx, my)) {
            float my_rel = my - content_y();
            float ty_rel = thumb_y() - content_y();
            float th = thumb_height();
            if (my_rel >= ty_rel && my_rel < ty_rel + th) {
                scrollbar_dragging = true;
                scrollbar_drag_offset = my_rel - ty_rel;
            }
            else {
                scroll_offset_y = (my_rel / inner_h()) * get_max_scroll();
                clamp_scroll();
            }
            return;
        }

        lmb_selecting = true;
        on_click_position(mx, my, doc_->input.modifiers.shift);
    });

    handle.on(Event::MouseMove, [this] (WeakNode self) {
        if (scrollbar_dragging) {
            float usable = inner_h() - thumb_height();
            if (usable > 0.f) {
                float frac = (self->mouse_y() - content_y() - scrollbar_drag_offset) / usable;
                scroll_offset_y = std::clamp(frac * get_max_scroll(), 0.f, get_max_scroll());
            }
            return;
        }
        if (lmb_selecting)
            on_click_position(self->mouse_x(), self->mouse_y(), /*extend=*/true);
    });

    handle.on(Event::MouseUp, [this] (WeakNode) {
        lmb_selecting = scrollbar_dragging = false;
    });

    handle.on(Event::Char, [this] (WeakNode) {
        if (editable && has_focus) on_input(doc_->input.key_char);
    });

    handle.on(Event::KeyDown, [this] (WeakNode) {
        if (!has_focus) return;
        const bool shift = doc_->input.modifiers.shift;
        const bool ctrl = doc_->input.modifiers.ctrl;

        auto nav = [&] (auto fn) { caret_blink_s = 0.f; fn(); };

        switch (doc_->input.key_vkey) {
            case VK_BACK:   if (editable) on_backspace(); break;
            case VK_DELETE: if (editable) on_delete();    break;
            case VK_LEFT:  nav([&] { ctrl ? on_move_word_left(shift) : on_move_left(shift);  }); break;
            case VK_RIGHT: nav([&] { ctrl ? on_move_word_right(shift) : on_move_right(shift); }); break;
            case VK_HOME:  nav([&] { on_move_home(shift); }); break;
            case VK_END:   nav([&] { on_move_end(shift);  }); break;
            case 0x41: if (ctrl) { selection_anchor = 0; caret_pos = content.size(); } break; // Ctrl+A
            case 0x43: if (ctrl) copy_to_clipboard();                                  break; // Ctrl+C
            case 0x58: if (ctrl && editable) { copy_to_clipboard(); if (has_selection()) delete_selection(); } break; // Ctrl+X
            case 0x56: if (ctrl && editable) paste_from_clipboard();                   break; // Ctrl+V
            default: break;
        }
    });

    handle.focusable(true);
}

// ---------------------------------------------------------------------------
// DWrite format and layout
// ---------------------------------------------------------------------------

void ITextNode::ensure_format() {
    if (fmt) return;
    fmt = CANVAS.make_text_format(font_family.c_str(), font_size, bold, italic_val, wrap);
    if (!fmt) return;
    fmt->SetTextAlignment(dwrite_alignment(text_align_val));
    DWRITE_PARAGRAPH_ALIGNMENT pa = (vertical_center && !scrollbar_enabled)
        ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    fmt->SetParagraphAlignment(pa);
}

ComPtr<IDWriteTextLayout> ITextNode::make_layout(float max_w, float max_h) const {
    if (!fmt || content.empty()) return {};
    return CANVAS.make_text_layout(
        content.c_str(), static_cast<uint32_t>(content.size()),
        fmt.Get(), max_w, max_h);
}

// ---------------------------------------------------------------------------
// Layout — measure & arrange
// ---------------------------------------------------------------------------

void ITextNode::measure(float avail_w, float avail_h) {
    if (can_skip_measure(avail_w, avail_h)) return;

    measure_self_size(avail_w, avail_h);
    ensure_format();

    if (content.empty() || !fmt) {
        content_height_ = 0.f;
        if (layout_width().is_auto() && !wrap)
            rect.w = layout_padding().horizontal() + (scrollbar_enabled ? scrollbar_width() : 0.f);
        if (layout_height().is_auto())
            rect.h = font_size + layout_padding().vertical();
    }
    else {
        const float layout_w = wrap ? text_inner_w() : 1e6f;
        if (ComPtr<IDWriteTextLayout> layout = make_layout(layout_w, 1e6f)) {
            DWRITE_TEXT_METRICS m{};
            layout->GetMetrics(&m);
            content_height_ = m.height;
            if (layout_width().is_auto() && !wrap)
                rect.w = m.widthIncludingTrailingWhitespace
                + layout_padding().horizontal()
                + (scrollbar_enabled ? scrollbar_width() : 0.f);
            if (layout_height().is_auto())
                rect.h = m.height + layout_padding().vertical();
        }
    }

    rect.w = std::max(0.f, rect.w);
    rect.h = std::max(0.f, rect.h);
    cached_avail_w_ = avail_w;
    cached_avail_h_ = avail_h;
    props.make_clean();
}

void ITextNode::arrange(float slot_x, float slot_y) {
    const Edges margin = layout_margin();
    rect.x = slot_x + margin.left;
    rect.y = slot_y + margin.top;
}

// ---------------------------------------------------------------------------
// Scrollbar helpers
// ---------------------------------------------------------------------------

float ITextNode::thumb_height() const {
    if (!is_scrollbar_visible()) return 0.f;
    float view_h = inner_h();
    return std::max(20.f, view_h * view_h / content_height_);
}

float ITextNode::thumb_y() const {
    if (!is_scrollbar_visible()) return 0.f;
    float max_s = get_max_scroll();
    if (max_s <= 0.f) return content_y();
    return content_y() + (scroll_offset_y / max_s) * (inner_h() - thumb_height());
}

bool ITextNode::is_in_scrollbar(float mx, float my) const {
    if (!is_scrollbar_visible()) return false;
    float sb_x = content_x() + text_inner_w();
    float sb_y = content_y();
    return mx >= sb_x && mx <= sb_x + scrollbar_width()
        && my >= sb_y && my <= sb_y + inner_h();
}

void ITextNode::clamp_scroll() {
    scroll_offset_y = std::clamp(scroll_offset_y, 0.f, get_max_scroll());
}

void ITextNode::ensure_caret_visible() {
    if (!editable || !has_focus || !is_scrollbar_visible()) return;

    if (content.empty()) { scroll_offset_y = 0.f; return; }

    ensure_format();
    ComPtr<IDWriteTextLayout> layout = make_layout(wrap ? text_inner_w() : 1e6f, 1e6f);
    if (!layout) return;

    float cpx = 0.f, cpy = 0.f;
    DWRITE_HIT_TEST_METRICS h{};
    layout->HitTestTextPosition(static_cast<UINT32>(caret_pos), FALSE, &cpx, &cpy, &h);

    const float caret_bot = cpy + std::max(h.height, font_size);
    const float view_bot = scroll_offset_y + inner_h();

    if (cpy < scroll_offset_y)  scroll_offset_y = cpy;
    else if (caret_bot > view_bot) scroll_offset_y = caret_bot - inner_h();

    clamp_scroll();
}

// ---------------------------------------------------------------------------
// Selection highlight rendering
// ---------------------------------------------------------------------------

void ITextNode::draw_selection(
    const ComPtr<IDWriteTextLayout>& layout, Canvas& canvas, float y_offset) const {
    if (!has_selection() || !layout) return;

    const UINT32 range_start = static_cast<UINT32>(sel_start());
    const UINT32 range_length = static_cast<UINT32>(sel_end() - sel_start());

    UINT32 count = 0;
    layout->HitTestTextRange(range_start, range_length, 0.f, 0.f, nullptr, 0, &count);
    if (count == 0) return;

    std::vector<DWRITE_HIT_TEST_METRICS> ranges(count);
    layout->HitTestTextRange(range_start, range_length, 0.f, 0.f, ranges.data(), count, &count);

    const float ox = content_x();
    const float oy = content_y() - y_offset;
    const Color sel_color{ 0.20f, 0.44f, 0.85f, 0.40f };

    for (UINT32 i = 0; i < count; ++i)
        canvas.fill_rect({ ox + ranges[i].left, oy + ranges[i].top,
                           ranges[i].width,     ranges[i].height }, sel_color);
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void ITextNode::draw(Node& handle, Canvas& canvas) {
    draw_default(canvas);
    ensure_format();
    if (!fmt) return;

    clamp_scroll();

    const float lw = wrap ? text_inner_w() : 1e6f;
    ComPtr<IDWriteTextLayout> layout = content.empty() ? nullptr : make_layout(lw, 1e6f);

    const Rect clip_rect{ content_x(), content_y(), text_inner_w(), inner_h() };
    canvas.push_clip(clip_rect);

    draw_selection(layout, canvas, scroll_offset_y);

    if (!content.empty()) {
        const Rect text_rect{ content_x(), content_y() - scroll_offset_y, text_inner_w(), inner_h() };
        canvas.draw_text(content, fmt.Get(), text_rect, text_color);
    }

    canvas.pop_clip();

    // Caret (drawn outside clip)
    if (editable && has_focus) {
        static constexpr float kBlinkShow = 0.7f;
        static constexpr float kBlinkCycle = 1.5f;
        caret_blink_s += Window::get().delta_time();
        if (caret_blink_s > kBlinkCycle) caret_blink_s = 0.f;

        if (caret_blink_s < kBlinkShow) {
            float cx = content_x(), cy = content_y(), ch = font_size;

            if (content.empty()) {
                if (vertical_center && !scrollbar_enabled)
                    cy += (inner_h() - font_size) * 0.5f;
            }
            else if (layout) {
                float cpx = 0.f, cpy = 0.f;
                DWRITE_HIT_TEST_METRICS hit{};
                layout->HitTestTextPosition(static_cast<UINT32>(caret_pos), FALSE, &cpx, &cpy, &hit);
                cx += cpx;
                cy = content_y() + cpy - scroll_offset_y;
                ch = (hit.height > 0.f) ? hit.height : font_size;
            }

            canvas.draw_line(cx, cy, cx, cy + ch, text_color, 1.0f);
        }
    }

    // Scrollbar
    if (is_scrollbar_visible()) {
        const float sb_x = content_x() + text_inner_w();
        const float sb_y = content_y();
        canvas.fill_rect({ sb_x, sb_y, scrollbar_width(), inner_h() },
                         Color(0.15f, 0.15f, 0.15f, 0.6f));
        canvas.fill_rect({ sb_x, thumb_y(), scrollbar_width(), thumb_height() },
                         Color(0.4f, 0.4f, 0.4f, 0.9f));
    }
}

// ---------------------------------------------------------------------------
// set_caret / delete_selection
// ---------------------------------------------------------------------------

void ITextNode::set_caret(size_t pos, bool extend) {
    caret_pos = pos;
    if (!extend) selection_anchor = pos;
    if (editable && has_focus) ensure_caret_visible();
}

void ITextNode::delete_selection() {
    if (!has_selection()) return;
    const size_t s = sel_start();
    content.erase(s, sel_end() - s);
    caret_pos = selection_anchor = s;
    content_height_ = 0.f;
    text_dirty();
    invalidate_format();
    if (editable && has_focus) ensure_caret_visible();
}

// ---------------------------------------------------------------------------
// Word-boundary helpers
// ---------------------------------------------------------------------------

size_t ITextNode::word_start(size_t pos) const {
    size_t i = pos;
    while (i > 0 && std::iswspace(content[i - 1])) --i;
    while (i > 0 && !std::iswspace(content[i - 1])) --i;
    return i;
}

size_t ITextNode::word_end(size_t pos) const {
    const size_t n = content.size();
    size_t i = pos;
    while (i < n && !std::iswspace(content[i])) ++i;
    while (i < n && std::iswspace(content[i])) ++i;
    return i;
}

// ---------------------------------------------------------------------------
// Click-to-position
// ---------------------------------------------------------------------------

void ITextNode::on_click_position(float lx, float ly, bool extend) {
    ensure_format();
    if (!fmt) return;

    ComPtr<IDWriteTextLayout> layout = make_layout(wrap ? text_inner_w() : 1e6f, 1e6f);
    if (!layout) return;

    BOOL is_trailing = FALSE, is_inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    layout->HitTestPoint(lx, ly + scroll_offset_y, &is_trailing, &is_inside, &m);

    size_t pos = std::min<size_t>(m.textPosition + (is_trailing ? 1u : 0u), content.size());
    set_caret(pos, extend);
}

// ---------------------------------------------------------------------------
// Movement callbacks
// ---------------------------------------------------------------------------

void ITextNode::on_move_left(bool extend) {
    if (!extend && has_selection()) { set_caret(sel_start(), false); return; }
    if (caret_pos > 0) set_caret(caret_pos - 1, extend);
}

void ITextNode::on_move_right(bool extend) {
    if (!extend && has_selection()) { set_caret(sel_end(), false); return; }
    if (caret_pos < content.size()) set_caret(caret_pos + 1, extend);
}

void ITextNode::on_move_word_left(bool extend) { set_caret(word_start(caret_pos), extend); }
void ITextNode::on_move_word_right(bool extend) { set_caret(word_end(caret_pos), extend); }
void ITextNode::on_move_home(bool extend) { set_caret(0, extend); }
void ITextNode::on_move_end(bool extend) { set_caret(content.size(), extend); }

// ---------------------------------------------------------------------------
// Editing callbacks
// ---------------------------------------------------------------------------

void ITextNode::on_input(wchar_t ch) {
    if (user_on_char && !user_on_char(ch)) return;
    if (!std::iswprint(ch) && ch != static_cast<wchar_t>(13)) return;
    if (has_selection()) delete_selection();
    content.insert(content.begin() + static_cast<std::ptrdiff_t>(caret_pos), ch);
    set_caret(caret_pos + 1, false);
    text_dirty();
    invalidate_format();
}

void ITextNode::on_backspace() {
    if (has_selection()) { delete_selection(); return; }
    if (caret_pos == 0) return;
    content.erase(content.begin() + static_cast<std::ptrdiff_t>(caret_pos - 1));
    set_caret(caret_pos - 1, false);
    text_dirty();
    invalidate_format();
}

void ITextNode::on_delete() {
    if (has_selection()) { delete_selection(); return; }
    if (caret_pos < content.size()) {
        content.erase(content.begin() + static_cast<std::ptrdiff_t>(caret_pos));
        text_dirty();
        invalidate_format();
    }
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

void ITextNode::copy_to_clipboard() const {
    if (!has_selection() || !OpenClipboard(nullptr)) return;
    EmptyClipboard();

    const std::wstring text = content.substr(sel_start(), sel_end() - sel_start());
    const size_t byte_count = (text.size() + 1) * sizeof(wchar_t);

    if (HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, byte_count)) {
        if (wchar_t* dst = static_cast<wchar_t*>(GlobalLock(hmem))) {
            std::memcpy(dst, text.c_str(), byte_count);
            GlobalUnlock(hmem);
            if (!SetClipboardData(CF_UNICODETEXT, hmem)) GlobalFree(hmem);
        }
        else {
            GlobalFree(hmem);
        }
    }
    CloseClipboard();
}

void ITextNode::paste_from_clipboard() {
    if (!OpenClipboard(nullptr)) return;

    std::wstring text;
    if (HGLOBAL hmem = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* src = static_cast<const wchar_t*>(GlobalLock(hmem))) {
            text = src;
            GlobalUnlock(hmem);
        }
    }
    CloseClipboard();

    if (text.empty()) return;
    if (has_selection()) delete_selection();
    for (wchar_t ch : text) on_input(ch);
}

// ---------------------------------------------------------------------------
// TextNode public API
// ---------------------------------------------------------------------------

TextNode::TextNode(): Node(nullptr) {
    impl_allocate<ITextNode>();
    ITextNode& n = *handle<ITextNode>();
    n.props.set(Key::Share, 0.f);
    n.props.make_dirty();
    n.invalidate_format();
    n.wire_events(*this);
}

TextNode::TextNode(const std::wstring& initial_content): Node(nullptr) {
    impl_allocate<ITextNode>();
    ITextNode& n = *handle<ITextNode>();
    n.content = initial_content;
    n.props.set(get_key("content"), initial_content);
    n.props.set(Key::Share, 0.f);
    n.props.make_dirty();
    n.invalidate_format();
    n.wire_events(*this);
}

TextNode& TextNode::content(const std::wstring& c) {
    ITextNode& n = *handle<ITextNode>();
    n.content = c;
    n.caret_pos = 0;
    n.content_height_ = 0.f;
    n.scroll_offset_y = 0.f;
    n.props.set(get_key("content"), c);
    n.text_dirty();
    n.invalidate_format();
    return *this;
}

TextNode& TextNode::clear_content() { return content(L""); }
std::wstring& TextNode::content() { return handle<ITextNode>()->content; }

TextNode& TextNode::text_align(TextAlign a) {
    SELF.props.set(Key::TextAlign, static_cast<float>(static_cast<int>(a)));
    SELF.invalidate_format();
    return *this;
}

TextNode& TextNode::scrollbar(bool enable) {
    SELF.scrollbar_enabled = enable;
    SELF.props.set(Key::Scrollbar, enable);
    SELF.props.make_dirty();
    return *this;
}

TextNode& TextNode::center_vertically(bool center) {
    SELF.vertical_center = center;
    SELF.props.set(Key::VerticalCenter, center);
    SELF.invalidate_format();
    return *this;
}

TextNode& TextNode::select_all() {
    SELF.selection_anchor = 0;
    SELF.caret_pos = SELF.content.size();
    return *this;
}

TextNode& TextNode::deselect() {
    SELF.selection_anchor = SELF.caret_pos;
    return *this;
}

std::wstring TextNode::selected_text() const {
    const ITextNode& n = *handle<ITextNode>();
    if (!n.has_selection()) return {};
    return n.content.substr(n.sel_start(), n.sel_end() - n.sel_start());
}

TextNode& TextNode::on_char(std::function<bool(wchar_t ch)> callback) {
    SELF.user_on_char = callback;
    return *this;
}

} // namespace lintel
