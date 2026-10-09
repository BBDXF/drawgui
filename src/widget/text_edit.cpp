#include "drawgui/widget/text_edit.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
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
// which deletes nothing and reports false), otherwise [lo, hi) recorded as
// `kind`.
bool delete_selection_or(TextEditState& state, int lo, int hi, TextEditKind kind) {
  if (state.selection_anchor.has_value()) {
    const TextSelection range = normalize(state.cursor, *state.selection_anchor);
    return replace_range(state, range.start, range.end, "");
  }
  return replace_range(state, lo, hi, "", kind);
}

TextEditSnapshot snapshot_of(const TextEditState& state) {
  return TextEditSnapshot{state.text, state.cursor, state.selection_anchor};
}

void push_capped(std::vector<TextEditSnapshot>& stack, TextEditSnapshot snapshot) {
  stack.push_back(std::move(snapshot));
  if (stack.size() > TextEditHistory::kMaxDepth) {
    stack.erase(stack.begin());
  }
}

bool is_space_codepoint(char32_t cp) {
  return cp == U' ' || cp == U'\t' || cp == U'\u00A0' || cp == U'\u3000' ||
         (cp >= U'\u2000' && cp <= U'\u200A');
}

// Records the undo step `replace_range()` is about to make, merging it into
// the previous one when TextEditKind's rules allow.
void record_edit(TextEditState& state, TextEditKind kind, std::string_view inserted) {
  TextEditHistory& history = state.history;
  const bool space =
      !inserted.empty() && is_space_codepoint(utf8_decode(inserted, 0).codepoint);
  const std::int64_t elapsed = history.now.ms - history.last_time.ms;
  // Typing a space after a word starts a new step.
  const bool word_break = kind == TextEditKind::kTyping && space && !history.last_was_space;
  const bool merge = kind != TextEditKind::kOther && history.merge_open &&
                     kind == history.last_kind && !history.undo.empty() &&
                     state.cursor == history.last_cursor &&
                     !state.selection_anchor.has_value() && elapsed >= 0 &&
                     elapsed <= TextEditHistory::kMergeWindowMs && !word_break;
  if (!merge) {
    push_capped(history.undo, snapshot_of(state));
  }
  history.redo.clear();
  history.last_kind = kind;
  history.last_time = history.now;
  history.last_was_space = space;
  history.merge_open = kind != TextEditKind::kOther;
}

enum class WordClass : std::uint8_t { kSpace, kNewline, kPunct, kCjk, kEmoji, kWord };

bool in(char32_t cp, char32_t lo, char32_t hi) {
  return cp >= lo && cp <= hi;
}

WordClass classify(char32_t cp) {
  if (cp == U'\n') {
    return WordClass::kNewline;
  }
  if (is_space_codepoint(cp)) {
    return WordClass::kSpace;
  }
  if (cp < 0x80) {
    const bool alnum = in(cp, U'0', U'9') || in(cp, U'a', U'z') || in(cp, U'A', U'Z');
    return (alnum || cp == U'_') ? WordClass::kWord : WordClass::kPunct;
  }
  if (in(cp, 0xA1, 0xBF) || cp == 0xD7 || cp == 0xF7 || in(cp, 0x2010, 0x205E) ||
      in(cp, 0x3001, 0x303F) || in(cp, 0xFF01, 0xFF0F) || in(cp, 0xFF1A, 0xFF20) ||
      in(cp, 0xFF3B, 0xFF40) || in(cp, 0xFF5B, 0xFF65)) {
    return WordClass::kPunct;
  }
  if (in(cp, 0x1F000, 0x1FAFF) || in(cp, 0x2600, 0x27BF) || in(cp, 0x2B00, 0x2BFF)) {
    return WordClass::kEmoji;
  }
  if (in(cp, 0x3040, 0x30FF) || in(cp, 0x3400, 0x4DBF) || in(cp, 0x4E00, 0x9FFF) ||
      in(cp, 0xF900, 0xFAFF) || in(cp, 0x20000, 0x3FFFF)) {
    return WordClass::kCjk;
  }
  return WordClass::kWord;
}

bool is_blank(WordClass c) {
  return c == WordClass::kSpace || c == WordClass::kNewline;
}

