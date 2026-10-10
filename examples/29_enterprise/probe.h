// Drives the inventory window the way a user would (examples/29_enterprise
// --verify and --dump-png): real platform input on the SDL queue, every
// event the pump returns handed to the app.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "drawgui/app/app.h"
#include "drawgui/app/window.h"
#include "drawgui/shortcuts/chord.h"
#include "drawgui/shortcuts/logical_key.generated.h"
#include "drawgui/window/window_manager.h"
#include "inventory.h"

namespace inventory {

class Probe {
 public:
  Probe(dg::App& app, dg::Window& window, Inventory& inv) : app_(app), w_(window), inv_(inv) {}

  std::vector<dg::Event> pump(int rounds = 3);
  std::vector<dg::Event> click(int x, int y,
                               dg::PointerButton button = dg::PointerButton::kPrimary,
                               int clicks = 0);
  std::vector<dg::Event> click(dg::NodeId node,
                               dg::PointerButton button = dg::PointerButton::kPrimary,
                               int clicks = 0);
  std::vector<dg::Event> click_text(const std::string& text,
                                    std::optional<dg::NodeId> within = std::nullopt,
                                    dg::PointerButton button = dg::PointerButton::kPrimary);
  // Presses the name cell of the table row whose key is `key`.
  std::vector<dg::Event> click_row(std::uint64_t key,
                                   dg::PointerButton button = dg::PointerButton::kPrimary,
                                   int clicks = 0);
  std::vector<dg::Event> chord(dg::LogicalKey key, dg::Modifier mods = dg::Modifier::kNone);
  std::vector<dg::Event> key(dg::Key key, bool shift = false);
  std::vector<dg::Event> type(const std::string& text);

  [[nodiscard]] dg::PixelRect bounds(dg::NodeId node) const;
  [[nodiscard]] bool under(dg::NodeId ancestor, dg::NodeId node) const;
  [[nodiscard]] std::optional<dg::NodeId> find_text(
      const std::string& text, std::optional<dg::NodeId> within = std::nullopt) const;
  [[nodiscard]] std::optional<dg::NodeId> name_cell(std::uint64_t key) const;

 private:
  dg::App& app_;
  dg::Window& w_;
  Inventory& inv_;
};

}  // namespace inventory
