#include "itextnode.h"
#include "framework.h"
#include <dwrite.h>
#include <d2d1.h>
#include <cwctype>
#include <vector>
#include <algorithm>

namespace lintel {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static DWRITE_TEXT_ALIGNMENT dwrite_alignment(TextAlign a) {
    switch (a) {
        case TextAlign::Center:  return DWRITE_TEXT_ALIGNMENT_CENTER;
        case TextAlign::Right:   return DWRITE_TEXT_ALIGNMENT_TRAILING;
        case TextAlign::Justify: return DWRITE_TEXT_ALIGNMENT_JUSTIFIED;
        default:                 return DWRITE_TEXT_ALIGNMENT_LEADING;
    }
}

// ---------------------------------------------------------------------------
// Style synchronisation
// ---------------------------------------------------------------------------
//
// Reads Prop-keyed values from the attr map into the local cached fields.
// When any property affecting text metrics changes (font, size, weight, wrap),
// attr.layout_dirty is set and the DWrite format is invalidated so that
// ITextNode::measure() recomputes the correct size on the next frame.
//

void ITextNode::sync_style() {
    bool changed = false; // true when a text-metric property changed

    if (const std::wstring* v = attr.get<std::wstring>(property::FontFamily)) {
        if (*v != font_family) { font_family = *v; changed = true; }
    }
    if (const float* v = attr.get<float>(property::FontSize)) {
        if (*v != font_size) { font_size = *v; changed = true; }
    }
    if (const Color* v = attr.get<Color>(property::TextColor)) {
        text_color = *v; // colour change - no re-measure needed
    }
    if (const bool* v = attr.get<bool>(property::Bold)) {
        if (*v != bold) { bold = *v; changed = true; }
    }
    if (const bool* v = attr.get<bool>(property::Italic)) {
        if (*v != italic_val) { italic_val = *v; changed = true; }
    }
    if (const bool* v = attr.get<bool>(property::Wrap)) {
        if (*v != wrap) { wrap = *v; changed = true; }
    }
    // TextAlign is stored as float (integer cast) - same convention as Direction.
    if (const float* v = attr.get<float>(property::TextAlign)) {
        TextAlign ta = static_cast<TextAlign>(static_cast<int>(*v));
        if (ta != text_align_val) { text_align_val = ta; changed = true; }
    }

    // new vertical center & scrollbar
    {
        bool prev = vertical_center;
        vertical_center = attr.get_or<bool>(property::VerticalCenter, false);
        if (prev != vertical_center) invalidate_format();
    }
    {
        bool prev = scrollbar_enabled;
        scrollbar_enabled = attr.get_or<bool>(property::Scrollbar, false);
        if (prev != scrollbar_enabled) attr.layout_dirty = true;
    }

    editable = attr.get_or<bool>(property::Editable, false);

    if (content.empty()) {
        Property p = FRAMEWORK.get_property("content");
        if (auto str = attr.get<std::wstring>(p)) content = *str;
    }

    if (changed) {
        // Text-metric changes affect measured size: force a layout pass and
        // rebuild the DWrite format so the next draw picks up the change.
        attr.layout_dirty = true;
        invalidate_format();
    }
}

void ITextNode::apply_notifier(Property p) {
    // TODO: cache
    if (p == FRAMEWORK.get_property("content")) {
        if (auto* str = attr.get<std::wstring>(p)) {
            content = *str;
            content_height_ = 0.f;
            scroll_offset_y = 0.f;
            attr.layout_dirty = true;
        }
    }
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
        caret_pos = 0;
        selection_anchor = 0;
        lmb_selecting = false;
        scrollbar_dragging = false;
    });

    handle.on(Event::MouseDown, [this] (WeakNode self) {
        float mx = self->mouse_x();
        float my = self->mouse_y();

        if (is_scrollbar_visible() && is_in_scrollbar(mx, my)) {
            float my_rel = my - content_y();
            float th = thumb_height();
            float ty_rel = thumb_y() - content_y();

            if (my_rel >= ty_rel && my_rel < ty_rel + th) {
                // clicked on thumb -> start drag
                scrollbar_dragging = true;
                scrollbar_drag_offset = my_rel - ty_rel;
            }
            else {
                // clicked on track -> jump
                float frac = my_rel / inner_h();
                scroll_offset_y = frac * get_max_scroll();
                clamp_scroll();
            }
            return;
        }

        lmb_selecting = true;
        on_click_position(mx, my, modifiers().shift);
    });

    handle.on(Event::MouseMove, [this] (WeakNode self) {
        if (scrollbar_dragging) {
            float my = self->mouse_y();
            float my_rel = my - content_y();
            float usable = inner_h() - thumb_height();
            if (usable > 0.f) {
                float frac = (my_rel - scrollbar_drag_offset) / usable;
                scroll_offset_y = std::clamp(frac * get_max_scroll(), 0.f, get_max_scroll());
            }
            return;
        }
        if (!lmb_selecting) return;
        on_click_position(self->mouse_x(), self->mouse_y(), /*extend=*/true);
    });

    handle.on(Event::MouseUp, [this] (WeakNode) {
        lmb_selecting = false;
        scrollbar_dragging = false;
    });

    handle.on(Event::Char, [this] (WeakNode) {
        if (!editable || !has_focus) return;
        on_input(key_char());
    });

    handle.on(Event::KeyDown, [this] (WeakNode) {
        if (!has_focus) return;

        const bool shift = modifiers().shift;
        const bool ctrl = modifiers().ctrl;

        switch (key_vkey()) {
            case VK_BACK:   if (editable) on_backspace(); break;
            case VK_DELETE: if (editable) on_delete();    break;

            case VK_LEFT:
                ctrl ? on_move_word_left(shift) : on_move_left(shift);
                break;
            case VK_RIGHT:
                ctrl ? on_move_word_right(shift) : on_move_right(shift);
                break;

            case VK_HOME: on_move_home(shift); break;
            case VK_END:  on_move_end(shift);  break;

            case 0x41: // Ctrl+A - select all
                if (ctrl) { selection_anchor = 0; caret_pos = content.size(); }
                break;
            case 0x43: // Ctrl+C - copy
                if (ctrl) copy_to_clipboard();
                break;
            case 0x58: // Ctrl+X - cut
                if (ctrl && editable) {
                    copy_to_clipboard();
                    if (has_selection()) delete_selection();
                }
                break;
            case 0x56: // Ctrl+V - paste
                if (ctrl && editable) paste_from_clipboard();
                break;

            default: break;
        }
    });

    handle.focusable(true);
}

