#include "itextnode.h"
#include <dwrite.h>
#include <d2d1.h>
#include <cwctype>   // iswprint, iswspace
#include <vector>

namespace lintel {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Map our TextAlign enum to the DWrite equivalent.
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

void ITextNode::sync_style() {
    bool changed = false;

    if (const std::wstring* v = attr.get<std::wstring>(attribs::font_family)) {
        if (*v != font_family) { font_family = *v; changed = true; }
    }
    if (const float* v = attr.get<float>(attribs::font_size)) {
        if (*v != font_size) { font_size = *v; changed = true; }
    }
    if (const Color* v = attr.get<Color>(attribs::text_color)) {
        text_color = *v; // colour changes don't require a format rebuild
    }
    if (const bool* v = attr.get<bool>(attribs::bold)) {
        if (*v != bold) { bold = *v; changed = true; }
    }
    if (const bool* v = attr.get<bool>(attribs::italic)) {
        if (*v != italic_val) { italic_val = *v; changed = true; }
    }
    if (const bool* v = attr.get<bool>(attribs::wrap)) {
        if (*v != wrap) { wrap = *v; changed = true; }
    }

    // text_align is stored as a float in the attribute map (using the
    // underlying integer value) because AttribValue has no enum slot.
    // TextNode::text_align() casts to float before storing.
    if (const float* v = attr.get<float>(attribs::text_align)) {
        TextAlign ta = static_cast<TextAlign>(static_cast<int>(*v));
        if (ta != text_align_val) { text_align_val = ta; changed = true; }
    }

    editable = attr.get_or(attribs::editable, false);

    if (changed)
        invalidate_format();
}

// ---------------------------------------------------------------------------
// Event wiring
// ---------------------------------------------------------------------------

void ITextNode::wire_events(Node& handle) {
    // ── Focus / Blur ──────────────────────────────────────────────────────
    handle.on(Event::Focus, [this] (Node&) {
        has_focus = true;
    });
    handle.on(Event::Blur, [this] (Node&) {
        has_focus = false;
        caret_pos = 0;
        selection_anchor = 0;
        lmb_selecting = false;
    });

    // ── Mouse: click-to-position ──────────────────────────────────────────
    handle.on(Event::MouseDown, [this] (Node& self) {
        lmb_selecting = true;
        on_click_position(self.mouse_x(), self.mouse_y(), modifiers().shift);
    });

    // ── Mouse: drag-extend selection ──────────────────────────────────────
    handle.on(Event::MouseMove, [this] (Node& self) {
        if (!lmb_selecting) return;
        on_click_position(self.mouse_x(), self.mouse_y(), /*extend=*/true);
    });

    handle.on(Event::MouseUp, [this] (Node&) {
        lmb_selecting = false;
    });

    // ── Char: printable-character insertion ───────────────────────────────
    handle.on(Event::Char, [this] (Node&) {
        if (!editable || !has_focus) return;
        on_input(key_char());
    });

    // ── KeyDown: navigation and editing ───────────────────────────────────
    handle.on(Event::KeyDown, [this] (Node&) {
        if (!has_focus) return;

        const bool shift = modifiers().shift;
        const bool ctrl = modifiers().ctrl;

        switch (key_vkey()) {
            case VK_BACK:
                if (editable) on_backspace();
                break;
            case VK_DELETE:
                if (editable) on_delete();
                break;

            case VK_LEFT:
                ctrl ? on_move_word_left(shift) : on_move_left(shift);
                break;
            case VK_RIGHT:
                ctrl ? on_move_word_right(shift) : on_move_right(shift);
                break;

            case VK_HOME:
                on_move_home(shift);
                break;
            case VK_END:
                on_move_end(shift);
                break;

            case 0x41: // 'A'  — select all
                if (ctrl) {
                    selection_anchor = 0;
                    caret_pos = content.size();
                }
                break;

            case 0x43: // 'C'  — copy (works on read-only nodes too)
                if (ctrl) copy_to_clipboard();
                break;

            case 0x58: // 'X'  — cut
                if (ctrl && editable) {
                    copy_to_clipboard();
                    if (has_selection()) delete_selection();
                }
                break;

            case 0x56: // 'V'  — paste
                if (ctrl && editable) paste_from_clipboard();
                break;

            default: break;
        }
    });

    // Make the node focusable so the dispatch layer calls set_focus on
    // mouse-down, which fires the Focus event above.
    handle.focusable(true);
}

// ---------------------------------------------------------------------------
// DWrite format and layout management
// ---------------------------------------------------------------------------
//
// Both methods go through CORE.canvas so no DWrite or D2D API is called
// directly from this translation unit.
//

void ITextNode::ensure_format() {
    if (fmt) return;

    fmt = CORE.canvas.make_text_format(
        font_family.c_str(), font_size, bold, italic_val, wrap);

    if (fmt)
        fmt->SetTextAlignment(dwrite_alignment(text_align_val));
}

ComPtr<IDWriteTextLayout> ITextNode::make_layout(float max_w) const {
    if (!fmt || content.empty()) return {};
    return CORE.canvas.make_text_layout(
        content.c_str(), static_cast<uint32_t>(content.size()),
        fmt.Get(), max_w,
        1e6f); // height — never clamp vertically for hit-testing
}

// ---------------------------------------------------------------------------
// Layout – measure & arrange
// ---------------------------------------------------------------------------

void ITextNode::measure(float avail_w, float avail_h) {
    sync_style();

    rect.w = is_auto(lp.width)
        ? std::max(0.f, avail_w - lp.margin.horizontal())
        : lp.width;
    rect.h = is_auto(lp.height) ? 0.f : lp.height;

    ensure_format();
    if (content.empty() || !fmt) {
        if (is_auto(lp.height)) rect.h = lp.padding.vertical();
        return;
    }

    const float layout_w = (!wrap || is_auto(lp.width)) ? 1e6f : inner_w();
    ComPtr<IDWriteTextLayout> layout = make_layout(layout_w);
    if (layout) {
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        if (is_auto(lp.width))
            rect.w = m.widthIncludingTrailingWhitespace + lp.padding.horizontal();
        if (is_auto(lp.height))
            rect.h = m.height + lp.padding.vertical();
    }
    rect.w = std::max(0.f, rect.w);
    rect.h = std::max(0.f, rect.h);
}

void ITextNode::arrange(float slot_x, float slot_y) {
    rect.x = slot_x + lp.margin.left;
    rect.y = slot_y + lp.margin.top;
}

// ---------------------------------------------------------------------------
// Selection highlight rendering
// ---------------------------------------------------------------------------

void ITextNode::draw_selection(
    const ComPtr<IDWriteTextLayout>& layout, Canvas& canvas) const {
    if (!has_selection() || !layout) return;

    const UINT32 range_start = static_cast<UINT32>(sel_start());
    const UINT32 range_length = static_cast<UINT32>(sel_end() - sel_start());

    // First call: discover how many geometry rectangles the selection spans
    // (may be > 1 when text wraps across multiple lines).
    UINT32 count = 0;
    layout->HitTestTextRange(range_start, range_length,
                             0.f, 0.f, nullptr, 0, &count);
    if (count == 0) return;

    std::vector<DWRITE_HIT_TEST_METRICS> ranges(count);
    layout->HitTestTextRange(range_start, range_length,
                             0.f, 0.f, ranges.data(), count, &count);

    // HitTestTextRange returns coordinates relative to the layout origin,
    // which is placed at (content_x(), content_y()).
    const float ox = content_x();
    const float oy = content_y();

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
// Draw: background → selection → text → caret
// ---------------------------------------------------------------------------

void ITextNode::draw(Node& handle, Canvas& canvas) {
    sync_style();
    draw_default(canvas);

    ensure_format();
    if (!fmt) return;

    // Build one shared layout used for both selection highlight and caret.
    const float lw = inner_w();
    ComPtr<IDWriteTextLayout> layout =
        content.empty() ? ComPtr<IDWriteTextLayout>{} : make_layout(lw);

    // ── selection highlight (behind text) ─────────────────────────────────
    draw_selection(layout, canvas);

    // ── text ──────────────────────────────────────────────────────────────
    if (!content.empty()) {
        // layout_box spans the padded content area in absolute pixel coordinates.
        const Rect text_rect{ content_x(), content_y(), inner_w(), inner_h() };
        canvas.draw_text(content, fmt.Get(), text_rect, text_color);
    }

    // ── caret ─────────────────────────────────────────────────────────────
    if (!editable || !has_focus || !layout) return;

    float caret_px = 0.f, caret_py = 0.f;
    DWRITE_HIT_TEST_METRICS hit{};
    layout->HitTestTextPosition(
        static_cast<UINT32>(caret_pos),
        /*isTrailingHit=*/FALSE,
        &caret_px, &caret_py, &hit);

    const float cx = content_x() + caret_px;
    const float cy = content_y() + caret_py;
    const float ch = (hit.height > 0.f) ? hit.height : font_size;

    canvas.draw_line(cx, cy, cx, cy + ch, text_color, 1.0f);
}

// ---------------------------------------------------------------------------
// set_caret / delete_selection helpers
// ---------------------------------------------------------------------------

void ITextNode::set_caret(size_t pos, bool extend) {
    caret_pos = pos;
    if (!extend) selection_anchor = pos;
}

void ITextNode::delete_selection() {
    if (!has_selection()) return;
    const size_t s = sel_start();
    const size_t e = sel_end();
    content.erase(s, e - s);
    caret_pos = s;
    selection_anchor = s;
    invalidate_format();
}

// ---------------------------------------------------------------------------
// Word-boundary helpers
// ---------------------------------------------------------------------------

size_t ITextNode::word_start(size_t pos) const {
    if (pos == 0) return 0;
    size_t i = pos;
    // Step over any whitespace immediately to the left of pos.
    while (i > 0 && std::iswspace(content[i - 1])) --i;
    // Step over the preceding non-whitespace "word".
    while (i > 0 && !std::iswspace(content[i - 1])) --i;
    return i;
}

size_t ITextNode::word_end(size_t pos) const {
    const size_t n = content.size();
    if (pos >= n) return n;
    size_t i = pos;
    // Step over non-whitespace (the current word).
    while (i < n && !std::iswspace(content[i])) ++i;
    // Step over trailing whitespace so the caret lands at the next word.
    while (i < n && std::iswspace(content[i])) ++i;
    return i;
}

// ---------------------------------------------------------------------------
// Click-to-position
// ---------------------------------------------------------------------------

void ITextNode::on_click_position(float lx, float ly, bool extend) {
    ensure_format();
    if (!fmt) return;

    ComPtr<IDWriteTextLayout> layout = make_layout(inner_w());
    if (!layout) return;

    BOOL is_trailing = FALSE, is_inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    layout->HitTestPoint(lx, ly, &is_trailing, &is_inside, &m);

    // HitTestPoint returns the cluster the click fell nearest to.
    // isTrailingHit == TRUE means the click was in the trailing half of that
    // cluster, so the logical insertion point is after it.
    size_t pos = m.textPosition + (is_trailing ? 1u : 0u);
    pos = std::min(pos, content.size());

    set_caret(pos, extend);
}

// ---------------------------------------------------------------------------
// Movement callbacks
// ---------------------------------------------------------------------------

void ITextNode::on_move_left(bool extend) {
    // Without Shift, if there is a selection collapse it to the left edge.
    if (!extend && has_selection()) { set_caret(sel_start(), false); return; }
    if (caret_pos > 0)
        set_caret(caret_pos - 1, extend);
}

void ITextNode::on_move_right(bool extend) {
    if (!extend && has_selection()) { set_caret(sel_end(), false); return; }
    if (caret_pos < content.size())
        set_caret(caret_pos + 1, extend);
}

void ITextNode::on_move_word_left(bool extend) {
    set_caret(word_start(caret_pos), extend);
}

void ITextNode::on_move_word_right(bool extend) {
    set_caret(word_end(caret_pos), extend);
}

void ITextNode::on_move_home(bool extend) {
    set_caret(0, extend);
}

void ITextNode::on_move_end(bool extend) {
    set_caret(content.size(), extend);
}

// ---------------------------------------------------------------------------
// Editing callbacks
// ---------------------------------------------------------------------------

void ITextNode::on_input(wchar_t ch) {
    // Only printable characters (and CR) actually insert content.
    // The selection must not be erased for control characters such as the
    // WM_CHAR payloads produced by Ctrl+A (0x01), Ctrl+C (0x03), etc. —
    // those are handled in the KeyDown handler and must not reach here as
    // editing operations.
    if (std::iswprint(ch) || ch == static_cast<wchar_t>(13)) {
        // Replace any active selection so that typing over highlighted text
        // works naturally — but only now that we know we will insert something.
        if (has_selection()) delete_selection();
        content.insert(
            content.begin() + static_cast<std::wstring::difference_type>(caret_pos), ch);
        set_caret(caret_pos + 1, /*extend=*/false);
        invalidate_format();
    }
}

void ITextNode::on_backspace() {
    if (has_selection()) { delete_selection(); return; }
    if (caret_pos == 0)  return;
    content.erase(
        content.begin() + static_cast<std::wstring::difference_type>(caret_pos - 1));
    set_caret(caret_pos - 1, false);
    invalidate_format();
}

void ITextNode::on_delete() {
    if (has_selection()) { delete_selection(); return; }
    if (caret_pos >= content.size()) return;
    content.erase(
        content.begin() + static_cast<std::wstring::difference_type>(caret_pos));
    invalidate_format(); // caret_pos stays; anchor stays
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
    if (!hmem) {
        CloseClipboard();
        return;
    }

    if (wchar_t* dst = static_cast<wchar_t*>(GlobalLock(hmem))) {
        std::memcpy(dst, text.c_str(), byte_count);
        GlobalUnlock(hmem);
        // SetClipboardData takes ownership of hmem on success;
        // only free it ourselves if the call fails.
        if (!SetClipboardData(CF_UNICODETEXT, hmem))
            GlobalFree(hmem);
    }
    else {
        GlobalFree(hmem);
    }

    CloseClipboard();
}

void ITextNode::paste_from_clipboard() {
    if (!OpenClipboard(nullptr)) return;

    HGLOBAL hmem = GetClipboardData(CF_UNICODETEXT);
    if (!hmem) {
        CloseClipboard();
        return;
    }

    // Copy into a local wstring while the clipboard is still open and the
    // memory is locked, then release both before touching node state.
    std::wstring text;
    if (const wchar_t* src = static_cast<const wchar_t*>(GlobalLock(hmem))) {
        text = src;
        GlobalUnlock(hmem);
    }

    CloseClipboard();

    if (text.empty()) return;

    // Replace any active selection before inserting.
    if (has_selection()) delete_selection();

    // Insert through on_input so non-printable code points are filtered and
    // format invalidation is handled consistently with normal typing.
    for (wchar_t ch : text)
        on_input(ch);
}

// ---------------------------------------------------------------------------
// TextNode public API
// ---------------------------------------------------------------------------

TextNode::TextNode(): Node(nullptr) {
    impl_allocate<ITextNode>();
    ITextNode& n = *handle<ITextNode>();
    n.lp.share = 0.f;
    n.wire_events(*this);
}

TextNode::TextNode(std::wstring_view initial_content): Node(nullptr) {
    impl_allocate<ITextNode>();
    ITextNode& n = *handle<ITextNode>();
    n.content = initial_content;
    n.lp.share = 0.f;
    n.wire_events(*this);
}

TextNode& TextNode::content(std::wstring_view c) {
    ITextNode& n = *handle<ITextNode>();
    n.content = c;
    n.invalidate_format();
    return *this;
}

TextNode& TextNode::text_align(TextAlign a) {
    // Store as float so it fits in the AttribValue variant; sync_style() casts back.
    handle<ITextNode>()->attr.set(attribs::text_align,
                                  static_cast<float>(static_cast<int>(a)));
    handle<ITextNode>()->invalidate_format();
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
