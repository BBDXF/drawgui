// Hover-delay timing: "how long has the pointer sat continuously over one
// widget" - 7-5b's own prerequisite for Tooltip (doc/menus.md section 6.2).
//
// 5-2/7-5 already found the mechanism half is free: 6-1's `dg::AnimTime` is
// a plain millisecond timestamp, so "show after N ms of continuous hover" is
// one comparison against a recorded start time, needing no new engine
// feature (doc/animation.md's own `AnimTime` seam, unchanged by this file).
// What doc/menus.md named as genuinely missing was a HOME for that start
// time - this file is that home.
//
// A SINGLE OPTIONAL (NodeId, AnimTime) PAIR, NOT A SIDE TABLE KEYED BY
// NodeId. Defended against the side-table precedent this project actually
// has (`ThemeBindings`, `WidgetSet` itself): both of those exist because
// MANY nodes can simultaneously need the state they hold (many nodes can be
// theme-bound; every node can carry a Widget). Hover is not that shape - at
// most ONE widget is hovered at any instant, which is exactly why
// `dg::Interaction` itself already holds its own `hovered_` as a single
// `optional<NodeId>` rather than a per-node bool table (interaction.h's own
// header, unchanged) - and `dg::Focus` holds its own scope root as a single
// optional NodeId for the identical reason (focus.h's own header). A
// HoverTimer is the same argument applied one field further: not a table
// keyed by every node that COULD be hovered, a single record of the one
// that currently IS.
#pragma once

#include <optional>

#include "drawgui/anim/clock.h"
#include "drawgui/render/render_tree.h"

namespace dg {

// `target`/`since` together answer "which widget, and for how long" -
// std::nullopt in `target` means nothing is currently being timed (no
// widget hovered, or the pointer left before the delay elapsed and nothing
// has re-entered since).
struct HoverTimer {
  std::optional<NodeId> target;
  AnimTime since;
};

// Called once per pump()/frame with the widget dg::Interaction::hovered()
// currently names (or nullopt) and the current time. Restarts the timer -
// resets `since` to `now` - whenever `hovered` differs from `timer.target`,
// INCLUDING the transition to or from nullopt: a tooltip's delay measures
// CONTINUOUS hover over the SAME widget, so leaving and re-entering (even
// the same widget) must not inherit time already accumulated before the
// gap, the identical "an atomic transition, never a stale partial state"
// argument dg::Interaction's own InteractionChange already makes for
// hover/press.
void update_hover_timer(HoverTimer& timer, std::optional<NodeId> hovered, AnimTime now);

// Whether `timer` names a widget that has been continuously hovered for at
// least `delay_ms` as of `now` - the single comparison doc/menus.md section
// 6.2 already named as needing no new engine feature. False when nothing is
// being timed (`timer.target` is nullopt).
[[nodiscard]] bool hover_ready(const HoverTimer& timer, AnimTime now, int delay_ms);

}  // namespace dg
