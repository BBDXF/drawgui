#include "scene.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace scene {
namespace {

using dg::Color;
using dg::NodeId;
using dg::NodeStyle;
using dg::PixelRect;

constexpr int kSidebarItems = 6;
constexpr int kHeaderTicks = 12;
constexpr int kCardColumns = 3;
constexpr int kCardRows = 2;

// The pulsing card sweeps between these two over kPulsePeriod frames. Both
// are opaque: an alpha-bearing node would be blended onto whatever the damage
// repaint left underneath it, which is correct here only because the root
// repaints the background first - but relying on that in the one node that
// changes every frame would make the verification prove less than it looks.
constexpr std::uint32_t kPulseLow = 0xFF264257;
constexpr std::uint32_t kPulseHigh = 0xFF3FA9F5;
constexpr int kPulsePeriod = 96;

// A slow beat so a human watching the window can see the two corners blink
// together, and so most frames damage only the pulse and the marker.
constexpr int kCornerPeriod = 24;

NodeStyle flat(std::uint32_t argb) {
  NodeStyle style;
  style.fill = Color::from_argb(argb);
  return style;
}

// Square corners on every container, and rounded ones only on the small
// accents that actually move. That is a damage decision, not a visual one:
// an anti-aliased rounded node has to be repainted whole (see NodeStyle in
// render_tree.h), so a rounded CARD makes its entire area the smallest unit
// of damage anything inside it can produce. Measured on this scene at 1080p,
// rounding the containers took a frame's damage from 0.9% of the window to
// 23.8%, and the repaint from 0.10 ms to 0.27 ms.
NodeStyle panel(std::uint32_t fill, std::uint32_t border) {
  NodeStyle style;
  style.fill = Color::from_argb(fill);
  style.border_color = Color::from_argb(border);
  style.border_width = dg::BorderWidths::all(1.0F);
  return style;
}

NodeStyle pill(std::uint32_t fill, std::uint32_t border, float radius) {
  NodeStyle style = panel(fill, border);
  style.radii = dg::Radii::all(radius);
  return style;
}

std::uint8_t mix_channel(std::uint8_t from, std::uint8_t to, int numerator, int denominator) {
  const int span = static_cast<int>(to) - static_cast<int>(from);
  return static_cast<std::uint8_t>(static_cast<int>(from) + ((span * numerator) / denominator));
}

// Integer arithmetic on purpose. The verification replays this script on two
// surfaces and compares the results byte for byte, so every value it produces
// has to be exactly reproducible rather than merely close.
Color mix(std::uint32_t from, std::uint32_t to, int numerator, int denominator) {
  const Color a = Color::from_argb(from);
  const Color b = Color::from_argb(to);
  return Color::rgba(mix_channel(a.red(), b.red(), numerator, denominator),
                     mix_channel(a.green(), b.green(), numerator, denominator),
                     mix_channel(a.blue(), b.blue(), numerator, denominator), 0xFF);
}

// A triangle wave, so the sweep reverses instead of snapping back - a jump
// would still be correct but would hide a one-frame stale-pixel artifact in
// the discontinuity.
int triangle(int frame, int period) {
  const int phase = ((frame % period) + period) % period;
  const int half = period / 2;
  return phase < half ? phase : period - phase;
}

}  // namespace

