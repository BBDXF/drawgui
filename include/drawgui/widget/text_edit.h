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

#include <optional>
#include <string>
#include <string_view>

namespace dg {

// A normalized [start, end) byte range into an editor's text.
struct TextSelection {
  int start = 0;
  int end = 0;
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
[[nodiscard]] std::string sanitize_insertable(std::string_view input, bool allow_newline = false);

// Replaces text[lo:hi) (clamped, either order) with `replacement` verbatim,
// moves the cursor to just after it and clears the selection. False only
// when nothing would change (an empty range replaced by nothing).
bool replace_range(TextEditState& state, int lo, int hi, std::string_view replacement);

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
