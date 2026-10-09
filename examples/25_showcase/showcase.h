// examples/25_showcase: the one demo of every drawgui feature, built on the
// public runtime (dg::App / dg::Window).
//
// One window: a header (title + light/dark switch), a Tabs strip with one
// page per area, and a status line that names the last event. Showcase owns
// the node ids, the app-side data (table rows, timers, counters) and the
// reaction to every event; main.cpp picks the mode, showcase_check.cpp
// drives it headlessly.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "drawgui/app/app.h"
#include "drawgui/app/window.h"
#include "drawgui/theme/theme.h"

namespace showcase {

inline constexpr int kWidth = 1120;
inline constexpr int kHeight = 780;
inline constexpr int kListItems = 1000;
inline constexpr int kFieldMaxLength = 12;
inline constexpr int kProgressTickMs = 80;
inline constexpr int kClockTickMs = 1000;
inline constexpr std::uint64_t kWakerPayload = 42;
inline constexpr const char* kNamePlaceholder = "输入你的名字…";

enum Tab : std::uint8_t {
  kBasics,
  kText,
  kLayout,
  kData,
  kPopups,
  kImages,
  kWindow,
  kTabCount
};

// Colours the builtin theme has no token for. Everything else binds to a
// theme token and follows the switch on its own.
enum class Role : std::uint8_t { kPage, kSubtle, kMuted, kAccentSoft, kChip, kHeader };

struct Paint {
  dg::NodeId node;
  std::uint16_t prop = 0;
  Role role = Role::kPage;
};

// Buttons follow the theme by themselves (primary); a secondary one gets a
// subtle fill of its own, the runtime deriving its hover/press shades.
enum class ControlLook : std::uint8_t { kPrimary, kSecondary };

struct Nodes {
  dg::NodeId root;
  dg::NodeId header;
  dg::NodeId theme_button;
  dg::NodeId tabs;
  std::array<dg::NodeId, kTabCount> pages{};
  dg::NodeId status;

  // 基础控件
  dg::NodeId primary_button;
  dg::NodeId secondary_button;
  dg::NodeId disabled_button;
  dg::NodeId click_count;
  dg::NodeId notify_check;
  std::vector<dg::NodeId> size_radios;
  dg::NodeId size_label;
  dg::NodeId volume;
  dg::NodeId volume_label;
  dg::NodeId steps;
  dg::NodeId steps_label;
  dg::NodeId enable_switch;
  dg::NodeId editable_group;
  dg::NodeId group_check;
  dg::NodeId group_slider;
  dg::NodeId group_button;
  std::vector<dg::NodeId> cursor_tiles;

  // 文本
  dg::NodeId name_field;
  dg::NodeId code_field;
  dg::NodeId code_counter;
  dg::NodeId text_area;
  dg::NodeId paragraph;

  // 布局
  std::vector<dg::NodeId> justify_rows;
  dg::NodeId percent_row;
  std::vector<dg::NodeId> percent_cells;
  dg::NodeId wrap_box;

  // 数据
  dg::NodeId list;
  dg::NodeId tree;
  dg::NodeId table;
  dg::NodeId progress;
  dg::NodeId progress_label;
  dg::NodeId progress_button;
  dg::NodeId spinner;

  // 弹层
  dg::NodeId dropdown;
  dg::NodeId dropdown_label;
  dg::NodeId menu_area;
  dg::NodeId menu_label;
  dg::NodeId delete_button;
  dg::NodeId info_button;
  dg::NodeId dialog_label;
  std::vector<dg::NodeId> tooltip_buttons;

  // 图片
  std::array<dg::NodeId, 3> bitmaps{};
  std::array<dg::ImageId, 3> bitmap_ids{};

  // 窗口
  dg::NodeId title_button;
  dg::NodeId maximize_button;
  dg::NodeId restore_button;
  dg::NodeId fullscreen_button;
  dg::NodeId minimize_button;
  dg::NodeId tick_label;
  dg::NodeId waker_button;
  dg::NodeId waker_label;
  std::vector<dg::NodeId> log_lines;
};

class Showcase {
 public:
  Showcase(dg::App& app, dg::Window& window, std::shared_ptr<dg::Theme> theme,
           dg::ThemeVariant variant);
  Showcase(const Showcase&) = delete;
  Showcase& operator=(const Showcase&) = delete;
  Showcase(Showcase&&) = delete;
  Showcase& operator=(Showcase&&) = delete;
  ~Showcase();