// The text's grapheme boundaries and one class per cluster.
struct Segments {
  std::vector<int> bounds;
  std::vector<WordClass> classes;  // classes[i] is the cluster [bounds[i], bounds[i+1])

  // The cluster index starting at `offset` (clamped; size() at the end).
  [[nodiscard]] std::size_t index_of(int offset) const {
    const auto it = std::lower_bound(bounds.begin(), bounds.end(), offset);
    return static_cast<std::size_t>(it - bounds.begin());
  }
};

Segments segment(std::string_view text) {
  Segments out;
  out.bounds = grapheme_boundaries(text);
  for (std::size_t i = 0; i + 1 < out.bounds.size(); ++i) {
    out.classes.push_back(
        classify(utf8_decode(text, static_cast<std::size_t>(out.bounds[i])).codepoint));
  }
  return out;
}

// One run of `cls` starting just before (left) / at (right) cluster `i`.
// Emoji never form runs.
std::size_t run_left(const Segments& seg, std::size_t i) {
  const WordClass cls = seg.classes[i - 1];
  --i;
  if (cls == WordClass::kEmoji) {
    return i;
  }
  while (i > 0 && seg.classes[i - 1] == cls) {
    --i;
  }
  return i;
}

std::size_t run_right(const Segments& seg, std::size_t i) {
  const WordClass cls = seg.classes[i];
  ++i;
  if (cls == WordClass::kEmoji) {
    return i;
  }
  while (i < seg.classes.size() && seg.classes[i] == cls) {
    ++i;
  }
  return i;
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

bool replace_range(TextEditState& state, int lo, int hi, std::string_view replacement,
                   TextEditKind kind) {
  const int size = static_cast<int>(state.text.size());
  const int start = std::clamp(std::min(lo, hi), 0, size);
  const int end = std::clamp(std::max(lo, hi), 0, size);
  if (start == end && replacement.empty()) {
    return false;
  }
  record_edit(state, kind, replacement);
  state.text.replace(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start),
                     replacement);
  state.cursor = start + static_cast<int>(replacement.size());
  state.selection_anchor.reset();
  state.history.last_cursor = state.cursor;
  return true;
}

bool insert(TextEditState& state, std::string_view input, bool allow_newline) {
  // `cursor`/`selection_anchor` were never touched while composing, so
  // discarding the stale preview and falling through inserts/replaces at
  // exactly the range that was there when composition began.
  const bool committing = state.composing;
  if (committing) {
    reset_composition(state);
  }
  std::string sanitized = sanitize_insertable(input, allow_newline);
  const std::optional<TextSelection> range =
      state.selection_anchor.has_value()
          ? std::optional(normalize(state.cursor, *state.selection_anchor))
          : std::nullopt;
  if (state.max_graphemes > 0) {
    const auto clusters = [](std::string_view piece) {
      return static_cast<int>(grapheme_boundaries(piece).size()) - 1;
    };
    const std::string_view text(state.text);
    const int kept = range.has_value()
                         ? clusters(text.substr(0, static_cast<std::size_t>(range->start))) +
                               clusters(text.substr(static_cast<std::size_t>(range->end)))
                         : clusters(text);
    const int room = std::max(0, state.max_graphemes - kept);
    const std::vector<int> bounds = grapheme_boundaries(sanitized);
    if (static_cast<int>(bounds.size()) - 1 > room) {
      sanitized.resize(static_cast<std::size_t>(bounds[static_cast<std::size_t>(room)]));
    }
  }
  if (range.has_value()) {
    return replace_range(state, range->start, range->end, sanitized);
  }
  if (sanitized.empty()) {
    return false;
  }
  const bool one_cluster = grapheme_boundaries(sanitized).size() == 2;
  return replace_range(
      state, state.cursor, state.cursor, sanitized,
      (one_cluster && !committing) ? TextEditKind::kTyping : TextEditKind::kOther);
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
  return delete_selection_or(state, start, state.cursor, TextEditKind::kDeleteBackward);
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
  return delete_selection_or(state, state.cursor, end, TextEditKind::kDeleteForward);
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
  const bool changed = state.cursor != old_cursor || state.selection_anchor != old_anchor;
  if (changed) {
    state.history.merge_open = false;
  }
  return changed;
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
  state.history.merge_open = false;
  return true;
}