// ---------------------------------------------------------------------------
// DWrite format and layout management
// ---------------------------------------------------------------------------

void ITextNode::ensure_format() {
    if (fmt) return;

    fmt = CORE.canvas.make_text_format(
        font_family.c_str(), font_size, bold, italic_val, wrap);

    if (fmt) {
        fmt->SetTextAlignment(dwrite_alignment(text_align_val));
        // vertical alignment is CENTER only when explicitly requested AND
        // we are not using a scrollbar (scrollbar forces top alignment)
        DWRITE_PARAGRAPH_ALIGNMENT pa = (vertical_center && !scrollbar_enabled)
            ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
            : DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
        fmt->SetParagraphAlignment(pa);
    }
}

ComPtr<IDWriteTextLayout> ITextNode::make_layout(float max_w, float max_h) const {
    if (!fmt || content.empty()) return {};
    return CORE.canvas.make_text_layout(
        content.c_str(), static_cast<uint32_t>(content.size()),
        fmt.Get(), max_w, max_h);
}

// ---------------------------------------------------------------------------
// Layout - measure & arrange
// ---------------------------------------------------------------------------

void ITextNode::measure(float avail_w, float avail_h) {
    // sync_style may set attr.layout_dirty if a text-metric prop changed.
    // It must run before the early-exit guard so those changes are detected.
    sync_style();

    if (!attr.layout_dirty &&
        avail_w == cached_avail_w_ &&
        avail_h == cached_avail_h_)
        return;

    const Edges margin = layout_margin();

    rect.w = is_auto(layout_width())
        ? std::max(0.f, avail_w - margin.horizontal())
        : layout_width();
    rect.h = is_auto(layout_height()) ? 0.f : layout_height();

    ensure_format();
    if (content.empty() || !fmt) {
        content_height_ = 0.f;
        if (is_auto(layout_height()))
            rect.h = layout_padding().vertical();
        cached_avail_w_ = avail_w;
        cached_avail_h_ = avail_h;
        attr.layout_dirty = false;
        return;
    }

    const float layout_w = wrap ? text_inner_w() : 1e6f;

    ComPtr<IDWriteTextLayout> layout = make_layout(layout_w);
    if (layout) {
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        content_height_ = m.height;

        // Only shrink the node's width when it's a single-line (non-wrapped) auto node.
        // Wrapped auto nodes should fill the available width (standard text-area behavior).
        if (is_auto(layout_width()) && !wrap) {
            rect.w = m.widthIncludingTrailingWhitespace
                + layout_padding().horizontal()
                + (scrollbar_enabled ? scrollbar_width() : 0.f);
        }

        if (is_auto(layout_height()))
            rect.h = m.height + layout_padding().vertical();
    }

    rect.w = std::max(0.f, rect.w);
    rect.h = std::max(0.f, rect.h);

    cached_avail_w_ = avail_w;
    cached_avail_h_ = avail_h;
    attr.layout_dirty = false;
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
    float frac = scroll_offset_y / max_s;
    return content_y() + frac * (inner_h() - thumb_height());
}