Scene build(const dg::TreeSpec& spec) {
  dg::RenderTree tree{spec};
  const int width = spec.viewport.width;
  const int height = spec.viewport.height;

  const int header_h = std::clamp(height / 12, 20, 64);
  const int sidebar_w = std::clamp(width / 6, 70, 240);
  const int pad = std::clamp(width / 90, 6, 16);
  const int readout_h = std::clamp(height / 4, 116, 220);

  const NodeId root = dg::RenderTree::root();
  const NodeId header =
      tree.add_child(root, PixelRect{0, 0, width, header_h}, flat(0xFF1B2028));
  for (int i = 0; i < kHeaderTicks; ++i) {
    const int tick_w = std::max(8, sidebar_w / 5);
    tree.add_child(header,
                   PixelRect{pad + (i * (tick_w + pad)), header_h / 3, tick_w, header_h / 3},
                   flat(i % 3 == 0 ? 0xFF3A4657 : 0xFF2A323D));
  }

  const int body_h = height - header_h - readout_h;
  const NodeId sidebar =
      tree.add_child(root, PixelRect{0, header_h, sidebar_w, body_h}, flat(0xFF191E26));
  const int item_h = std::clamp(body_h / 10, 18, 40);
  for (int i = 0; i < kSidebarItems; ++i) {
    tree.add_child(
        sidebar,
        PixelRect{pad, pad + (i * (item_h + (pad / 2))), sidebar_w - (2 * pad), item_h},
        panel(0xFF232A34, 0xFF2E3742));
  }

  const PixelRect content_bounds{sidebar_w, header_h, width - sidebar_w, body_h};
  const NodeId content = tree.add_child(root, content_bounds, flat(0xFF14171C));

  const int track_h = std::clamp(content_bounds.height / 8, 24, 56);
  const int grid_w = content_bounds.width - (pad * (kCardColumns + 1));
  const int grid_h = content_bounds.height - track_h - (pad * (kCardRows + 2));
  const int card_w = grid_w / kCardColumns;
  const int card_h = grid_h / kCardRows;

  Handles handles;
  NodeId host = content;
  for (int row = 0; row < kCardRows; ++row) {
    for (int column = 0; column < kCardColumns; ++column) {
      const PixelRect card{pad + (column * (card_w + pad)), pad + (row * (card_h + pad)),
                           card_w, card_h};
      const NodeId id = tree.add_child(content, card, panel(0xFF222A35, 0xFF313B49));
      if (row == 0 && column == 1) {
        host = id;
      }
    }
  }

  // The one node that changes colour every frame is a small accent inside a
  // card, not the card itself. That is what a hover highlight or a caret is,
  // and it is the scale the acceptance number is about.
  const int pill_w = std::clamp(card_w / 3, 60, 200);
  const int pill_h = std::clamp(card_h / 5, 18, 40);
  handles.pulse = tree.add_child(host, PixelRect{pad, (card_h - pill_h) / 2, pill_w, pill_h},
                                 pill(kPulseLow, 0xFF5C8FB5, 6.0F));

  // Added after the pill and overlapping it, so it paints on top. If a
  // repaint driven by the pill's damage failed to bring this node back, the
  // badge would be erased - the classic z-order bug in a damage system.
  handles.badge =
      tree.add_child(host,
                     PixelRect{pad + pill_w - (pill_w / 3), ((card_h - pill_h) / 2) - (pad / 2),
                               std::max(20, pill_w / 2), std::max(16, pill_h)},
                     pill(0xFFE74C3C, 0xFFFF9C8F, 5.0F));

  const int marker_w = std::clamp(content_bounds.width / 9, 30, 110);
  handles.marker_track = PixelRect{pad, content_bounds.height - track_h - pad,
                                   content_bounds.width - (2 * pad), track_h};
  tree.add_child(content, handles.marker_track, panel(0xFF1A1F27, 0xFF2A323D));
  handles.marker = tree.add_child(
      content,
      PixelRect{handles.marker_track.x + 2, handles.marker_track.y + 4, marker_w, track_h - 8},
      pill(0xFF2E86DE, 0xFF8FC8FF, 4.0F));

  const int dot = std::clamp(content_bounds.width / 40, 12, 28);
  handles.corner_a = tree.add_child(content, PixelRect{pad, pad, dot, dot}, flat(0xFFF6C445));
  handles.corner_b = tree.add_child(
      content,
      PixelRect{content_bounds.width - pad - dot, handles.marker_track.y - pad - dot, dot, dot},
      flat(0xFF27AE60));

  // Last, so it paints over everything.
  handles.readout = tree.add_child(root, PixelRect{0, height - readout_h, width, readout_h},
                                   flat(0xFF0E1116));

  return Scene{std::move(tree), handles};
}

void apply_frame(dg::RenderTree& tree, const Handles& handles, int frame) {
  tree.set_fill(handles.pulse,
                mix(kPulseLow, kPulseHigh, triangle(frame, kPulsePeriod), kPulsePeriod / 2));

  const PixelRect marker = tree.local_bounds(handles.marker);
  const int span = std::max(1, handles.marker_track.width - marker.width - 4);
  tree.set_local_origin(handles.marker,
                        handles.marker_track.x + 2 + triangle(frame * 3, span * 2), marker.y);

  if (frame % kCornerPeriod == 0) {
    const bool lit = (frame / kCornerPeriod) % 2 == 0;
    tree.set_fill(handles.corner_a, Color::from_argb(lit ? 0xFFF6C445 : 0xFF4A421F));
    tree.set_fill(handles.corner_b, Color::from_argb(lit ? 0xFF27AE60 : 0xFF1D3B2A));
  }
}

}  // namespace scene
