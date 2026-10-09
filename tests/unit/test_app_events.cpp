// dg::Event -> DG_EVENT_*: every alternative of the runtime's event variant
// (and every `kind` of the ones that carry one) maps to its own C ABI kind.

#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <variant>
#include <vector>

#include <doctest/doctest.h>
#include "abi/abi_internal.h"
#include "drawgui/app/events.h"

namespace {

template <std::size_t... I>
std::vector<dg::Event> one_of_each(std::index_sequence<I...> /*indices*/) {
  return {dg::Event(std::in_place_index<I>)...};
}

}  // namespace

TEST_CASE("app events: every alternative maps to a distinct DG_EVENT_* kind") {
  std::vector<dg::Event> events =
      one_of_each(std::make_index_sequence<std::variant_size_v<dg::Event>>{});
  using TreeKind = dg::events::TreeEvent::Kind;
  using TableKind = dg::events::TableEvent::Kind;
  using StateKind = dg::events::WindowStateEvent::Kind;
  // The alternatives whose kind is a field, in every value but the default.
  dg::events::MenuEvent activated;
  activated.item = 0;
  events.emplace_back(activated);
  for (const TreeKind kind : {TreeKind::kExpand, TreeKind::kCollapse}) {
    dg::events::TreeEvent tree;
    tree.kind = kind;
    events.emplace_back(tree);
  }
  dg::events::TableEvent sort;
  sort.kind = TableKind::kSort;
  events.emplace_back(sort);
  for (const StateKind kind :
       {StateKind::kMoved, StateKind::kMinimized, StateKind::kMaximized, StateKind::kRestored,
        StateKind::kFocusGained, StateKind::kFocusLost, StateKind::kEnteredFullscreen,
        StateKind::kLeftFullscreen}) {
    dg::events::WindowStateEvent state;
    state.kind = kind;
    events.emplace_back(state);
  }

  std::set<std::uint32_t> kinds;
  for (const dg::Event& event : events) {
    const std::uint32_t kind = dg::abi::dg_event_kind(event);
    CHECK(kind >= DG_EVENT_WINDOW_CLOSED);
    CHECK(kind <= DG_EVENT_SLIDER_CHANGED);
    kinds.insert(kind);
  }
  CHECK(kinds.size() == events.size());
  // ...and together they cover every DG_EVENT_* kind there is.
  CHECK(kinds.size() == DG_EVENT_SLIDER_CHANGED - DG_EVENT_WINDOW_CLOSED + 1);
}
