#pragma once
#include "inode.h"

namespace lintel {

/**
 * ITextNode
 * @brief Implementation backing lintel::TextNode.
 *
 * Visual properties are driven by the inherited INode::attr map.  The local
 * fields below act as resolved defaults; sync_style() is called at the start
 * of measure() and draw() to pick up any changes made through Node::attr().
 *
 * ── Input wiring (registered in wire_events(), called from constructors) ──
 *
 *   Event::Focus     – sets has_focus, resets caret blink timer.
 *   Event::Blur      – clears has_focus, caret, and selection.
 *   Event::MouseDown – hit-tests the click position against the DWrite layout
 *                      to move the caret; Shift+click extends the selection.
 *   Event::MouseMove – while LMB is held, extends the selection to the cursor
 *                      position (i.e. click-drag selection).
 *   Event::Char      – inserts a printable character; replaces any selection.
 *   Event::KeyDown   – navigation and editing:
 *
 *       Arrow keys               – move caret one code-unit
 *       Shift + Arrow            – extend / shrink selection
 *       Ctrl  + Left / Right     – jump by word boundary
 *       Ctrl  + Shift + Left / Right – extend selection by word
 *       Home / End               – jump to line start / end
 *       Shift + Home / End       – extend selection to line start / end
 *       Ctrl  + A                – select all
 *       Backspace / Delete       – delete selection or adjacent character
 *
 * Editing is only active when editable == true.  Read-only nodes still
 * respond to Focus, Blur, and all selection / navigation events so that the
 * caller can style a focused read-only field and the user can select text
 * (e.g. for copy, handled by the application layer).
 */
class ITextNode : public INode {
public:
    /* ── resolved appearance (kept in sync with attr map) ────────────────── */
    std::wstring font_family = L"Segoe UI";
    float        font_size = 14.f;
    Color        text_color = Color(1.f, 1.f, 1.f, 1.f);
    TextAlign    text_align_val = TextAlign::Left;
    bool         bold = false;
    bool         italic_val = false;
    bool         wrap = true;
    bool         editable = false;

    /* ── content & editing state ──────────────────────────────────────────── */
    std::wstring content;               // UTF-16 text displayed by this node.
    size_t       caret_pos = 0; // Active end of the selection (0 … size).
    size_t       selection_anchor = 0; // Fixed end of the selection.
    bool         has_focus = false;
    bool         lmb_selecting = false; // True while LMB is held for drag-select.

    /* ── selection helpers ───────────────────────────────────────────────── */

    bool   has_selection() const { return selection_anchor != caret_pos; }
    size_t sel_start()     const { return std::min(selection_anchor, caret_pos); }
    size_t sel_end()       const { return std::max(selection_anchor, caret_pos); }

    /**
     * @brief Move caret to @p pos.
     * @param extend  When true the selection_anchor is not moved, extending
     *                (or shrinking) the selection.  When false both anchor and
     *                caret are set to @p pos, collapsing any selection.
     */
    void set_caret(size_t pos, bool extend);

    /**
     * @brief Delete the selected range and collapse the selection.
     * No-op if has_selection() is false.
     */
    void delete_selection();

    /* ── DirectWrite helpers ──────────────────────────────────────────────── */
    ComPtr<IDWriteTextFormat> fmt;   // Rebuilt lazily when properties change.

    void invalidate_format() { fmt.Reset(); }
    void ensure_format();

    /**
     * @brief Synchronise the attr map into the local resolved fields.
     * Called at the top of measure() and draw().  Invalidates the DWrite
     * format whenever a text-affecting property has actually changed.
     */
    void sync_style();

    /* ── layout ───────────────────────────────────────────────────────────── */
    void measure(float avail_w, float avail_h) override;
    void arrange(float slot_x, float slot_y)   override;

    /* ── rendering ────────────────────────────────────────────────────────── */
    void draw(Node& handle) override;

    /* ── event wiring ─────────────────────────────────────────────────────── */

    /**
     * @brief Register all input handlers on @p handle.
     * Must be called once after the ITextNode has been installed.
     * The TextNode constructors call this automatically.
     */
    void wire_events(Node& handle);

    /* ── editing callbacks (called from the wired handlers) ──────────────── */

    /** Insert @p ch at the caret, replacing any selection. */
    void on_input(wchar_t ch);

    /** Delete the selection, or the character before the caret (Backspace). */
    void on_backspace();

    /** Delete the selection, or the character after the caret (Delete). */
    void on_delete();

    /**
     * @brief Move caret left by one code-unit.
     * @param extend  Extend selection when true (Shift held).
     *
     * When @p extend is false and there is an active selection the caret
     * collapses to the selection start without further movement (matching the
     * behaviour of most text editors).
     */
    void on_move_left(bool extend = false);

    /** Move caret right by one code-unit.  See on_move_left for selection semantics. */
    void on_move_right(bool extend = false);

    /**
     * @brief Jump caret to the nearest word start to the left of caret_pos.
     * Word boundaries are defined as transitions between whitespace and
     * non-whitespace.  Ctrl+Left.
     */
    void on_move_word_left(bool extend = false);

    /**
     * @brief Jump caret to the nearest word end to the right of caret_pos.
     * Ctrl+Right.
     */
    void on_move_word_right(bool extend = false);

    /** Jump caret to the beginning of content.  Home. */
    void on_move_home(bool extend = false);

    /** Jump caret to the end of content.  End. */
    void on_move_end(bool extend = false);

    /**
     * @brief Hit-test a layout-relative point and move the caret there.
     * @param lx / ly  Coordinates relative to the layout origin (i.e. the
     *                 content-area top-left, which is Node::mouse_x/y).
     * @param extend   Extend the selection rather than collapsing it.
     */
    void on_click_position(float lx, float ly, bool extend);

private:
    /* ── word-boundary helpers ────────────────────────────────────────────── */

    /**
     * @brief Return the code-unit index of the word start at or before @p pos.
     * Skips trailing whitespace then non-whitespace in the left direction.
     */
    size_t word_start(size_t pos) const;

    /**
     * @brief Return the code-unit index of the word end at or after @p pos.
     * Skips non-whitespace then whitespace in the right direction.
     */
    size_t word_end(size_t pos) const;

    /**
     * @brief Build a DWriteTextLayout for the current content at @p max_w.
     * Layout alignment is applied from text_align_val so hit-test results are
     * consistent with what is visually rendered.
     */
    ComPtr<IDWriteTextLayout> make_layout(float max_w) const;

    /**
     * @brief Render selection highlight rectangles behind the text.
     * Called from draw() before DrawText.  No-op when has_selection() is false.
     */
    void draw_selection(const ComPtr<IDWriteTextLayout>& layout) const;
};

} // namespace lintel