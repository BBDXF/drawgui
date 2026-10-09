#include "drawgui/widget/text_edit.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "drawgui/base/utf8.h"
#include "drawgui/render/grapheme.h"

namespace dg::text_edit {
namespace {

// One grapheme-cluster step from `from`, which MUST already be a boundary
// in `boundaries` (TextEditState's own invariant). Left/right unified into
// one function via `left`, matching design.md's "the minimum unit is the
// grapheme cluster, not the byte" mandate for both directions at once.
int grapheme_step(const std::vector<int>& boundaries, int from, bool left) {
  const auto it = std::lower_bound(boundaries.begin(), boundaries.end(), from);
  if (it == boundaries.end() || *it != from) {
    return from;  // defensive: `from` was not on a boundary - should not happen
  }
  if (left) {
    return it == boundaries.begin() ? from : *(it - 1);
  }
  const auto next = it + 1;
  return next == boundaries.end() ? from : *next;
}

// The byte range within a composition's own preedit string that the IME's
// start/length units resolve to.
struct CompositionFocusBytes {
  int start = 0;
  int end = 0;
};

// Converts SDL_TextEditingEvent's own start/length ("UTF-8 characters")
// into a byte range within `text` (assumed well-formed - the caller already
// sanitized it) by walking codepoints via dg::utf8_decode(). `int64_t`
// accumulators rather than `int` ones: a hostile or merely buggy IME's
// start/length are exactly the "absurd cursor offset" -DDG_SANITIZE=ON is
// meant to catch, and `start_units + length_units` computed directly in
// `int` could overflow. Both ends are clamped to `text.size()` by the walk
// itself stopping there, never by trusting the input.
CompositionFocusBytes composition_focus_bytes(std::string_view text, int start_units,
                                              int length_units) {
  const std::int64_t start = std::max<std::int64_t>(0, start_units);
  const std::int64_t end = start + std::max<std::int64_t>(0, length_units);

  CompositionFocusBytes result;
  std::size_t byte = 0;
  std::int64_t index = 0;
  bool start_found = false;
  while (true) {
    if (!start_found && index >= start) {
      result.start = static_cast<int>(byte);
      start_found = true;
    }
    if (start_found && index >= end) {
      result.end = static_cast<int>(byte);
      return result;
    }
    if (byte >= text.size()) {
      break;
    }
    const Utf8Step step = utf8_decode(text, byte);
    byte += step.length;
    ++index;
  }
  if (!start_found) {
    result.start = static_cast<int>(text.size());
  }
  result.end = static_cast<int>(text.size());
  return result;
}

void reset_composition(TextEditState& state) {
  state.composing = false;
  state.composition_text.clear();
  state.composition_focus_start = 0;
  state.composition_focus_length = 0;
  state.composition_replace_start = 0;
  state.composition_replace_end = 0;
}

// Deletes the anchored range when an anchor is set (even a collapsed one,
// which deletes nothing and reports false), otherwise [lo, hi).
bool delete_selection_or(TextEditState& state, int lo, int hi) {
  if (state.selection_anchor.has_value()) {
    const TextSelection range = normalize(state.cursor, *state.selection_anchor);
    return replace_range(state, range.start, range.end, "");
  }
  return replace_range(state, lo, hi, "");
}

}  // namespace

TextSelection normalize(int cursor, int anchor) {
  return TextSelection{std::min(cursor, anchor), std::max(cursor, anchor)};
}

std::optional<TextSelection> selection(const TextEditState& state) {
  if (!state.selection_anchor.has_value() || *state.selection_anchor == state.cursor) {
    return std::nullopt;
  }
  return normalize(state.cursor, *state.selection_anchor);
}

// Stripping control bytes AFTER sanitize_utf8() (rather than folding both
// into one pass) is safe because well-formed UTF-8 guarantees every byte
// below 0x80 is a complete one-byte codepoint on its own, never a
// continuation byte of something else.
std::string sanitize_insertable(std::string_view input, bool allow_newline) {
  const std::string well_formed = sanitize_utf8(input);
  std::string out;
  out.reserve(well_formed.size());
  for (const char byte : well_formed) {
    const auto value = static_cast<unsigned char>(byte);
    const bool kept_newline = allow_newline && byte == '\n';
    if ((value < 0x20 && !kept_newline) || value == 0x7F) {
      continue;
    }
    out.push_back(byte);
  }
  return out;
}

bool replace_range(TextEditState& state, int lo, int hi, std::string_view replacement) {
  const int size = static_cast<int>(state.text.size());
  const int start = std::clamp(std::min(lo, hi), 0, size);
  const int end = std::clamp(std::max(lo, hi), 0, size);
  if (start == end && replacement.empty()) {
    return false;
  }
  state.text.replace(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start),
                     replacement);
  state.cursor = start + static_cast<int>(replacement.size());
  state.selection_anchor.reset();
  return true;
}