  // Starts the progress and clock timers (the window mode and the checks).
  void start_timers();
  // Everything the app does in answer to an event.
  void on_event(const dg::Event& event);

  void set_variant(dg::ThemeVariant variant);
  [[nodiscard]] dg::ThemeVariant variant() const { return variant_; }
  void select_tab(int index);
  void set_progress(float value);

  [[nodiscard]] const Nodes& nodes() const { return n_; }
  [[nodiscard]] dg::Window& window() const { return w_; }
  [[nodiscard]] const std::vector<std::vector<std::string>>& rows() const { return rows_; }
  [[nodiscard]] int clicks() const { return clicks_; }
  [[nodiscard]] int ticks() const { return ticks_; }
  [[nodiscard]] float progress_value() const { return progress_; }
  [[nodiscard]] std::uint64_t woken_payload() const { return woken_; }
  [[nodiscard]] std::optional<std::int32_t> last_menu() const { return menu_id_; }
  [[nodiscard]] std::string status_text() const;
  [[nodiscard]] dg::Color color_of(Role role) const;

 private:
  void build();
  void build_header();
  void build_basics(dg::NodeId page);
  void build_text(dg::NodeId page);
  void build_layout(dg::NodeId page);
  void build_data(dg::NodeId page);
  void build_popups(dg::NodeId page);
  void build_images(dg::NodeId page);
  void build_window(dg::NodeId page);
  void restyle();

  // Builders.
  dg::NodeId box(dg::NodeId parent, bool row, float gap);
  dg::NodeId card(dg::NodeId parent, const std::string& title, const std::string& note);
  dg::NodeId label(dg::NodeId parent, const std::string& text, float size = 14,
                   bool muted = false, float width = -1);
  dg::NodeId button(dg::NodeId parent, const std::string& text, ControlLook look,
                    float width = 0);
  dg::NodeId toggle(dg::NodeId parent, dg::NodeKind kind, const std::string& text);
  void paint(dg::NodeId node, std::uint16_t prop, Role role);
  void pad(dg::NodeId node, float px);
  void pad(dg::NodeId node, float x, float y);
  void radius(dg::NodeId node, float px);
  void outline(dg::NodeId node);
  void align(dg::NodeId node, std::uint32_t align);

  void status(const std::string& text);
  void sort_table(int column);
  void log_window(const std::string& line);
  void open_context_menu(dg::PixelPoint at);
  void on_click(const dg::events::ClickEvent& click);
  void on_window_click(dg::NodeId node);
  void on_timer(const dg::events::TimerEvent& timer);

  dg::App& app_;
  dg::Window& w_;
  std::shared_ptr<dg::Theme> theme_;
  dg::ThemeVariant variant_;
  Nodes n_;
  std::vector<Paint> paints_;

  std::vector<std::vector<std::string>> rows_;
  int sort_column_ = -1;
  bool sort_ascending_ = true;
  int clicks_ = 0;
  int ticks_ = 0;
  float progress_ = 0.0F;
  bool progress_running_ = true;
  bool fullscreen_ = false;
  int title_index_ = 0;
  std::uint64_t woken_ = 0;
  std::optional<std::int32_t> menu_id_;
  std::vector<std::string> log_;
  dg::TimerId progress_timer_{};
  dg::TimerId clock_timer_{};
  std::thread waker_thread_;
};

// The demo's window (kWidth x kHeight, resizable).
dg::Window* open_window(dg::App& app);

// The palette-tuned builtin theme (indigo accent).
std::shared_ptr<dg::Theme> make_theme();

// The runtime-generated sample bitmaps, encoded: PNG, JPEG, WebP.
std::array<std::vector<std::uint8_t>, 3> sample_images();

}  // namespace showcase
