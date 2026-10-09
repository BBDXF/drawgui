// The render-free editing core shared by every text-editing widget.
//
// Everything here operates on a plain TextEditState - a UTF-8 string plus a
// caret, an optional selection anchor, and the IME composition preview - and
// knows nothing about nodes, fonts or pixels. WidgetSet's kTextField
// functions are thin wrappers: they call one of these, then re-project the
// result onto their child nodes (text_field_refresh_display()). Keeping the
// model operations here is what lets a second editor (a multi-line
// TextArea) reuse the identical grapheme/selection/composition rules rather
// than growing a parallel copy of them.
//
// Every offset is a BYTE offset into `TextEditState::text`. `cursor` and
// `selection_anchor` always land on a grapheme-cluster boundary
// (dg::grapheme_boundaries()): every mutator below only ever places them
// via a boundary query or at the edge of an inserted string, never by raw
// arithmetic.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "drawgui/anim/clock.h"

namespace dg {

// A normalized [start, end) byte range into an editor's text.
struct TextSelection {
  int start = 0;
  int end = 0;
};

// What one undo step restores: the committed model, never the IME preview.
struct TextEditSnapshot {
  std::string text;
  int cursor = 0;
  std::optional<int> selection_anchor;
};

// How an edit is recorded. Consecutive kTyping (or kDeleteBackward, or
// kDeleteForward) edits within TextEditHistory::kMergeWindowMs of each
// other, each starting where the last one left the caret, merge into ONE
// undo step; typing a whitespace after a non-whitespace starts a new step,
// so "hello world" undoes as "hello" + " world". kOther (paste, cut, an IME
// commit, a selection replace, a word delete) is always a step of its own.
enum class TextEditKind : std::uint8_t {
  kTyping,
  kDeleteBackward,
  kDeleteForward,
  kOther,
};

// Undo/redo for one editor. Snapshots are whole-text copies (simple and
// obviously correct; fine at the depth below for form-sized text).
// `now` is the clock seam: the caller stamps it (WidgetSet::set_edit_time(),
// fed by dg::steady_anim_time() in the C ABI, by a fixed AnimTime in tests)
// before an edit, and only differences between stamps are read - no
// function here reads a real clock.
struct TextEditHistory {
  static constexpr std::size_t kMaxDepth = 100;
  static constexpr std::int64_t kMergeWindowMs = 1000;

  std::vector<TextEditSnapshot> undo;
  std::vector<TextEditSnapshot> redo;
  AnimTime now;

  // The merge bookkeeping of the last recorded edit. `merge_open` is cleared
  // by anything that should end a typing run: a caret move, a selection
  // change, undo/redo, a kOther edit.
  bool merge_open = false;
  TextEditKind last_kind = TextEditKind::kOther;
  AnimTime last_time;
  int last_cursor = 0;
  bool last_was_space = false;
};

struct TextEditState {
  // The MODEL. Well-formed UTF-8, sanitized at every mutation entry point
  // (text_edit::insert()).
  std::string text;
  int cursor = 0;                       // byte offset into `text`, in [0, text.size()]
  std::optional<int> selection_anchor;  // set => a selection [min(anchor,cursor), max(...))

  // The IME composition preview - deliberately SEPARATE from `text`/
  // `cursor`/`selection_anchor`: composition never touches any of them
  // until a REAL commit arrives (text_edit::insert()), which is what lets
  // Escape/focus-loss/a click mid-composition simply discard
  // `composition_text` and leave the committed model exactly as it was.
  //
  // While `composing`, `composition_replace_start`/`_end` are the byte range
  // WITHIN `text` the preview virtually sits over (captured ONCE at
  // composition start from whatever selection/cursor was there), and
  // `composition_focus_start`/`_length` are byte offsets WITHIN
  // `composition_text` (SDL_TextEditingEvent's own start/length, converted
  // to bytes and clamped defensively).
  bool composing = false;
  std::string composition_text;
  int composition_focus_start = 0;
  int composition_focus_length = 0;
  int composition_replace_start = 0;
  int composition_replace_end = 0;

  // The x position vertical caret movement tries to keep across lines. Not
  // consumed by any single-line editor; nothing here sets it.
  std::optional<float> preferred_x;

  TextEditHistory history;