bool select_range(TextEditState& state, int anchor, int cursor) {
  const std::optional<int> new_anchor =
      anchor == cursor ? std::nullopt : std::optional<int>(anchor);
  if (state.cursor == cursor && state.selection_anchor == new_anchor) {
    return false;
  }
  state.cursor = cursor;
  state.selection_anchor = new_anchor;
  state.history.merge_open = false;
  return true;
}

int word_left(std::string_view text, int offset) {
  const Segments seg = segment(text);
  std::size_t i = std::min(seg.index_of(offset), seg.classes.size());
  while (i > 0 && is_blank(seg.classes[i - 1])) {
    --i;
  }
  if (i > 0) {
    i = run_left(seg, i);
  }
  return seg.bounds[i];
}

int word_right(std::string_view text, int offset) {
  const Segments seg = segment(text);
  std::size_t i = std::min(seg.index_of(offset), seg.classes.size());
  while (i < seg.classes.size() && is_blank(seg.classes[i])) {
    ++i;
  }
  if (i < seg.classes.size()) {
    i = run_right(seg, i);
  }
  return seg.bounds[i];
}

TextSelection word_range(std::string_view text, int offset) {
  const Segments seg = segment(text);
  if (seg.classes.empty()) {
    return TextSelection{0, 0};
  }
  std::size_t i = std::min(seg.index_of(offset), seg.classes.size());
  const bool at_end = i == seg.classes.size();
  if (i > 0 && !is_blank(seg.classes[i - 1]) && (at_end || is_blank(seg.classes[i]))) {
    --i;
  }
  if (i == seg.classes.size() || seg.classes[i] == WordClass::kNewline) {
    const int at = seg.bounds[std::min(i, seg.classes.size())];
    return TextSelection{at, at};
  }
  const std::size_t end = run_right(seg, i);
  std::size_t start = i;
  if (seg.classes[i] != WordClass::kEmoji) {
    while (start > 0 && seg.classes[start - 1] == seg.classes[i]) {
      --start;
    }
  }
  return TextSelection{seg.bounds[start], seg.bounds[end]};
}

TextSelection line_range(std::string_view text, int offset) {
  const std::size_t at = std::min(static_cast<std::size_t>(std::max(0, offset)), text.size());
  const std::size_t before = at == 0 ? std::string_view::npos : text.rfind('\n', at - 1);
  const std::size_t start = before == std::string_view::npos ? 0 : before + 1;
  const std::size_t after = text.find('\n', at);
  const std::size_t end = after == std::string_view::npos ? text.size() : after;
  return TextSelection{static_cast<int>(start), static_cast<int>(end)};
}

bool move_word(TextEditState& state, bool left, bool extend) {
  if (state.composing) {
    return false;
  }
  const int target =
      left ? word_left(state.text, state.cursor) : word_right(state.text, state.cursor);
  return set_cursor(state, target, extend);
}

bool delete_word(TextEditState& state, bool backward) {
  if (state.composing) {
    return false;
  }
  if (const std::optional<TextSelection> range = selection(state)) {
    return replace_range(state, range->start, range->end, "");
  }
  state.selection_anchor.reset();
  const int other =
      backward ? word_left(state.text, state.cursor) : word_right(state.text, state.cursor);
  return replace_range(state, other, state.cursor, "");
}

namespace {

bool step_history(TextEditState& state, std::vector<TextEditSnapshot>& from,
                  std::vector<TextEditSnapshot>& to) {
  if (state.composing || from.empty()) {
    return false;
  }
  push_capped(to, snapshot_of(state));
  TextEditSnapshot restored = std::move(from.back());
  from.pop_back();
  state.text = std::move(restored.text);
  state.cursor = restored.cursor;
  state.selection_anchor = restored.selection_anchor;
  state.preferred_x.reset();
  state.history.merge_open = false;
  return true;
}

}  // namespace

bool undo(TextEditState& state) {
  return step_history(state, state.history.undo, state.history.redo);
}

bool redo(TextEditState& state) {
  return step_history(state, state.history.redo, state.history.undo);
}

void clear_history(TextEditState& state) {
  state.history.undo.clear();
  state.history.redo.clear();
  state.history.merge_open = false;
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