bool insert(TextEditState& state, std::string_view input, bool allow_newline) {
  // `cursor`/`selection_anchor` were never touched while composing, so
  // discarding the stale preview and falling through inserts/replaces at
  // exactly the range that was there when composition began.
  if (state.composing) {
    reset_composition(state);
  }
  const std::string sanitized = sanitize_insertable(input, allow_newline);
  if (state.selection_anchor.has_value()) {
    const TextSelection range = normalize(state.cursor, *state.selection_anchor);
    return replace_range(state, range.start, range.end, sanitized);
  }
  if (sanitized.empty()) {
    return false;
  }
  return replace_range(state, state.cursor, state.cursor, sanitized);
}

bool backspace(TextEditState& state) {
  if (state.composing) {
    return false;
  }
  if (!state.selection_anchor.has_value() && state.cursor <= 0) {
    return false;
  }
  const int start = state.selection_anchor.has_value()
                        ? state.cursor
                        : grapheme_step(grapheme_boundaries(state.text), state.cursor, true);
  return delete_selection_or(state, start, state.cursor);
}

bool delete_forward(TextEditState& state) {
  if (state.composing) {
    return false;
  }
  if (!state.selection_anchor.has_value() &&
      state.cursor >= static_cast<int>(state.text.size())) {
    return false;
  }
  const int end = state.selection_anchor.has_value()
                      ? state.cursor
                      : grapheme_step(grapheme_boundaries(state.text), state.cursor, false);
  return delete_selection_or(state, state.cursor, end);
}

bool move_horizontal(TextEditState& state, bool left, bool extend) {
  if (state.composing) {
    return false;
  }
  // Plain (non-extending) Left/Right with an active selection collapses to
  // the selection's near edge rather than moving from the cursor - matching
  // every desktop toolkit.
  const bool collapsing = !extend && state.selection_anchor.has_value();
  int target = 0;
  if (collapsing) {
    const TextSelection range = normalize(state.cursor, *state.selection_anchor);
    target = left ? range.start : range.end;
  } else {
    target = grapheme_step(grapheme_boundaries(state.text), state.cursor, left);
  }
  return set_cursor(state, target, extend);
}

bool set_cursor(TextEditState& state, int offset, bool extend) {
  const int old_cursor = state.cursor;
  const std::optional<int> old_anchor = state.selection_anchor;
  if (extend) {
    if (!old_anchor.has_value()) {
      state.selection_anchor = old_cursor;
    }
  } else {
    state.selection_anchor.reset();
  }
  state.cursor = offset;
  return state.cursor != old_cursor || state.selection_anchor != old_anchor;
}

bool select_all(TextEditState& state) {
  if (state.composing) {
    return false;
  }
  const int size = static_cast<int>(state.text.size());
  if (size == 0) {
    return false;
  }
  if (state.selection_anchor == 0 && state.cursor == size) {
    return false;
  }
  state.selection_anchor = 0;
  state.cursor = size;
  return true;
}

bool composition_update(TextEditState& state, std::string_view text, int start_units,
                        int length_units) {
  const std::string sanitized = sanitize_insertable(text);
  if (sanitized.empty()) {
    // SDL's own convention: an empty preedit means composition ended
    // WITHOUT a commit - a real commit is always a separate, later insert().
    return cancel_composition(state);
  }
  if (!state.composing) {
    // Composition START: capture the replace range ONCE from whatever
    // selection/cursor the model already has - never re-derived while
    // composing, so a later real commit replaces exactly this range.
    const std::optional<TextSelection> range = selection(state);
    state.composition_replace_start = range ? range->start : state.cursor;
    state.composition_replace_end = range ? range->end : state.cursor;
    state.composing = true;
  }
  const CompositionFocusBytes bytes =
      composition_focus_bytes(sanitized, start_units, length_units);
  state.composition_text = sanitized;
  state.composition_focus_start = bytes.start;
  state.composition_focus_length = bytes.end - bytes.start;
  return true;
}

bool cancel_composition(TextEditState& state) {
  if (!state.composing) {
    return false;
  }
  reset_composition(state);
  return true;
}

Projection display_projection(const TextEditState& state) {
  if (!state.composing) {
    return Projection{state.text, state.cursor, selection(state), std::nullopt};
  }
  // While composing the DISPLAY splices the not-yet-committed preedit into
  // `text` at the point composition began - the model itself is only read.
  Projection projection;
  projection.text =
      state.text.substr(0, static_cast<std::size_t>(state.composition_replace_start)) +
      state.composition_text +
      state.text.substr(static_cast<std::size_t>(state.composition_replace_end));
  projection.cursor = state.composition_replace_start + state.composition_focus_start;
  if (state.composition_focus_length > 0) {
    const int start = state.composition_replace_start + state.composition_focus_start;
    projection.highlight = TextSelection{start, start + state.composition_focus_length};
  }
  projection.underline = TextSelection{
      state.composition_replace_start,
      state.composition_replace_start + static_cast<int>(state.composition_text.size())};
  return projection;
}

}  // namespace dg::text_edit