  // The most GRAPHEME CLUSTERS (user-perceived characters, not bytes) the
  // committed text may hold; 0 means unlimited. Enforced by insert() (typing,
  // paste, an IME commit), which truncates the inserted string at a cluster
  // boundary to what still fits; the preview of an in-progress composition
  // is not limited. Counted per piece, so a combining mark that would join
  // the cluster before the caret still counts as one.
  int max_graphemes = 0;
};

namespace text_edit {

// [min(a, b), max(a, b)).
[[nodiscard]] TextSelection normalize(int cursor, int anchor);

// The non-empty selection, or nullopt when there is none (no anchor, or an
// anchor sitting on the cursor).
[[nodiscard]] std::optional<TextSelection> selection(const TextEditState& state);

// Repairs malformed UTF-8 to U+FFFD (dg::sanitize_utf8()) and drops ASCII
// control characters (0x00-0x1F) and DEL. `allow_newline` keeps '\n' for a
// multi-line editor; a single-line one passes false.
[[nodiscard]] std::string sanitize_insertable(std::string_view input,
                                              bool allow_newline = false);

// Replaces text[lo:hi) (clamped, either order) with `replacement` verbatim,
// moves the cursor to just after it and clears the selection. False only
// when nothing would change (an empty range replaced by nothing). Every
// text mutation in this file goes through here, which records the undo
// step (as `kind`, see TextEditKind) and clears the redo stack.
bool replace_range(TextEditState& state, int lo, int hi, std::string_view replacement,
                   TextEditKind kind = TextEditKind::kOther);

// A REAL commit: ends any composition preview, sanitizes `input`, and
// replaces the selection (or inserts at the cursor). False when nothing
// changed.
bool insert(TextEditState& state, std::string_view input, bool allow_newline = false);

// Delete the selection, or one grapheme cluster before/after the cursor.
// Suppressed (false) while composing - the IME owns those keys then.
bool backspace(TextEditState& state);
bool delete_forward(TextEditState& state);

// One grapheme step left/right. A non-extending step with an active
// selection collapses to the selection's near edge instead. Suppressed
// while composing.
bool move_horizontal(TextEditState& state, bool left, bool extend);

// Places the cursor at `offset` (which the caller guarantees is a grapheme
// boundary), either extending the selection from the old cursor or
// clearing it. Does NOT consult `composing` - callers decide that. False
// when neither the cursor nor the anchor changed.
bool set_cursor(TextEditState& state, int offset, bool extend);

// Selects the whole text. Suppressed while composing; false when the text is
// empty or already fully selected.
bool select_all(TextEditState& state);

// Sets the selection to [anchor, cursor) exactly (a collapsed pair clears
// it). Both must be grapheme boundaries. False when nothing changed.
bool select_range(TextEditState& state, int anchor, int cursor);

// --- Words and lines ---
//
// A render-free word segmentation over grapheme clusters, classified by the
// cluster's first codepoint: whitespace, newline, punctuation/symbols, CJK
// (Han, kana - a run of them is ONE word: there is no dictionary
// segmentation), emoji (every emoji cluster is a word of its own) and
// everything else (letters, digits, '_'). The same rules drive Ctrl+Left/
// Right, Ctrl+Backspace/Delete and double-click selection.

// The offset Ctrl+Left/Right moves to from `offset`: skip whitespace and
// newlines, then one run of a single class.
[[nodiscard]] int word_left(std::string_view text, int offset);
[[nodiscard]] int word_right(std::string_view text, int offset);

// The run of one class under `offset` (what a double-click selects). At the
// end of a run that is followed by whitespace (or at the end of the text)
// the run BEFORE the offset wins, so a click on a word's right half still
// picks the word. Empty (start == end) on a newline or an empty text.
[[nodiscard]] TextSelection word_range(std::string_view text, int offset);

// The logical line (between '\n's, the newline itself excluded) holding
// `offset` - what a triple-click in a multi-line editor selects.
[[nodiscard]] TextSelection line_range(std::string_view text, int offset);

// Ctrl+Left/Right. Suppressed while composing.
bool move_word(TextEditState& state, bool left, bool extend);

// Ctrl+Backspace (`backward`) / Ctrl+Delete: deletes the selection if any,
// otherwise up to word_left()/word_right(). One undo step each. Suppressed
// while composing.
bool delete_word(TextEditState& state, bool backward);

// --- Undo/redo ---

// Restore the previous / re-apply the undone snapshot. Suppressed while
// composing (the IME owns the keys then); false when there is nothing to
// undo/redo.
bool undo(TextEditState& state);
bool redo(TextEditState& state);

// Forgets every undo/redo step (a programmatic set_text).
void clear_history(TextEditState& state);

// Starts or updates the composition preview. `start_units`/`length_units`
// are the IME's focus range in codepoints within `text`. An empty (after
// sanitizing) `text` ends composition without committing, exactly like
// cancel_composition(). Returns whether the display needs refreshing.
bool composition_update(TextEditState& state, std::string_view text, int start_units,
                        int length_units);

// Discards the composition preview. False when nothing was composing.
bool cancel_composition(TextEditState& state);

// What an editor should DISPLAY while focused: the committed text, or (while
// composing) the text with the preview spliced in. `highlight` is the
// user's selection OR the IME's focused clause - mutually exclusive by
// construction; `underline` spans the whole preview and is only set while
// composing.
struct Projection {
  std::string text;
  int cursor = 0;
  std::optional<TextSelection> highlight;
  std::optional<TextSelection> underline;
};

[[nodiscard]] Projection display_projection(const TextEditState& state);

}  // namespace text_edit

}  // namespace dg