bool ITextNode::is_in_scrollbar(float mx, float my) const {
    if (!is_scrollbar_visible()) return false;
    float sb_x = content_x() + text_inner_w();
    float sb_y = content_y();
    float sb_w = scrollbar_width();
    float sb_h = inner_h();
    return mx >= sb_x && mx <= sb_x + sb_w && my >= sb_y && my <= sb_y + sb_h;
}

void ITextNode::clamp_scroll() {
    scroll_offset_y = std::clamp(scroll_offset_y, 0.f, get_max_scroll());
}

void ITextNode::ensure_caret_visible() {
    if (!editable || !has_focus || !is_scrollbar_visible()) return;

    ensure_format();
    float lw = (!wrap) ? 1e6f : text_inner_w();
    ComPtr<IDWriteTextLayout> layout = make_layout(lw, 1e6f);
    if (!layout) return;

    float cpx = 0.f, cpy = 0.f;
    DWRITE_HIT_TEST_METRICS h{};
    layout->HitTestTextPosition(static_cast<UINT32>(caret_pos), FALSE, &cpx, &cpy, &h);

    float caret_top = cpy;
    float caret_bot = cpy + std::max(h.height, font_size);

    float view_top = scroll_offset_y;
    float view_bot = scroll_offset_y + inner_h();

    if (caret_top < view_top) {
        scroll_offset_y = caret_top;
    }
    else if (caret_bot > view_bot) {
        scroll_offset_y = caret_bot - inner_h();
    }
    clamp_scroll();
}

// ---------------------------------------------------------------------------
// Selection highlight rendering
// ---------------------------------------------------------------------------

