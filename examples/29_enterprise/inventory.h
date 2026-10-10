// examples/29_enterprise: 资产管理 - a desktop "asset inventory" app on the
// public runtime dg::App / dg::Window, wired the way a real enterprise tool
// is: a menu bar and keyboard shortcuts that share one command switch, a
// toolbar, a location tree that filters a sortable table, a detail panel
// with tabs, form dialogs, and a status bar.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "drawgui/app/app.h"
#include "drawgui/app/window.h"

namespace inventory {

inline constexpr int kWidth = 1280;
inline constexpr int kHeight = 800;

// One command enum: menu entries carry it as MenuEntry::command and
// shortcuts as ActionEvent::action_id - kUserActionBase. Inventory::run()
// is the single switch handling both.
enum Cmd : std::int32_t {
  kCmdNone = 0,
  kCmdNew,
  kCmdSave,
  kCmdQuit,
  kCmdRename,
  kCmdDelete,
  kCmdFind,
  kCmdShowDetail,
  kCmdCompact,
  kCmdDark,
  kCmdAbout,
  kCmdEdit,
  kCmdCopy,
  kCmdToggleInUse,
  kCmdNewHere,
  kCmdRenameNode,
};

enum class DialogKind : std::uint8_t {
  kNone,
  kNew,
  kEdit,
  kRename,
  kRenameNode,
  kDelete,
  kQuit,
  kAbout
};

inline constexpr int kTypeCount = 5;

struct Tag {
  std::uint64_t id = 0;
  std::string name;
  bool on = true;
};

struct Asset {
  std::uint64_t id = 0;  // the table row key
  std::string name;
  int type = 0;  // 0 笔记本 1 显示器 2 服务器 3 打印机 4 手机
  std::string owner;
  int value = 0;
  bool in_use = true;
  int dept = 1;
  std::vector<Tag> tags;
};

// A change that can be undone from the 历史 tab: the asset as it was.
struct HistoryEntry {
  std::uint64_t id = 0;  // the history row key
  std::uint64_t asset = 0;
  int kind = 0;  // 0 created, 1 edited, 2 toggled, 3 tag
  std::string text;
  std::string time;
  Asset before;
};

struct Dept {
  int id = 0;
  int location = 0;
  std::string name;
};

class Inventory {
 public:
  // Every node the app (and its check) names.
  struct Nodes {
    dg::NodeId root, menu_bar, toolbar;
    dg::NodeId btn_new, btn_save, btn_delete, btn_filter, search;
    dg::NodeId icon_new, icon_save, icon_delete;
    dg::NodeId left, tree, centre, table, table_count, right, detail_title, tabs;
    dg::NodeId f_name, f_owner, f_type, f_in_use, f_apply, f_revert;
    dg::NodeId history;
    dg::NodeId tag_field, tag_add, tag_scroll, tag_empty;
    dg::NodeId status, last_action;
    // The open form dialog's fields (none while closed).
    std::optional<dg::NodeId> dlg_form, dlg_name, dlg_owner, dlg_value;
    std::vector<dg::NodeId> dlg_types;
    std::vector<dg::NodeId> tag_rows;
  };

  Inventory(dg::App& app, dg::Window& window, dg::ThemeVariant variant);
  void on_event(const dg::Event& event);
  // The one command switch; `key` = the row a context menu acted on (0 =
  // the selected asset).
  void run(Cmd cmd, std::uint64_t key = 0);

  [[nodiscard]] const Nodes& nodes() const { return n_; }
  [[nodiscard]] const std::vector<Asset>& assets() const { return assets_; }
  [[nodiscard]] const Asset* find(std::uint64_t key) const;
  [[nodiscard]] const std::vector<std::uint64_t>& visible() const { return visible_; }
  [[nodiscard]] const std::vector<HistoryEntry>& history() const { return history_; }
  [[nodiscard]] std::uint64_t selected_key() const { return selected_; }
  [[nodiscard]] bool dirty() const { return dirty_; }
  [[nodiscard]] int saves() const { return saves_; }
  [[nodiscard]] DialogKind dialog_kind() const { return dialog_; }
  [[nodiscard]] std::int32_t dialog_id() const { return dialog_id_; }
  [[nodiscard]] bool quit_requested() const { return quit_; }
  [[nodiscard]] const std::string& last_action() const { return last_; }
  // The create() node holding focus, as FocusChangedEvent names it
  // (Window::focused() answers a component's inner node instead).
  [[nodiscard]] std::optional<dg::NodeId> focus_owner() const { return focus_; }

