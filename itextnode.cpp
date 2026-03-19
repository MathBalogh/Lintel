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
    // mouse_x/y on the handler's Node& are layout-relative (content origin),
    // so they can be passed directly to HitTestPoint.
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

            case 0x41: // 'A'
                if (ctrl) {
                    selection_anchor = 0;
                    caret_pos = content.size();
                }
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

void ITextNode::ensure_format() {
    if (fmt || !GPU.dwrite_factory) return;

    GPU.dwrite_factory->CreateTextFormat(
        font_family.c_str(), nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR,
        italic_val ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        font_size, L"", &fmt);

    if (fmt) {
        fmt->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP
                             : DWRITE_WORD_WRAPPING_NO_WRAP);
        fmt->SetTextAlignment(dwrite_alignment(text_align_val));
    }
}

ComPtr<IDWriteTextLayout> ITextNode::make_layout(float max_w) const {
    if (!GPU.dwrite_factory || !fmt || content.empty()) return {};

    ComPtr<IDWriteTextLayout> layout;
    GPU.dwrite_factory->CreateTextLayout(
        content.c_str(), (UINT32) content.size(),
        fmt.Get(), max_w,
        1e6f, // height – never clamp vertically for hit-testing
        &layout);
    return layout;
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

void ITextNode::draw_selection(const ComPtr<IDWriteTextLayout>& layout) const {
    if (!has_selection() || !layout) return;

    const UINT32 range_start = (UINT32) sel_start();
    const UINT32 range_length = (UINT32) (sel_end() - sel_start());

    // First call: discover how many geometry rectangles the selection spans
    // (may be > 1 when text wraps across multiple lines).
    UINT32 count = 0;
    layout->HitTestTextRange(range_start, range_length,
                             0.f, 0.f, nullptr, 0, &count);
    if (count == 0) return;

    std::vector<DWRITE_HIT_TEST_METRICS> ranges(count);
    layout->HitTestTextRange(range_start, range_length,
                             0.f, 0.f, ranges.data(), count, &count);

    ComPtr<ID2D1SolidColorBrush> sel_brush;
    GPU.d2d_context->CreateSolidColorBrush(
        D2D1::ColorF(0.20f, 0.44f, 0.85f, 0.40f), &sel_brush);
    if (!sel_brush) return;

    // HitTestTextRange returns coordinates relative to layout origin.
    // The layout is rendered at (content_x(), content_y()), i.e. the
    // top-left of the padded content area.
    const float ox = content_x();
    const float oy = content_y();

    for (UINT32 i = 0; i < count; ++i) {
        const D2D1_RECT_F sr = D2D1::RectF(
            ox + ranges[i].left,
            oy + ranges[i].top,
            ox + ranges[i].left + ranges[i].width,
            oy + ranges[i].top + ranges[i].height);
        GPU.d2d_context->FillRectangle(sr, sel_brush.Get());
    }
}

// ---------------------------------------------------------------------------
// Draw: background → selection → text → caret
// ---------------------------------------------------------------------------

void ITextNode::draw(Node& handle) {
    sync_style();
    draw_default();

    ensure_format();
    if (!fmt) return;

    // Build one shared layout used for both selection highlight and caret.
    const float lw = inner_w();
    ComPtr<IDWriteTextLayout> layout = content.empty() ? ComPtr<IDWriteTextLayout>{} : make_layout(lw);

    // ── selection highlight (behind text) ─────────────────────────────────
    draw_selection(layout);

    // ── text ──────────────────────────────────────────────────────────────
    if (!content.empty()) {
        auto brush = make_brush(text_color);
        if (brush) {
            const D2D1_RECT_F text_rect = D2D1::RectF(
                content_x(),
                content_y(),
                rect.x + rect.w - lp.padding.right,
                rect.y + rect.h - lp.padding.bottom);
            GPU.d2d_context->DrawText(
                content.c_str(), (UINT32) content.size(),
                fmt.Get(), text_rect, brush.Get());
        }
    }

    // ── caret ─────────────────────────────────────────────────────────────
    if (!editable || !has_focus || !layout) return;

    float caret_px = 0.f, caret_py = 0.f;
    DWRITE_HIT_TEST_METRICS hit{};
    layout->HitTestTextPosition(
        (UINT32) caret_pos,
        /*isTrailingHit=*/FALSE,
        &caret_px, &caret_py, &hit);

    const float cx = content_x() + caret_px;
    const float cy = content_y() + caret_py;
    const float ch = (hit.height > 0.f) ? hit.height : font_size;

    ComPtr<ID2D1SolidColorBrush> caret_brush;
    GPU.d2d_context->CreateSolidColorBrush(
        D2D1::ColorF(text_color.r, text_color.g, text_color.b, text_color.a),
        &caret_brush);
    if (caret_brush) {
        GPU.d2d_context->DrawLine(
            D2D1::Point2F(cx, cy),
            D2D1::Point2F(cx, cy + ch),
            caret_brush.Get(), 1.0f);
    }
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
    // Replace any selection first so typing over a highlight works naturally.
    if (has_selection()) delete_selection();
    if (std::iswprint(ch) || ch == (wchar_t) 13) {
        content.insert(
            content.begin() + (std::wstring::difference_type) caret_pos, ch);
        set_caret(caret_pos + 1, /*extend=*/false);
        invalidate_format();
    }
}

void ITextNode::on_backspace() {
    if (has_selection()) { delete_selection(); return; }
    if (caret_pos == 0)  return;
    content.erase(
        content.begin() + (std::wstring::difference_type) (caret_pos - 1));
    set_caret(caret_pos - 1, false);
    invalidate_format();
}

void ITextNode::on_delete() {
    if (has_selection()) { delete_selection(); return; }
    if (caret_pos >= content.size()) return;
    content.erase(
        content.begin() + (std::wstring::difference_type) caret_pos);
    invalidate_format(); // caret_pos stays; anchor stays
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