void ITextNode::draw_selection(const ComPtr<IDWriteTextLayout>& layout, Canvas& canvas, float y_offset) const {
    if (!has_selection() || !layout) return;

    const UINT32 range_start = static_cast<UINT32>(sel_start());
    const UINT32 range_length = static_cast<UINT32>(sel_end() - sel_start());

    UINT32 count = 0;
    layout->HitTestTextRange(range_start, range_length, 0.f, 0.f, nullptr, 0, &count);
    if (count == 0) return;

    std::vector<DWRITE_HIT_TEST_METRICS> ranges(count);
    layout->HitTestTextRange(range_start, range_length,
                             0.f, 0.f, ranges.data(), count, &count);

    const float ox = content_x();
    const float oy = content_y() - y_offset;

    const Color sel_color{ 0.20f, 0.44f, 0.85f, 0.40f };
    for (UINT32 i = 0; i < count; ++i) {
        canvas.fill_rect(
            Rect{ ox + ranges[i].left,
                  oy + ranges[i].top,
                  ranges[i].width,
                  ranges[i].height },
            sel_color);
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void ITextNode::draw(Node& handle, Canvas& canvas) {
    sync_style();
    draw_default(canvas);

    ensure_format();
    if (!fmt) return;

    clamp_scroll();

    const float lw = (!wrap) ? 1e6f : text_inner_w();
    ComPtr<IDWriteTextLayout> layout =
        content.empty() ? ComPtr<IDWriteTextLayout>{} : make_layout(lw, 1e6f);

    // clip all text / selection / caret rendering to the visible content area
    const Rect clip_rect{ content_x(), content_y(), text_inner_w(), inner_h() };
    canvas.push_clip(clip_rect);

    draw_selection(layout, canvas, scroll_offset_y);

    if (!content.empty()) {
        const Rect text_rect{ content_x(), content_y() - scroll_offset_y, text_inner_w(), inner_h() };
        canvas.draw_text(content, fmt.Get(), text_rect, text_color);
    }

    canvas.pop_clip();

    if (!editable || !has_focus || !layout) return;

    // caret (still inside the clip)
    float caret_px = 0.f, caret_py = 0.f;
    DWRITE_HIT_TEST_METRICS hit{};
    layout->HitTestTextPosition(
        static_cast<UINT32>(caret_pos),
        /*isTrailingHit=*/FALSE,
        &caret_px, &caret_py, &hit);

    const float cx = content_x() + caret_px;
    const float cy = content_y() + caret_py - scroll_offset_y;
    const float ch = (hit.height > 0.f) ? hit.height : font_size;
    canvas.draw_line(cx, cy, cx, cy + ch, text_color, 1.0f);

    // scrollbar (drawn outside the text clip)
    if (is_scrollbar_visible()) {
        const float view_h = inner_h();
        const float th = thumb_height();
        const float ty = thumb_y();
        const float sb_x = content_x() + text_inner_w();
        const float sb_y = content_y();

        // track
        canvas.fill_rect(Rect{ sb_x, sb_y, scrollbar_width(), view_h }, Color(0.15f, 0.15f, 0.15f, 0.6f));
        // thumb
        canvas.fill_rect(Rect{ sb_x, ty, scrollbar_width(), th }, Color(0.4f, 0.4f, 0.4f, 0.9f));
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
    const size_t e = sel_end();
    content.erase(s, e - s);
    caret_pos = s;
    selection_anchor = s;
    content_height_ = 0.f;
    attr.layout_dirty = true;
    invalidate_format();
    if (editable && has_focus) ensure_caret_visible();
}

// ---------------------------------------------------------------------------
// Word-boundary helpers
// ---------------------------------------------------------------------------

size_t ITextNode::word_start(size_t pos) const {
    if (pos == 0) return 0;
    size_t i = pos;
    while (i > 0 && std::iswspace(content[i - 1])) --i;
    while (i > 0 && !std::iswspace(content[i - 1])) --i;
    return i;
}

size_t ITextNode::word_end(size_t pos) const {
    const size_t n = content.size();
    if (pos >= n) return n;
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

    const float lw = (!wrap) ? 1e6f : text_inner_w();
    ComPtr<IDWriteTextLayout> layout = make_layout(lw, 1e6f);
    if (!layout) return;

    // adjust hit-test y for current scroll offset
    float layout_ly = ly + scroll_offset_y;

    BOOL is_trailing = FALSE, is_inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    layout->HitTestPoint(lx, layout_ly, &is_trailing, &is_inside, &m);

    size_t pos = m.textPosition + (is_trailing ? 1u : 0u);
    pos = std::min(pos, content.size());
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
    if (std::iswprint(ch) || ch == static_cast<wchar_t>(13)) {
        if (has_selection()) delete_selection();
        content.insert(
            content.begin() + static_cast<std::wstring::difference_type>(caret_pos), ch);
        set_caret(caret_pos + 1, false);
        attr.layout_dirty = true;
        invalidate_format();
    }
}

void ITextNode::on_backspace() {
    if (has_selection()) { delete_selection(); return; }
    if (caret_pos == 0)  return;
    content.erase(content.begin() +
                  static_cast<std::wstring::difference_type>(caret_pos - 1));
    set_caret(caret_pos - 1, false);
    attr.layout_dirty = true;
    invalidate_format();
}

void ITextNode::on_delete() {
    if (has_selection()) { delete_selection(); return; }
    if (caret_pos >= content.size()) return;
    content.erase(content.begin() + static_cast<std::wstring::difference_type>(caret_pos));
    attr.layout_dirty = true;
    invalidate_format();
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

void ITextNode::copy_to_clipboard() const {
    if (!has_selection()) return;

    const std::wstring text = content.substr(sel_start(), sel_end() - sel_start());
    if (!OpenClipboard(nullptr)) return;

    EmptyClipboard();

    const size_t byte_count = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, byte_count);
    if (!hmem) { CloseClipboard(); return; }

    if (wchar_t* dst = static_cast<wchar_t*>(GlobalLock(hmem))) {
        std::memcpy(dst, text.c_str(), byte_count);
        GlobalUnlock(hmem);
        if (!SetClipboardData(CF_UNICODETEXT, hmem)) GlobalFree(hmem);
    }
    else {
        GlobalFree(hmem);
    }

    CloseClipboard();
}

void ITextNode::paste_from_clipboard() {
    if (!OpenClipboard(nullptr)) return;

    HGLOBAL hmem = GetClipboardData(CF_UNICODETEXT);
    if (!hmem) { CloseClipboard(); return; }

    std::wstring text;
    if (const wchar_t* src = static_cast<const wchar_t*>(GlobalLock(hmem))) {
        text = src;
        GlobalUnlock(hmem);
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
    n.attr.set(property::Share, 0.f); // shrink-wrap by default
    n.wire_events(*this);
}

TextNode::TextNode(std::wstring_view initial_content): Node(nullptr) {
    impl_allocate<ITextNode>();
    ITextNode& n = *handle<ITextNode>();
    n.content = initial_content;
    n.attr.set(property::Share, 0.f);
    n.wire_events(*this);
}

TextNode& TextNode::content(std::wstring_view c) {
    ITextNode& n = *handle<ITextNode>();
    n.content = c;
    n.content_height_ = 0.f;
    n.scroll_offset_y = 0.f;
    n.attr.layout_dirty = true;
    n.invalidate_format();
    return *this;
}

TextNode& TextNode::text_align(TextAlign a) {
    // Stored as float (integer cast) - sync_style() casts back to TextAlign.
    handle<ITextNode>()->attr.set(property::TextAlign,
                                  static_cast<float>(static_cast<int>(a)));
    handle<ITextNode>()->invalidate_format();
    return *this;
}

/** Enable / disable the optional vertical scrollbar (shown when text overflows). */
TextNode& TextNode::scrollbar(bool enable) {
    ITextNode& n = *handle<ITextNode>();
    n.scrollbar_enabled = enable;
    n.attr.set(property::Scrollbar, enable);
    n.attr.layout_dirty = true;           // width may change
    return *this;
}

/** Center text vertically inside the node (uses DWrite paragraph alignment). */
TextNode& TextNode::center_vertically(bool center) {
    ITextNode& n = *handle<ITextNode>();
    n.vertical_center = center;
    n.attr.set(property::VerticalCenter, center);
    n.invalidate_format();
    return *this;
}

TextNode& TextNode::select_all() {
    ITextNode& n = *handle<ITextNode>();
    n.selection_anchor = 0;
    n.caret_pos = n.content.size();
    return *this;
}

TextNode& TextNode::deselect() {
    ITextNode& n = *handle<ITextNode>();
    n.selection_anchor = n.caret_pos;
    return *this;
}

std::wstring TextNode::selected_text() const {
    const ITextNode& n = *handle<ITextNode>();
    if (!n.has_selection()) return {};
    return n.content.substr(n.sel_start(), n.sel_end() - n.sel_start());
}

} // namespace lintel