 private:
  enum class Role : std::uint8_t { kPage, kPanel, kChrome, kMuted, kSoft, kInput };
  struct Paint {
    dg::NodeId node;
    std::uint16_t prop;
    Role role;
  };
  struct Icons {
    std::vector<dg::ImageId> type;  // name cell, by type
    std::vector<dg::ImageId> dot;   // type cell, by type
    dg::ImageId all, building, folder;
    dg::ImageId h_create, h_edit, h_toggle, h_tag, undo;
    dg::ImageId plain_close;  // untinted; DG_PROP_IMAGE_TINT colours it
  };

  // Builders.
  void build();
  void build_menu_bar();
  void build_toolbar();
  void build_left(dg::NodeId body);
  void build_centre(dg::NodeId body);
  void build_right(dg::NodeId body);
  void build_status();
  dg::NodeId box(dg::NodeId parent, bool row, float gap);
  dg::NodeId label(dg::NodeId parent, const std::string& text, float size, bool muted,
                   float width = 0);
  dg::NodeId button(dg::NodeId parent, const std::string& text, bool primary, float width);
  dg::NodeId icon_button(dg::NodeId parent, const char* svg, const std::string& tip,
                         dg::NodeId& image);
  dg::NodeId field(dg::NodeId parent, const std::string& placeholder);
  void pad(dg::NodeId node, float x, float y);
  void radius(dg::NodeId node, float px);
  void edge(dg::NodeId node, std::uint16_t side);
  void paint(dg::NodeId node, std::uint16_t prop, Role role);
  [[nodiscard]] dg::Color color_of(Role role) const;
  void load_icons();
  [[nodiscard]] const Icons& icons() const;

  // Model -> view.
  void refresh_all();
  void refresh_table();
  void refresh_tree();
  void refresh_detail();
  void refresh_history();
  void refresh_tags();
  void refresh_status();
  void set_last(const std::string& text);
  [[nodiscard]] bool passes(const Asset& asset) const;
  [[nodiscard]] Asset* find_mut(std::uint64_t key);
  [[nodiscard]] std::uint64_t target(std::uint64_t key) const;
  void record(const Asset& before, int kind, const std::string& text);
  void mark_dirty();
  [[nodiscard]] std::string now();
  void select_asset(std::uint64_t key);
  void form_changed();
  void apply_form();

  // Popups.
  void open_row_menu(std::uint64_t key, dg::PixelPoint at);
  void open_tree_menu(std::uint64_t key, dg::PixelPoint at);
  void open_asset_dialog(DialogKind kind, std::uint64_t key, int dept);
  void open_rename_dialog(DialogKind kind, std::uint64_t key, const std::string& current);
  void open_plain_dialog(DialogKind kind, const std::string& title, const std::string& body,
                         std::vector<std::string> buttons, int default_button, int cancel);
  void on_dialog(int button);
  void update_dialog_ok();
  void close_dialog_form();
  void set_view_entry(int item, const std::string& text, bool on, Cmd cmd);

  // Handlers.
  void on_click(const dg::events::ClickEvent& e);
  void on_context(const dg::events::ContextClickEvent& e);
  void on_row_part(const dg::events::RowPartEvent& e);
  void on_text(dg::NodeId node);
  void sort_by(int column);
  void undo(std::uint64_t history_key);
  void add_tag();

  dg::App& app_;
  dg::Window& w_;
  dg::ThemeVariant variant_;
  Nodes n_;
  std::vector<Paint> paints_;
  Icons icons_[2];
  std::vector<Dept> depts_;
  std::vector<std::string> locations_;
  std::vector<Asset> assets_;
  std::vector<HistoryEntry> history_;
  std::vector<std::uint64_t> visible_;
  std::vector<bool> tree_expanded_;
  std::uint64_t selected_ = 0;
  std::uint64_t tree_key_ = 1;
  std::uint64_t next_asset_ = 1;
  std::uint64_t next_history_ = 1;
  std::uint64_t next_tag_ = 1;
  int sort_column_ = -1;
  bool sort_ascending_ = true;
  bool filter_in_use_ = false;
  bool show_detail_ = true;
  bool compact_ = false;
  bool dirty_ = false;
  bool form_dirty_ = false;
  bool quit_ = false;
  int saves_ = 0;
  int clock_ = 9 * 60 + 12;
  std::string search_;
  std::string last_ = "就绪";
  std::uint64_t menu_target_ = 0;
  std::optional<dg::NodeId> focus_;
  DialogKind dialog_ = DialogKind::kNone;
  std::int32_t dialog_id_ = 0;
  std::uint64_t dialog_target_ = 0;
  int dialog_dept_ = 1;
};

dg::Window* open_window(dg::App& app);
std::shared_ptr<dg::Theme> make_theme();
// The headless check (CTest enterprise.verify).
int verify(dg::App& app, std::ostream& out);

}  // namespace inventory
