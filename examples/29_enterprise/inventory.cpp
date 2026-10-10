#include "inventory.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>
#include <variant>

#include "drawgui/app/prop.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"

namespace inventory {
namespace {

using dg::NodeId;
using dg::NodeKind;

constexpr dg::Color argb(std::uint32_t v) {
  return dg::Color::from_argb(v);
}

const char* const kTypeNames[kTypeCount] = {"笔记本", "显示器", "服务器", "打印机", "手机"};

// Monochrome 24x24 SVG icons; toolbar images are tinted by a theme token at
// paint time, row icons are decoded once per theme with their colour baked in.
const char* const kSvgPlus =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6z"/></svg>)";
const char* const kSvgSave =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M17 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2V7l-4-4zm-5 16a3 3 0 1 1 0-6 3 3 0 0 1 0 6zm3-10H5V5h10v4z"/></svg>)";
const char* const kSvgTrash =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M6 19a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>)";
const char* const kSvgSearch =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"/></svg>)";
const char* const kSvgClose =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M19 6.41 17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z"/></svg>)";
const char* const kSvgGrid =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M4 4h7v7H4zm9 0h7v7h-7zM4 13h7v7H4zm9 0h7v7h-7z"/></svg>)";
const char* const kSvgBuilding =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M12 7V3H2v18h20V7H12zM6 19H4v-2h2v2zm0-4H4v-2h2v2zm0-4H4V9h2v2zm0-4H4V5h2v2zm4 12H8v-2h2v2zm0-4H8v-2h2v2zm0-4H8V9h2v2zm0-4H8V5h2v2zm10 12h-8v-2h2v-2h-2v-2h2v-2h-2V9h8v10zm-2-8h-2v2h2v-2zm0 4h-2v2h2v-2z"/></svg>)";
const char* const kSvgFolder =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M10 4H4a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-8l-2-2z"/></svg>)";
const char* const kSvgPencil =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04a1 1 0 0 0 0-1.41l-2.34-2.34a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>)";
const char* const kSvgPower =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M13 3h-2v10h2V3zm4.83 2.17-1.42 1.42A6.92 6.92 0 0 1 19 12a7 7 0 1 1-11.42-5.42L6.17 5.17A8.93 8.93 0 0 0 3 12a9 9 0 0 0 18 0c0-2.74-1.23-5.18-3.17-6.83z"/></svg>)";
const char* const kSvgTag =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M21.41 11.58l-9-9A2 2 0 0 0 11 2H4a2 2 0 0 0-2 2v7a2 2 0 0 0 .59 1.42l9 9a2 2 0 0 0 2.82 0l7-7a2 2 0 0 0 0-2.84zM5.5 7A1.5 1.5 0 1 1 7 5.5 1.5 1.5 0 0 1 5.5 7z"/></svg>)";
const char* const kSvgUndo =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M12.5 8c-2.65 0-5.05.99-6.9 2.6L2 7v9h9l-3.62-3.62A7.95 7.95 0 0 1 20.1 16l2.37-.78C21.08 11.03 17.15 8 12.5 8z"/></svg>)";
const char* const kSvgDot =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><circle cx="12" cy="12" r="5"/></svg>)";
const char* const kSvgType[kTypeCount] = {
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M20 18a2 2 0 0 0 2-2V6a2 2 0 0 0-2-2H4a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2H0v2h24v-2h-4zM4 6h16v10H4V6z"/></svg>)",
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M21 3H3a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h5v2h8v-2h5a2 2 0 0 0 2-2V5a2 2 0 0 0-2-2zm0 14H3V5h18v12z"/></svg>)",
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M4 3h16a1 1 0 0 1 1 1v6a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1zm0 10h16a1 1 0 0 1 1 1v6a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1v-6a1 1 0 0 1 1-1z"/><circle cx="7" cy="7" r="1.5" fill="#fff"/><circle cx="7" cy="17" r="1.5" fill="#fff"/></svg>)",
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M19 8H5a3 3 0 0 0-3 3v6h4v4h12v-4h4v-6a3 3 0 0 0-3-3zm-3 11H8v-5h8v5zm3-7a1 1 0 1 1 0-2 1 1 0 0 1 0 2zm-1-9H6v4h12V3z"/></svg>)",
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M16 1H8a2.5 2.5 0 0 0-2.5 2.5v17A2.5 2.5 0 0 0 8 23h8a2.5 2.5 0 0 0 2.5-2.5v-17A2.5 2.5 0 0 0 16 1zm-4 21a1.5 1.5 0 1 1 0-3 1.5 1.5 0 0 1 0 3zm4.5-4h-9V4h9v14z"/></svg>)",
};

constexpr std::uint64_t kTreeAll = 1;
constexpr std::uint64_t kTreeLocation = 100;
constexpr std::uint64_t kTreeDept = 200;

// Table columns.
constexpr int kColName = 0;
constexpr int kColType = 1;
constexpr int kColInUse = 2;
constexpr int kColOwner = 3;
constexpr int kColValue = 4;

std::string money(int value) {
  std::string digits = std::to_string(value);
  std::string out;
  int n = 0;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    if (n > 0 && n % 3 == 0) {
      out.insert(out.begin(), ',');
    }
    out.insert(out.begin(), *it);
    ++n;
  }
  return "¥" + out;
}

std::string lower(std::string text) {
  for (char& c : text) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return text;
}

dg::MenuEntry entry(std::string text, Cmd command, std::string shortcut = {}) {
  dg::MenuEntry e;
  e.text = std::move(text);
  e.command = command;
  e.shortcut_label = std::move(shortcut);
  return e;
}

dg::MenuEntry separator() {
  dg::MenuEntry e;
  e.separator = true;
  return e;
}

dg::ImageId load_svg(dg::App& app, const char* svg, dg::Color tint) {
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(svg);  // NOLINT
  const dg::Expected<dg::ImageId, dg::AppError> id =
      app.load_image(bytes, std::char_traits<char>::length(svg), tint);
  return id ? id.value() : dg::ImageId{};
}

}  // namespace

// -- Window and theme --------------------------------------------------------

dg::Window* open_window(dg::App& app) {
  dg::WindowOptions options;
  options.title = "资产管理";
  options.width = kWidth;
  options.height = kHeight;
  options.resizable = true;
  options.min_width = 1040;
  options.min_height = 640;
  dg::Expected<dg::Window*, dg::AppError> made = app.create_window(options);
  if (!made) {
    std::cerr << "create_window() failed: " << made.error().message << "\n";
    return nullptr;
  }
  return made.value();
}

std::shared_ptr<dg::Theme> make_theme() {
  dg::Expected<dg::Theme, dg::ThemeLoadError> loaded = dg::load_builtin_theme();
  auto theme = std::make_shared<dg::Theme>(loaded ? std::move(loaded).value() : dg::Theme{});
  const auto set = [&](dg_token_id id, std::uint32_t light, std::uint32_t dark) {
    theme->set_color(id, dg::ThemeVariant::kLight, argb(light));
    theme->set_color(id, dg::ThemeVariant::kDark, argb(dark));
  };
  set(DG_TOKEN_COLOR_SURFACE, 0xFFFFFFFF, 0xFF1B1F27);
  set(DG_TOKEN_COLOR_ON_SURFACE, 0xFF1D2433, 0xFFE3E7EE);
  set(DG_TOKEN_COLOR_BORDER, 0xFFDDE1E8, 0xFF2F3541);
  set(DG_TOKEN_COLOR_PRIMARY, 0xFF2463EB, 0xFF5B8DEF);
  set(DG_TOKEN_COLOR_ON_PRIMARY, 0xFFFFFFFF, 0xFF0D1220);
  set(DG_TOKEN_COLOR_PRIMARY_HOVER, 0xFF3B76F0, 0xFF76A0F2);
  set(DG_TOKEN_COLOR_PRIMARY_PRESSED, 0xFF1B4FC4, 0xFF4675D6);
  set(DG_TOKEN_COLOR_FOCUS_RING, 0xFF2463EB, 0xFF7FA6F5);
  set(DG_TOKEN_COLOR_DISABLED, 0xFFEDEFF3, 0xFF242932);
  set(DG_TOKEN_COLOR_ON_DISABLED, 0xFF9AA1AD, 0xFF5D6574);
  set(DG_TOKEN_COLOR_SCROLLBAR_TRACK, 0xFFF3F4F7, 0xFF1B1F27);
  set(DG_TOKEN_COLOR_SCROLLBAR_THUMB, 0xFFC5CAD3, 0xFF3A4150);
  return theme;
}

// -- Construction -------------------------------------------------------------

Inventory::Inventory(dg::App& app, dg::Window& window, dg::ThemeVariant variant)
    : app_(app), w_(window), variant_(variant) {
  app_.set_theme(make_theme(), variant_);
  (void)app_.set_tooltip_delay(450);
  load_icons();

  locations_ = {"北京总部", "上海分部", "深圳办事处"};
  depts_ = {{1, 0, "研发部"}, {2, 0, "市场部"}, {3, 0, "财务部"},
            {4, 1, "研发部"}, {5, 1, "行政部"}, {6, 2, "销售部"}};
  tree_expanded_.assign(1 + locations_.size() + depts_.size(), true);

  struct Seed {
    const char* name;
    int type;
    const char* owner;
    int value;
    bool in_use;
    int dept;
  };
  const Seed seeds[] = {
      {"ThinkPad X1 Carbon", 0, "张伟", 12800, true, 1},
      {"MacBook Pro 14", 0, "王芳", 16999, true, 1},
      {"戴尔 U2723QE 显示器", 1, "张伟", 3899, true, 1},
      {"PowerEdge R750", 2, "运维组", 58000, true, 1},
      {"HP LaserJet M428", 3, "行政组", 2599, true, 5},
      {"iPhone 15", 4, "李娜", 5999, true, 2},
      {"华为 MateBook 14", 0, "刘洋", 6499, false, 2},
      {"明基 PD2705U", 1, "陈静", 2999, true, 3},
      {"联想 ThinkSystem SR650", 2, "运维组", 46500, true, 4},
      {"兄弟 HL-L2350DW", 3, "行政组", 1199, false, 5},
      {"小米 14", 4, "杨帆", 3999, true, 6},
      {"Surface Laptop 5", 0, "赵磊", 8888, true, 4},
      {"LG 27UL850", 1, "黄敏", 2699, false, 4},
      {"佳能 iR 2625", 3, "行政组", 9800, true, 3},
      {"MacBook Air 13", 0, "周杰", 7999, true, 6},
      {"三星 S24", 4, "吴昊", 5499, true, 6},
      {"浪潮 NF5280M6", 2, "运维组", 39900, false, 4},
      {"AOC Q27P2C", 1, "李娜", 1299, true, 2},
  };
  const char* const tag_names[] = {"保修期内", "需盘点", "外借", "高价值", "待报废"};
  int seed_index = 0;
  for (const Seed& s : seeds) {
    Asset a;
    a.id = next_asset_++;
    a.name = s.name;
    a.type = s.type;
    a.owner = s.owner;
    a.value = s.value;
    a.in_use = s.in_use;
    a.dept = s.dept;
    a.tags.push_back({next_tag_++, tag_names[seed_index % 2], true});
    if (s.value > 20000) {
      a.tags.push_back({next_tag_++, tag_names[3], true});
    }
    if (!s.in_use) {
      a.tags.push_back({next_tag_++, tag_names[4], false});
    }
    assets_.push_back(a);
    record(a, 0, "登记入库");
    ++seed_index;
  }
  // A little more history on the first asset.
  record(assets_[0], 1, "负责人改为 张伟");
  record(assets_[0], 1, "价值改为 ¥12,800");

  build();
  refresh_all();
}

const Inventory::Icons& Inventory::icons() const {
  return icons_[variant_ == dg::ThemeVariant::kDark ? 1 : 0];
}

void Inventory::load_icons() {
  // Per-type hues for the type dots; one accent for the name-cell icons.
  const std::uint32_t dots[2][kTypeCount] = {
      {0xFF2463EB, 0xFF0E9F6E, 0xFF7C3AED, 0xFFD97706, 0xFFDB2777},
      {0xFF6E9BF2, 0xFF34C38F, 0xFFA78BFA, 0xFFF0B04B, 0xFFF472B6}};
  const std::uint32_t accent[2] = {0xFF2463EB, 0xFF7FA6F5};
  const std::uint32_t muted[2] = {0xFF6B7385, 0xFF8D97A8};
  const std::uint32_t green[2] = {0xFF0E9F6E, 0xFF34C38F};
  const std::uint32_t amber[2] = {0xFFD97706, 0xFFF0B04B};
  for (int v = 0; v < 2; ++v) {
    Icons& set = icons_[v];
    for (int t = 0; t < kTypeCount; ++t) {
      set.type.push_back(load_svg(app_, kSvgType[t], argb(accent[v])));
      set.dot.push_back(load_svg(app_, kSvgDot, argb(dots[v][t])));
    }
    set.all = load_svg(app_, kSvgGrid, argb(accent[v]));
    set.building = load_svg(app_, kSvgBuilding, argb(muted[v]));
    set.folder = load_svg(app_, kSvgFolder, argb(amber[v]));
    set.h_create = load_svg(app_, kSvgPlus, argb(green[v]));
    set.h_edit = load_svg(app_, kSvgPencil, argb(accent[v]));
    set.h_toggle = load_svg(app_, kSvgPower, argb(amber[v]));
    set.h_tag = load_svg(app_, kSvgTag, argb(muted[v]));
    set.undo = load_svg(app_, kSvgUndo, argb(muted[v]));
    set.plain_close = load_svg(app_, kSvgClose, argb(0xFF000000));
  }
}

// -- Small builders -----------------------------------------------------------

void Inventory::pad(NodeId node, float x, float y) {
  (void)w_.set_prop(node, DG_PROP_PADDING_L, dg::PropValue::number(x));
  (void)w_.set_prop(node, DG_PROP_PADDING_R, dg::PropValue::number(x));
  (void)w_.set_prop(node, DG_PROP_PADDING_T, dg::PropValue::number(y));
  (void)w_.set_prop(node, DG_PROP_PADDING_B, dg::PropValue::number(y));
}

void Inventory::radius(NodeId node, float px) {
  for (const std::uint16_t id : {DG_PROP_BORDER_RADIUS_TL, DG_PROP_BORDER_RADIUS_TR,
                                 DG_PROP_BORDER_RADIUS_BR, DG_PROP_BORDER_RADIUS_BL}) {
    (void)w_.set_prop(node, id, dg::PropValue::number(px));
  }
}

// One 1px border side (DG_PROP_BORDER_WIDTH_*) in the theme's border colour.
void Inventory::edge(NodeId node, std::uint16_t side) {
  (void)w_.set_prop(node, side, dg::PropValue::number(1));
  (void)w_.bind_token(node, DG_PROP_BORDER_COLOR, DG_TOKEN_COLOR_BORDER);
}

void Inventory::paint(NodeId node, std::uint16_t prop, Role role) {
  paints_.push_back({node, prop, role});
  (void)w_.set_prop(node, prop, dg::PropValue::color(color_of(role)));
}

dg::Color Inventory::color_of(Role role) const {
  const bool dark = variant_ == dg::ThemeVariant::kDark;
  switch (role) {
    case Role::kPage:
      return argb(dark ? 0xFF13161C : 0xFFF3F5F8);
    case Role::kPanel:
      return argb(dark ? 0xFF181C23 : 0xFFFFFFFF);
    case Role::kChrome:
      return argb(dark ? 0xFF1E232B : 0xFFF9FAFB);
    case Role::kMuted:
      return argb(dark ? 0xFF8D97A8 : 0xFF667085);
    case Role::kSoft:
      return argb(dark ? 0xFF1F2A40 : 0xFFE9EFFD);
    case Role::kInput:
      return argb(dark ? 0xFF13161C : 0xFFFFFFFF);
  }
  return argb(0xFFFFFFFF);
}

NodeId Inventory::box(NodeId parent, bool row, float gap) {
  const NodeId node = w_.create(NodeKind::kBox, parent).value();
  (void)w_.set(node, {row ? dg::prop::row() : dg::prop::column(), dg::prop::gap(gap),
                      dg::prop::background(dg::Color{}), dg::prop::width_auto(),
                      dg::prop::height_auto()});
  return node;
}

NodeId Inventory::label(NodeId parent, const std::string& text, float size, bool muted,
                        float width) {
  const NodeId node = w_.create(NodeKind::kLabel, parent).value();
  (void)w_.set(node,
               {dg::prop::font_size(size), dg::prop::height(std::max(20.0F, size * 1.6F))});
  if (width > 0) {
    (void)w_.set(node, dg::prop::width(width));
  } else {
    (void)w_.set(node, dg::prop::grow(1));
  }
  (void)w_.set_text(node, DG_PROP_TEXT, text);
  (void)w_.set_prop(node, DG_PROP_TEXT_ALIGN, dg::PropValue::option(DG_TEXT_ALIGN_LEFT));
  if (muted) {
    paint(node, DG_PROP_TEXT_COLOR, Role::kMuted);
  } else {
    (void)w_.bind_token(node, DG_PROP_TEXT_COLOR, DG_TOKEN_COLOR_ON_SURFACE);
  }
  return node;
}

NodeId Inventory::button(NodeId parent, const std::string& text, bool primary, float width) {
  const NodeId node = w_.create(NodeKind::kButton, parent).value();
  (void)w_.set(node,
               {dg::prop::width(width), dg::prop::height(32), dg::prop::font_size(13.5F)});
  (void)w_.set_text(node, DG_PROP_TEXT, text);
  (void)w_.set_prop(node, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_POINTER));
  radius(node, 6);
  if (!primary) {
    paint(node, DG_PROP_BACKGROUND_COLOR, Role::kPanel);
    (void)w_.bind_token(node, DG_PROP_TEXT_COLOR, DG_TOKEN_COLOR_ON_SURFACE);
    for (const std::uint16_t side : {DG_PROP_BORDER_WIDTH_L, DG_PROP_BORDER_WIDTH_T,
                                     DG_PROP_BORDER_WIDTH_R, DG_PROP_BORDER_WIDTH_B}) {
      edge(node, side);
    }
  }
  return node;
}

// A square toolbar button holding a token-tinted SVG image.
NodeId Inventory::icon_button(NodeId parent, const char* svg, const std::string& tip,
                              NodeId& image) {
  const NodeId node = w_.create(NodeKind::kButton, parent).value();
  (void)w_.set(node, {dg::prop::width(34), dg::prop::height(32), dg::prop::row()});
  // A button does not centre its children; pad the 18px icon into the middle.
  pad(node, 8, 7);
  (void)w_.set_prop(node, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_POINTER));
  paint(node, DG_PROP_BACKGROUND_COLOR, Role::kChrome);
  radius(node, 6);
  (void)w_.set_tooltip(node, tip);
  image = w_.create(NodeKind::kImage, node).value();
  (void)w_.set(image, {dg::prop::width(18), dg::prop::height(18)});
  (void)w_.set_image(image, load_svg(app_, svg, argb(0xFF000000)));
  (void)w_.bind_token(image, DG_PROP_IMAGE_TINT, DG_TOKEN_COLOR_ON_SURFACE);
  return node;
}

NodeId Inventory::field(NodeId parent, const std::string& placeholder) {
  const NodeId node = w_.create(NodeKind::kTextField, parent).value();
  (void)w_.set(node, {dg::prop::height(32), dg::prop::font_size(13.5F)});
  (void)w_.set_prop(node, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_TEXT));
  (void)w_.set_text(node, DG_PROP_PLACEHOLDER, placeholder);
  return node;
}

// -- The scene ----------------------------------------------------------------

void Inventory::build() {
  n_.root = w_.create(NodeKind::kBox).value();
  (void)w_.set(n_.root, {dg::prop::width_percent(100), dg::prop::height_percent(100),
                         dg::prop::column(), dg::prop::gap(0)});
  (void)w_.set_prop(n_.root, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_STRETCH));
  paint(n_.root, DG_PROP_BACKGROUND_COLOR, Role::kPage);

  build_menu_bar();
  build_toolbar();
  const NodeId body = box(n_.root, true, 0);
  (void)w_.set(body, {dg::prop::grow(1), dg::prop::width_percent(100)});
  (void)w_.set_prop(body, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_STRETCH));
  build_left(body);
  build_centre(body);
  build_right(body);
  build_status();

  // Table-scoped keys: they fire only while focus is inside the table, so
  // F2/Delete typed in a text field stay text edits.
  (void)w_.add_shortcut(n_.table, "F2", dg::kUserActionBase + kCmdRename);
  (void)w_.add_shortcut(n_.table, "Delete", dg::kUserActionBase + kCmdDelete);
  (void)w_.add_shortcut(n_.table, "Enter", dg::kUserActionBase + kCmdEdit);
  // App-wide keys.
  (void)app_.add_shortcut("Ctrl+N", dg::kUserActionBase + kCmdNew);
  (void)app_.add_shortcut("Ctrl+S", dg::kUserActionBase + kCmdSave);
  (void)app_.add_shortcut("Ctrl+Q", dg::kUserActionBase + kCmdQuit);
  (void)app_.add_shortcut("Ctrl+F", dg::kUserActionBase + kCmdFind);
}

void Inventory::build_menu_bar() {
  n_.menu_bar = w_.create(NodeKind::kMenuBar, n_.root).value();
  (void)w_.set(n_.menu_bar, dg::prop::width_percent(100));
  const auto key = [&](const char* chord) { return app_.chord_label(chord).value_or(""); };
  std::vector<dg::MenuBarMenu> menus(4);
  menus[0].title = "文件";
  menus[0].entries = {entry("新建资产…", kCmdNew, key("Ctrl+N")),
                      entry("保存", kCmdSave, key("Ctrl+S")), separator(),
                      entry("退出", kCmdQuit, key("Ctrl+Q"))};
  menus[1].title = "编辑";
  menus[1].entries = {entry("重命名…", kCmdRename, key("F2")),
                      entry("删除…", kCmdDelete, key("Delete")), separator(),
                      entry("查找", kCmdFind, key("Ctrl+F"))};
  menus[2].title = "视图";
  menus[2].entries = {entry("显示详情", kCmdShowDetail), entry("紧凑行", kCmdCompact),
                      entry("深色主题", kCmdDark)};
  menus[2].entries[0].checked = show_detail_;
  menus[2].entries[1].checked = compact_;
  menus[2].entries[2].checked = variant_ == dg::ThemeVariant::kDark;
  menus[3].title = "帮助";
  menus[3].entries = {entry("关于资产管理", kCmdAbout)};
  (void)w_.menu_bar(n_.menu_bar).value().set_menus(menus);
}

void Inventory::set_view_entry(int item, const std::string& text, bool on, Cmd cmd) {
  dg::MenuEntry e = entry(text, cmd);
  e.checked = on;
  (void)w_.menu_bar(n_.menu_bar).value().set_entry(2, item, e);
}

void Inventory::build_toolbar() {
  n_.toolbar = box(n_.root, true, 4);
  (void)w_.set(n_.toolbar, {dg::prop::height(48), dg::prop::width_percent(100)});
  (void)w_.set_prop(n_.toolbar, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_CENTER));
  pad(n_.toolbar, 12, 0);
  paint(n_.toolbar, DG_PROP_BACKGROUND_COLOR, Role::kChrome);
  edge(n_.toolbar, DG_PROP_BORDER_WIDTH_B);
  edge(n_.toolbar, DG_PROP_BORDER_WIDTH_T);

  const auto chord = [&](const char* c) { return app_.chord_label(c).value_or(""); };
  n_.btn_new =
      icon_button(n_.toolbar, kSvgPlus, "新建资产（" + chord("Ctrl+N") + "）", n_.icon_new);
  n_.btn_save =
      icon_button(n_.toolbar, kSvgSave, "保存（" + chord("Ctrl+S") + "）", n_.icon_save);
  n_.btn_delete = icon_button(n_.toolbar, kSvgTrash, "删除所选资产（" + chord("Delete") + "）",
                              n_.icon_delete);
  const NodeId rule = box(n_.toolbar, false, 0);
  (void)w_.set(rule, {dg::prop::width(1), dg::prop::height(22)});
  (void)w_.bind_token(rule, DG_PROP_BACKGROUND_COLOR, DG_TOKEN_COLOR_BORDER);
  (void)w_.set_prop(rule, DG_PROP_MARGIN_L, dg::PropValue::number(6));
  (void)w_.set_prop(rule, DG_PROP_MARGIN_R, dg::PropValue::number(6));

  n_.btn_filter = w_.create(NodeKind::kButton, n_.toolbar).value();
  (void)w_.set(n_.btn_filter, {dg::prop::width(92), dg::prop::height(30),
                               dg::prop::font_size(13.5F), dg::prop::checked(false)});
  (void)w_.set_text(n_.btn_filter, DG_PROP_TEXT, "只看在用");
  (void)w_.set_tooltip(n_.btn_filter, "筛选：只显示在用的资产");
  (void)w_.set_prop(n_.btn_filter, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_POINTER));
  radius(n_.btn_filter, 6);

  const NodeId spacer = box(n_.toolbar, true, 0);
  (void)w_.set(spacer, {dg::prop::grow(1), dg::prop::height(1)});

  const NodeId search = box(n_.toolbar, true, 6);
  (void)w_.set(search, {dg::prop::width(280), dg::prop::height(32)});
  (void)w_.set_prop(search, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_CENTER));
  const NodeId glass = w_.create(NodeKind::kImage, search).value();
  (void)w_.set(glass, {dg::prop::width(18), dg::prop::height(18)});
  (void)w_.set_image(glass, load_svg(app_, kSvgSearch, argb(0xFF000000)));
  paint(glass, DG_PROP_IMAGE_TINT, Role::kMuted);
  n_.search = field(search, "查找名称或负责人（" + chord("Ctrl+F") + "）");
  (void)w_.set(n_.search, dg::prop::grow(1));
}

void Inventory::build_left(NodeId body) {
  n_.left = box(body, false, 0);
  (void)w_.set(n_.left, dg::prop::width(236));
  (void)w_.set_prop(n_.left, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_STRETCH));
  paint(n_.left, DG_PROP_BACKGROUND_COLOR, Role::kPanel);
  edge(n_.left, DG_PROP_BORDER_WIDTH_R);
  const NodeId head = box(n_.left, true, 0);
  pad(head, 14, 8);
  label(head, "位置与部门", 12.5F, true);
  n_.tree = w_.create(NodeKind::kTree, n_.left).value();
  (void)w_.set(n_.tree, {dg::prop::grow(1), dg::prop::height_auto()});
  (void)w_.set_prop(n_.tree, DG_PROP_MARGIN_L, dg::PropValue::number(8));
  (void)w_.set_prop(n_.tree, DG_PROP_MARGIN_R, dg::PropValue::number(8));
  (void)w_.set_prop(n_.tree, DG_PROP_MARGIN_B, dg::PropValue::number(8));
}

void Inventory::build_centre(NodeId body) {
  n_.centre = box(body, false, 8);
  (void)w_.set(n_.centre, {dg::prop::grow(1)});
  (void)w_.set_prop(n_.centre, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_STRETCH));
  pad(n_.centre, 16, 12);
  const NodeId head = box(n_.centre, true, 10);
  (void)w_.set_prop(head, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_CENTER));
  label(head, "资产清单", 17, false, 80);
  n_.table_count = label(head, "", 13, true);
  label(head, "右键行可编辑、复制或删除；双击打开", 12, true, 230);
  n_.table = w_.create(NodeKind::kTable, n_.centre).value();
  (void)w_.set(n_.table, {dg::prop::grow(1), dg::prop::height_auto()});
  (void)w_.table(n_.table).value().set_columns({{"名称", std::nullopt, 33.0F, true},
                                                {"类型", std::nullopt, 15.0F, true},
                                                {"在用", std::nullopt, 10.0F, true},
                                                {"负责人", std::nullopt, 18.0F, true},
                                                {"价值", std::nullopt, std::nullopt, true}});
}

void Inventory::build_right(NodeId body) {
  n_.right = box(body, false, 6);
  (void)w_.set(n_.right, dg::prop::width(330));
  (void)w_.set_prop(n_.right, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_STRETCH));
  (void)w_.set_prop(n_.right, DG_PROP_OVERFLOW, dg::PropValue::option(DG_OVERFLOW_CLIP));
  paint(n_.right, DG_PROP_BACKGROUND_COLOR, Role::kPanel);
  edge(n_.right, DG_PROP_BORDER_WIDTH_L);
  pad(n_.right, 14, 10);
  (void)w_.set(label(n_.right, "详情", 12.5F, true), dg::prop::grow(0));
  n_.detail_title = label(n_.right, "", 17, false);
  (void)w_.set(n_.detail_title, dg::prop::grow(0));

  n_.tabs = w_.create(NodeKind::kTabs, n_.right).value();
  (void)w_.set(n_.tabs, {dg::prop::grow(1)});
  dg::TabsRef tabs = w_.tabs(n_.tabs).value();
  const NodeId general = tabs.add("常规").value();
  const NodeId hist = tabs.add("历史").value();
  const NodeId tags = tabs.add("标签").value();
  for (const NodeId page : {general, hist, tags}) {
    (void)w_.set(page, {dg::prop::column(), dg::prop::gap(8), dg::prop::width_percent(100),
                        dg::prop::height_percent(100)});
    (void)w_.set_prop(page, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_STRETCH));
    paint(page, DG_PROP_BACKGROUND_COLOR, Role::kPanel);
    pad(page, 2, 12);
  }

  // 常规: a small form.
  const auto row = [&](const std::string& caption) {
    const NodeId r = box(general, true, 10);
    (void)w_.set_prop(r, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_CENTER));
    label(r, caption, 13, true, 56);
    return r;
  };
  n_.f_name = field(row("名称"), "资产名称");
  (void)w_.set(n_.f_name, dg::prop::grow(1));
  n_.f_owner = field(row("负责人"), "姓名或小组");
  (void)w_.set(n_.f_owner, dg::prop::grow(1));
  n_.f_type = w_.create(NodeKind::kDropdown, row("类型")).value();
  (void)w_.set(n_.f_type,
               {dg::prop::grow(1), dg::prop::height(32), dg::prop::font_size(13.5F)});
  (void)w_.dropdown(n_.f_type).value().set_items(
      std::vector<std::string>(kTypeNames, kTypeNames + kTypeCount));
  const NodeId check_row = row("状态");
  n_.f_in_use = w_.create(NodeKind::kCheckbox, check_row).value();
  (void)w_.set(n_.f_in_use, {dg::prop::width(22), dg::prop::height(22)});
  pad(n_.f_in_use, 4, 4);
  label(check_row, "在用", 13, false);
  const NodeId actions = box(general, true, 8);
  (void)w_.set_prop(actions, DG_PROP_MARGIN_T, dg::PropValue::number(6));
  (void)w_.set_prop(actions, DG_PROP_JUSTIFY, dg::PropValue::option(DG_JUSTIFY_END));
  n_.f_revert = button(actions, "还原", false, 72);
  n_.f_apply = button(actions, "应用", true, 72);
  (void)w_.set_tooltip(n_.f_revert, "放弃这里未应用的修改");

  // 历史: a rich list with an undo button per row.
  n_.history = w_.create(NodeKind::kList, hist).value();
  (void)w_.set(n_.history, {dg::prop::grow(1), dg::prop::height_auto()});

  // 标签: add box + a scroll view of checkbox / name / remove rows.
  const NodeId add = box(tags, true, 8);
  n_.tag_field = field(add, "新标签");
  (void)w_.set(n_.tag_field, dg::prop::grow(1));
  n_.tag_add = button(add, "添加", false, 64);
  n_.tag_scroll = w_.create(NodeKind::kScrollView, tags).value();
  (void)w_.set(n_.tag_scroll, {dg::prop::grow(1), dg::prop::column(), dg::prop::gap(2)});
  (void)w_.set_prop(n_.tag_scroll, DG_PROP_SCROLL_AXIS,
                    dg::PropValue::option(DG_SCROLL_AXIS_VERTICAL));
  (void)w_.set_prop(n_.tag_scroll, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_STRETCH));
  n_.tag_empty = label(tags, "", 12.5F, true);
  (void)w_.set(n_.tag_empty, dg::prop::grow(0));
}

void Inventory::build_status() {
  const NodeId bar = box(n_.root, true, 12);
  (void)w_.set(bar, {dg::prop::height(28), dg::prop::width_percent(100)});
  (void)w_.set_prop(bar, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_CENTER));
  pad(bar, 14, 0);
  paint(bar, DG_PROP_BACKGROUND_COLOR, Role::kChrome);
  edge(bar, DG_PROP_BORDER_WIDTH_T);
  n_.status = label(bar, "", 12.5F, false);
  n_.last_action = label(bar, "", 12.5F, true, 420);
  (void)w_.set_prop(n_.last_action, DG_PROP_TEXT_ALIGN,
                    dg::PropValue::option(DG_TEXT_ALIGN_RIGHT));
}

// -- Model --------------------------------------------------------------------

const Asset* Inventory::find(std::uint64_t key) const {
  for (const Asset& a : assets_) {
    if (a.id == key) {
      return &a;
    }
  }
  return nullptr;
}

Asset* Inventory::find_mut(std::uint64_t key) {
  return const_cast<Asset*>(std::as_const(*this).find(key));  // NOLINT
}

std::uint64_t Inventory::target(std::uint64_t key) const {
  return key != 0 ? key : selected_;
}

std::string Inventory::now() {
  char text[16];
  std::snprintf(text, sizeof text, "%02d:%02d", (clock_ / 60) % 24, clock_ % 60);
  clock_ += 7;
  return text;
}

void Inventory::record(const Asset& before, int kind, const std::string& text) {
  HistoryEntry h;
  h.id = next_history_++;
  h.asset = before.id;
  h.kind = kind;
  h.text = text;
  h.time = now();
  h.before = before;
  history_.push_back(std::move(h));
}

void Inventory::mark_dirty() {
  dirty_ = true;
}

bool Inventory::passes(const Asset& a) const {
  if (filter_in_use_ && !a.in_use) {
    return false;
  }
  if (tree_key_ >= kTreeDept) {
    if (static_cast<std::uint64_t>(a.dept) != tree_key_ - kTreeDept) {
      return false;
    }
  } else if (tree_key_ >= kTreeLocation) {
    const auto location = static_cast<int>(tree_key_ - kTreeLocation);
    const auto dept = std::find_if(depts_.begin(), depts_.end(),
                                   [&](const Dept& d) { return d.id == a.dept; });
    if (dept == depts_.end() || dept->location != location) {
      return false;
    }
  }
  if (!search_.empty()) {
    const std::string needle = lower(search_);
    if (lower(a.name).find(needle) == std::string::npos &&
        lower(a.owner).find(needle) == std::string::npos) {
      return false;
    }
  }
  return true;
}

// -- Model -> view --------------------------------------------------------------

void Inventory::refresh_all() {
  refresh_tree();
  refresh_table();
  refresh_detail();
  refresh_status();
}

void Inventory::refresh_table() {
  std::vector<const Asset*> rows;
  for (const Asset& a : assets_) {
    if (passes(a)) {
      rows.push_back(&a);
    }
  }
  if (sort_column_ >= 0) {
    const int c = sort_column_;
    const bool asc = sort_ascending_;
    std::stable_sort(rows.begin(), rows.end(), [&](const Asset* x, const Asset* y) {
      const auto less = [&](const Asset* p, const Asset* q) {
        switch (c) {
          case kColName:
            return p->name < q->name;
          case kColType:
            return p->type < q->type;
          case kColInUse:
            return p->in_use && !q->in_use;
          case kColOwner:
            return p->owner < q->owner;
          default:
            return p->value < q->value;
        }
      };
      return asc ? less(x, y) : less(y, x);
    });
  }
  const Icons& ic = icons();
  std::vector<std::vector<dg::TableCell>> cells;
  visible_.clear();
  for (const Asset* a : rows) {
    const auto t = static_cast<std::size_t>(a->type);
    cells.push_back({{a->name, ic.type[t]},
                     {kTypeNames[t], ic.dot[t]},
                     {a->in_use ? "在用" : "闲置", dg::ImageId{}, a->in_use},
                     {a->owner},
                     {money(a->value)}});
    visible_.push_back(a->id);
  }
  dg::TableRef table = w_.table(n_.table).value();
  (void)table.set_cells(std::move(cells));
  (void)table.set_row_keys(visible_);
  if (sort_column_ >= 0) {
    (void)table.set_sort_indicator(sort_column_, sort_ascending_);
  }
  if (selected_ != 0 && table.select_key(selected_) != dg::Status::kOk) {
    (void)table.select(-1);
  }
  (void)w_.set_text(n_.table_count, DG_PROP_TEXT,
                    "共 " + std::to_string(visible_.size()) + " 项");
}

void Inventory::refresh_tree() {
  const Icons& ic = icons();
  std::vector<dg::TreeEntry> items;
  const auto count = [&](const auto& pred) {
    return std::to_string(std::count_if(assets_.begin(), assets_.end(), pred));
  };
  std::size_t slot = 0;
  items.push_back({"全部资产", 0, tree_expanded_[slot++], ic.all,
                   count([](const Asset&) { return true; }), std::nullopt, kTreeAll});
  for (std::size_t l = 0; l < locations_.size(); ++l) {
    const int loc = static_cast<int>(l);
    items.push_back({locations_[l], 1, tree_expanded_[slot++], ic.building,
                     count([&](const Asset& a) {
                       return std::any_of(depts_.begin(), depts_.end(), [&](const Dept& d) {
                         return d.id == a.dept && d.location == loc;
                       });
                     }),
                     std::nullopt, kTreeLocation + l});
    for (const Dept& d : depts_) {
      if (d.location == loc) {
        items.push_back({d.name, 2, tree_expanded_[slot++], ic.folder,
                         count([&](const Asset& a) { return a.dept == d.id; }), std::nullopt,
                         kTreeDept + static_cast<std::uint64_t>(d.id)});
      }
    }
  }
  dg::TreeRef tree = w_.tree(n_.tree).value();
  (void)tree.set_items(std::move(items));
  (void)tree.select_key(tree_key_);
}

void Inventory::refresh_detail() {
  const Asset* a = find(selected_);
  (void)w_.set_text(n_.detail_title, DG_PROP_TEXT, a != nullptr ? a->name : "未选择资产");
  for (const NodeId node :
       {n_.f_name, n_.f_owner, n_.f_type, n_.f_in_use, n_.tag_field, n_.tag_add}) {
    (void)w_.set_disabled(node, a == nullptr);
  }
  (void)w_.set_text(n_.f_name, DG_PROP_TEXT, a != nullptr ? a->name : "");
  (void)w_.set_text(n_.f_owner, DG_PROP_TEXT, a != nullptr ? a->owner : "");
  (void)w_.dropdown(n_.f_type).value().select(a != nullptr ? a->type : -1);
  (void)w_.set(n_.f_in_use, dg::prop::checked(a != nullptr && a->in_use));
  form_dirty_ = false;
  (void)w_.set_disabled(n_.f_apply, true);
  (void)w_.set_disabled(n_.f_revert, true);
  refresh_history();
  refresh_tags();
}

void Inventory::refresh_history() {
  const Icons& ic = icons();
  std::vector<dg::RowItem> rows;
  for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
    if (it->asset != selected_) {
      continue;
    }
    const dg::ImageId icon = it->kind == 0   ? ic.h_create
                             : it->kind == 2 ? ic.h_toggle
                             : it->kind == 3 ? ic.h_tag
                                             : ic.h_edit;
    rows.push_back({it->text, it->time, icon, std::nullopt,
                    static_cast<std::uint8_t>(it->kind == 0 ? 0 : 1), it->id});
  }
  dg::ListRef list = w_.list(n_.history).value();
  (void)list.set_row_actions({{ic.undo, "撤销这次修改"}});
  (void)list.set_rows(std::move(rows), compact_ ? 30 : 40);
}

void Inventory::refresh_tags() {
  for (const NodeId row : n_.tag_rows) {
    (void)w_.remove(row);
  }
  n_.tag_rows.clear();
  const Asset* a = find(selected_);
  const float h = compact_ ? 28.0F : 36.0F;
  if (a != nullptr) {
    for (const Tag& t : a->tags) {
      const NodeId row = box(n_.tag_scroll, true, 8);
      (void)w_.set(row, {dg::prop::height(h)});
      (void)w_.set_prop(row, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_CENTER));
      pad(row, 8, 0);
      radius(row, 6);
      paint(row, DG_PROP_BACKGROUND_COLOR, Role::kChrome);
      const NodeId check = w_.create(NodeKind::kCheckbox, row).value();
      (void)w_.set(check, {dg::prop::width(20), dg::prop::height(20), dg::prop::checked(t.on)});
      pad(check, 3, 3);
      (void)w_.set_user_data(check, t.id);
      (void)w_.set_tooltip(check, "启用或停用这个标签");
      const NodeId name = label(row, t.name, 13, !t.on);
      (void)name;
      const NodeId remove = w_.create(NodeKind::kButton, row).value();
      (void)w_.set(remove, {dg::prop::width(26), dg::prop::height(26), dg::prop::row()});
      pad(remove, 6, 6);
      paint(remove, DG_PROP_BACKGROUND_COLOR, Role::kChrome);
      radius(remove, 13);
      (void)w_.set_user_data(remove, t.id);
      (void)w_.set_tooltip(remove, "移除标签「" + t.name + "」");
      const NodeId x = w_.create(NodeKind::kImage, remove).value();
      (void)w_.set(x, {dg::prop::width(14), dg::prop::height(14)});
      (void)w_.set_image(x, icons().plain_close);
      paint(x, DG_PROP_IMAGE_TINT, Role::kMuted);
      n_.tag_rows.push_back(row);
    }
  }
  // Paint records of removed rows are dead weight; drop them.
  paints_.erase(std::remove_if(paints_.begin(), paints_.end(),
                               [&](const Paint& p) { return !w_.kind_of(p.node).has_value(); }),
                paints_.end());
  (void)w_.set_text(n_.tag_empty, DG_PROP_TEXT,
                    a == nullptr ? "选择一项资产后在这里管理标签"
                    : a->tags.empty()
                        ? "还没有标签"
                        : std::to_string(a->tags.size()) + " 个标签 · 取消勾选可暂停使用");
}

void Inventory::refresh_status() {
  const Asset* a = find(selected_);
  std::string text = std::to_string(assets_.size()) + " 项资产";
  if (visible_.size() != assets_.size()) {
    text += "（显示 " + std::to_string(visible_.size()) + " 项）";
  }
  text += " · 已选 " + (a != nullptr ? a->name : std::string("无"));
  if (dirty_) {
    text += " · 未保存";
  }
  (void)w_.set_text(n_.status, DG_PROP_TEXT, text);
  (void)w_.set_text(n_.last_action, DG_PROP_TEXT, last_);
  (void)w_.set_title(dirty_ ? "资产管理 *" : "资产管理");
}

void Inventory::set_last(const std::string& text) {
  last_ = text;
  refresh_status();
}

void Inventory::select_asset(std::uint64_t key) {
  if (key == selected_) {
    return;
  }
  selected_ = key;
  refresh_detail();
  refresh_status();
}

// -- Detail form ----------------------------------------------------------------

void Inventory::form_changed() {
  const Asset* a = find(selected_);
  if (a == nullptr) {
    return;
  }
  const std::string name = w_.text(n_.f_name).value_or("");
  form_dirty_ = name != a->name || w_.text(n_.f_owner).value_or("") != a->owner ||
                w_.dropdown(n_.f_type).value().selected().value_or(a->type) != a->type ||
                w_.checked(n_.f_in_use).value_or(a->in_use) != a->in_use;
  (void)w_.set_disabled(n_.f_apply, !form_dirty_ || name.empty());
  (void)w_.set_disabled(n_.f_revert, !form_dirty_);
}

void Inventory::apply_form() {
  Asset* a = find_mut(selected_);
  if (a == nullptr || !form_dirty_) {
    return;
  }
  const Asset before = *a;
  a->name = w_.text(n_.f_name).value_or(a->name);
  a->owner = w_.text(n_.f_owner).value_or(a->owner);
  a->type = std::max(0, w_.dropdown(n_.f_type).value().selected().value_or(a->type));
  a->in_use = w_.checked(n_.f_in_use).value_or(a->in_use);
  record(before, 1, "修改了常规信息");
  mark_dirty();
  last_ = "已应用对「" + a->name + "」的修改";
  refresh_all();
}

// -- Commands -------------------------------------------------------------------

void Inventory::run(Cmd cmd, std::uint64_t key) {
  switch (cmd) {
    case kCmdNone:
      return;
    case kCmdNew: {
      int dept = 1;
      if (tree_key_ >= kTreeDept) {
        dept = static_cast<int>(tree_key_ - kTreeDept);
      } else if (tree_key_ >= kTreeLocation) {
        const auto loc = static_cast<int>(tree_key_ - kTreeLocation);
        for (const Dept& d : depts_) {
          if (d.location == loc) {
            dept = d.id;
            break;
          }
        }
      }
      open_asset_dialog(DialogKind::kNew, 0, dept);
      return;
    }
    case kCmdNewHere: {
      // `key` is a tree key here.
      const std::uint64_t saved = tree_key_;
      tree_key_ = key != 0 ? key : tree_key_;
      run(kCmdNew);
      tree_key_ = saved;
      return;
    }
    case kCmdSave:
      ++saves_;
      dirty_ = false;
      set_last("已保存 " + std::to_string(assets_.size()) + " 项资产");
      return;
    case kCmdQuit:
      if (dirty_) {
        open_plain_dialog(DialogKind::kQuit, "退出前保存修改？",
                          "有尚未保存的修改。不保存的话，这些修改会丢失。",
                          {"保存并退出", "不保存", "取消"}, 0, 2);
      } else {
        quit_ = true;
        app_.quit();
      }
      return;
    case kCmdFind:
      (void)w_.focus(n_.search);
      set_last("在搜索框中输入关键字");
      return;
    case kCmdShowDetail:
      show_detail_ = !show_detail_;
      // No visibility property: collapse the panel to zero width instead.
      (void)w_.set(n_.right, dg::prop::width(show_detail_ ? 330.0F : 0.0F));
      pad(n_.right, show_detail_ ? 14.0F : 0.0F, show_detail_ ? 10.0F : 0.0F);
      (void)w_.set_prop(n_.right, DG_PROP_BORDER_WIDTH_L,
                        dg::PropValue::number(show_detail_ ? 1.0F : 0.0F));
      (void)w_.set_disabled(n_.right, !show_detail_);
      set_view_entry(0, "显示详情", show_detail_, kCmdShowDetail);
      set_last(show_detail_ ? "已显示详情面板" : "已隐藏详情面板");
      return;
    case kCmdCompact:
      compact_ = !compact_;
      set_view_entry(1, "紧凑行", compact_, kCmdCompact);
      refresh_history();
      refresh_tags();
      set_last(compact_ ? "已切换为紧凑行" : "已切换为标准行高");
      return;
    case kCmdDark: {
      variant_ = variant_ == dg::ThemeVariant::kDark ? dg::ThemeVariant::kLight
                                                     : dg::ThemeVariant::kDark;
      (void)app_.set_theme_variant(variant_);
      for (const Paint& p : paints_) {
        (void)w_.set_prop(p.node, p.prop, dg::PropValue::color(color_of(p.role)));
      }
      set_view_entry(2, "深色主题", variant_ == dg::ThemeVariant::kDark, kCmdDark);
      refresh_tree();
      refresh_table();
      refresh_history();
      set_last(variant_ == dg::ThemeVariant::kDark ? "已切换到深色主题" : "已切换到浅色主题");
      return;
    }
    case kCmdAbout:
      open_plain_dialog(DialogKind::kAbout, "关于资产管理",
                        "资产管理 1.0 —— drawgui 示例程序。\n菜单、快捷键、右键菜单和对话框"
                        "都走同一套命令。",
                        {"好"}, 0, 0);
      return;
    default:
      break;
  }

  // Commands on one asset.
  const std::uint64_t id = target(key);
  Asset* a = find_mut(id);
  if (cmd == kCmdRenameNode) {
    std::string current;
    if (key >= kTreeDept) {
      for (const Dept& d : depts_) {
        if (static_cast<std::uint64_t>(d.id) == key - kTreeDept) {
          current = d.name;
        }
      }
    } else if (key >= kTreeLocation && key - kTreeLocation < locations_.size()) {
      current = locations_[key - kTreeLocation];
    } else {
      set_last("「全部资产」不能重命名");
      return;
    }
    open_rename_dialog(DialogKind::kRenameNode, key, current);
    return;
  }
  if (a == nullptr) {
    set_last("请先选择一项资产");
    return;
  }
  switch (cmd) {
    case kCmdRename:
      open_rename_dialog(DialogKind::kRename, id, a->name);
      break;
    case kCmdEdit:
      open_asset_dialog(DialogKind::kEdit, id, a->dept);
      break;
    case kCmdDelete:
      dialog_target_ = id;
      open_plain_dialog(DialogKind::kDelete, "删除资产？",
                        "「" + a->name + "」将从清单中删除。保存前仍可通过不保存退出来放弃。",
                        {"删除", "取消"}, 1, 1);
      break;
    case kCmdCopy: {
      Asset copy = *a;
      copy.id = next_asset_++;
      copy.name += "（副本）";
      for (Tag& t : copy.tags) {
        t.id = next_tag_++;
      }
      const std::string name = copy.name;
      assets_.push_back(copy);
      record(copy, 0, "从「" + a->name + "」复制");
      mark_dirty();
      selected_ = copy.id;
      last_ = "已复制为「" + name + "」";
      refresh_all();
      break;
    }
    case kCmdToggleInUse: {
      const Asset before = *a;
      a->in_use = !a->in_use;
      record(before, 2, a->in_use ? "标记为在用" : "标记为闲置");
      mark_dirty();
      last_ = "「" + a->name + "」" + (a->in_use ? "已标记为在用" : "已标记为闲置");
      refresh_all();
      break;
    }
    default:
      break;
  }
}

// -- Popups ---------------------------------------------------------------------

void Inventory::open_row_menu(std::uint64_t key, dg::PixelPoint at) {
  const Asset* a = find(key);
  if (a == nullptr) {
    return;
  }
  const auto chord = [&](const char* c) { return app_.chord_label(c).value_or(""); };
  std::vector<dg::MenuEntry> items = {
      entry("编辑…", kCmdEdit, chord("Enter")),
      entry("重命名…", kCmdRename, chord("F2")),
      entry("复制", kCmdCopy),
      separator(),
      entry(a->in_use ? "标记为闲置" : "标记为在用", kCmdToggleInUse),
      separator(),
      entry("删除…", kCmdDelete, chord("Delete"))};
  if (w_.open_menu(std::nullopt, at, items)) {
    menu_target_ = key;
  }
}

void Inventory::open_tree_menu(std::uint64_t key, dg::PixelPoint at) {
  std::vector<dg::MenuEntry> items = {entry("在此新建资产…", kCmdNewHere),
                                      entry("重命名…", kCmdRenameNode)};
  items[1].disabled = key == kTreeAll;
  if (w_.open_menu(std::nullopt, at, items)) {
    menu_target_ = key;
  }
}

void Inventory::open_plain_dialog(DialogKind kind, const std::string& title,
                                  const std::string& body, std::vector<std::string> buttons,
                                  int default_button, int cancel) {
  dg::DialogOptions options;
  options.title = title;
  options.body = body;
  options.buttons = std::move(buttons);
  options.default_button = default_button;
  options.cancel_button = cancel;
  const dg::Expected<std::int32_t, dg::Status> id = w_.open_dialog(options);
  if (id) {
    dialog_ = kind;
    dialog_id_ = id.value();
  }
}

void Inventory::open_asset_dialog(DialogKind kind, std::uint64_t key, int dept) {
  const Asset* a = find(key);
  dialog_target_ = key;
  dialog_dept_ = dept;
  // The form is built fresh for each dialog under the root and handed over
  // as DialogOptions::content; close_dialog_form() removes it again.
  const NodeId form = box(n_.root, false, 10);
  (void)w_.set_prop(form, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_STRETCH));
  const auto row = [&](const std::string& caption) {
    const NodeId r = box(form, true, 10);
    (void)w_.set_prop(r, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_CENTER));
    label(r, caption, 13, true, 70);
    return r;
  };
  n_.dlg_form = form;
  n_.dlg_name = field(row("名称"), "必填");
  (void)w_.set(*n_.dlg_name, dg::prop::grow(1));
  n_.dlg_owner = field(row("负责人"), "姓名或小组");
  (void)w_.set(*n_.dlg_owner, dg::prop::grow(1));
  n_.dlg_value = field(row("价值（元）"), "0");
  (void)w_.set(*n_.dlg_value, dg::prop::grow(1));
  // A dropdown cannot open inside a modal dialog (no nested popups), so the
  // type is a radio group here.
  const NodeId types = box(row("类型"), true, 6);
  (void)w_.set(types, {dg::prop::grow(1)});
  (void)w_.set_prop(types, DG_PROP_WRAP, dg::PropValue::boolean(true));
  (void)w_.set_prop(types, DG_PROP_RUN_GAP, dg::PropValue::number(8));
  (void)w_.set_prop(types, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_CENTER));
  n_.dlg_types.clear();
  for (int t = 0; t < kTypeCount; ++t) {
    const NodeId r = w_.create(NodeKind::kRadio, types).value();
    (void)w_.set(r, {dg::prop::width(18), dg::prop::height(18)});
    pad(r, 4, 4);
    (void)w_.radio(r).value().set_group(29);
    label(types, kTypeNames[t], 13, false, t == 4 ? 34.0F : 48.0F);
    n_.dlg_types.push_back(r);
  }
  const int type = a != nullptr ? a->type : 0;
  (void)w_.radio(n_.dlg_types[static_cast<std::size_t>(type)]).value().select();
  if (a != nullptr) {
    (void)w_.set_text(*n_.dlg_name, DG_PROP_TEXT, a->name);
    (void)w_.set_text(*n_.dlg_owner, DG_PROP_TEXT, a->owner);
    (void)w_.set_text(*n_.dlg_value, DG_PROP_TEXT, std::to_string(a->value));
  }

  std::string where;
  for (const Dept& d : depts_) {
    if (d.id == dept) {
      where = locations_[static_cast<std::size_t>(d.location)] + " / " + d.name;
    }
  }
  dg::DialogOptions options;
  options.title = kind == DialogKind::kNew ? "新建资产" : "编辑资产";
  options.body = kind == DialogKind::kNew ? "登记到：" + where : "所属：" + where;
  options.buttons = {kind == DialogKind::kNew ? "创建" : "确定", "取消"};
  options.default_button = 0;
  options.cancel_button = 1;
  options.content = form;
  const dg::Expected<std::int32_t, dg::Status> id = w_.open_dialog(options);
  if (!id) {
    close_dialog_form();
    return;
  }
  dialog_ = kind;
  dialog_id_ = id.value();
  (void)w_.focus(*n_.dlg_name);
  update_dialog_ok();
}

void Inventory::open_rename_dialog(DialogKind kind, std::uint64_t key,
                                   const std::string& current) {
  dialog_target_ = key;
  const NodeId form = box(n_.root, false, 8);
  (void)w_.set_prop(form, DG_PROP_ALIGN, dg::PropValue::option(DG_ALIGN_STRETCH));
  n_.dlg_form = form;
  n_.dlg_name = field(form, "必填");
  (void)w_.set_text(*n_.dlg_name, DG_PROP_TEXT, current);
  dg::DialogOptions options;
  options.title = kind == DialogKind::kRename ? "重命名资产" : "重命名";
  options.body = "新名称：";
  options.buttons = {"确定", "取消"};
  options.default_button = 0;
  options.cancel_button = 1;
  options.content = form;
  const dg::Expected<std::int32_t, dg::Status> id = w_.open_dialog(options);
  if (!id) {
    close_dialog_form();
    return;
  }
  dialog_ = kind;
  dialog_id_ = id.value();
  (void)w_.focus(*n_.dlg_name);
  update_dialog_ok();
}

void Inventory::update_dialog_ok() {
  if (!n_.dlg_name.has_value()) {
    return;
  }
  const std::string name = w_.text(*n_.dlg_name).value_or("");
  const bool blank = name.find_first_not_of(' ') == std::string::npos;
  (void)w_.set_dialog_button_disabled(dialog_id_, 0, blank);
}

void Inventory::close_dialog_form() {
  if (n_.dlg_form.has_value()) {
    (void)w_.remove(*n_.dlg_form);
  }
  n_.dlg_form.reset();
  n_.dlg_name.reset();
  n_.dlg_owner.reset();
  n_.dlg_value.reset();
  n_.dlg_types.clear();
  paints_.erase(std::remove_if(paints_.begin(), paints_.end(),
                               [&](const Paint& p) { return !w_.kind_of(p.node).has_value(); }),
                paints_.end());
}

void Inventory::on_dialog(int button) {
  const DialogKind kind = dialog_;
  dialog_ = DialogKind::kNone;
  dialog_id_ = 0;
  const bool ok = button == 0;
  switch (kind) {
    case DialogKind::kNew:
    case DialogKind::kEdit:
      if (ok) {
        Asset a;
        const Asset* old = find(dialog_target_);
        if (old != nullptr) {
          a = *old;
        } else {
          a.id = next_asset_++;
          a.dept = dialog_dept_;
        }
        const Asset before = a;
        a.name = w_.text(*n_.dlg_name).value_or("");
        a.owner = w_.text(*n_.dlg_owner).value_or("");
        a.value = std::atoi(w_.text(*n_.dlg_value).value_or("0").c_str());
        for (std::size_t t = 0; t < n_.dlg_types.size(); ++t) {
          if (w_.checked(n_.dlg_types[t]).value_or(false)) {
            a.type = static_cast<int>(t);
          }
        }
        if (old != nullptr) {
          *find_mut(a.id) = a;
          record(before, 1, "编辑了资产信息");
          last_ = "已更新「" + a.name + "」";
        } else {
          assets_.push_back(a);
          record(a, 0, "登记入库");
          last_ = "已新建「" + a.name + "」";
        }
        selected_ = a.id;
        mark_dirty();
        refresh_all();
      } else {
        set_last("已取消");
      }
      break;
    case DialogKind::kRename:
      if (ok) {
        Asset* a = find_mut(dialog_target_);
        if (a != nullptr) {
          const Asset before = *a;
          a->name = w_.text(*n_.dlg_name).value_or(a->name);
          record(before, 1, "重命名为 " + a->name);
          mark_dirty();
          last_ = "已重命名为「" + a->name + "」";
          refresh_all();
        }
      } else {
        set_last("已取消重命名");
      }
      break;
    case DialogKind::kRenameNode:
      if (ok) {
        const std::string name = w_.text(*n_.dlg_name).value_or("");
        if (dialog_target_ >= kTreeDept) {
          for (Dept& d : depts_) {
            if (static_cast<std::uint64_t>(d.id) == dialog_target_ - kTreeDept) {
              d.name = name;
            }
          }
        } else if (dialog_target_ >= kTreeLocation) {
          locations_[dialog_target_ - kTreeLocation] = name;
        }
        mark_dirty();
        last_ = "已重命名为「" + name + "」";
        refresh_tree();
        refresh_status();
      }
      break;
    case DialogKind::kDelete:
      if (ok) {
        const auto it = std::find_if(assets_.begin(), assets_.end(),
                                     [&](const Asset& a) { return a.id == dialog_target_; });
        if (it != assets_.end()) {
          last_ = "已删除「" + it->name + "」";
          assets_.erase(it);
          if (selected_ == dialog_target_) {
            selected_ = 0;
          }
          mark_dirty();
          refresh_all();
        }
      } else {
        set_last("已取消删除");
      }
      break;
    case DialogKind::kQuit:
      if (button == 0) {
        run(kCmdSave);
      }
      if (button == 0 || button == 1) {
        quit_ = true;
        app_.quit();
      }
      break;
    case DialogKind::kAbout:
    case DialogKind::kNone:
      break;
  }
  close_dialog_form();
  // Back to the table, wherever the dialog was opened from.
  if (!w_.focused().has_value()) {
    (void)w_.focus(n_.table);
  }
}

// -- Events -----------------------------------------------------------------------

void Inventory::sort_by(int column) {
  sort_ascending_ = sort_column_ == column ? !sort_ascending_ : true;
  sort_column_ = column;
  refresh_table();
  const char* const names[] = {"名称", "类型", "在用", "负责人", "价值"};
  set_last(std::string("按「") + names[column] + "」" + (sort_ascending_ ? "升序" : "降序"));
}

void Inventory::undo(std::uint64_t history_key) {
  const auto it = std::find_if(history_.begin(), history_.end(),
                               [&](const HistoryEntry& h) { return h.id == history_key; });
  if (it == history_.end()) {
    return;
  }
  Asset* a = find_mut(it->asset);
  if (a == nullptr) {
    return;
  }
  const std::string what = it->text;
  *a = it->before;
  history_.erase(it);
  mark_dirty();
  last_ = "已撤销：" + what;
  refresh_table();
  refresh_tree();
  // Keep the detail form in step without dropping the user's tab/selection.
  refresh_detail();
  refresh_status();
}

void Inventory::add_tag() {
  Asset* a = find_mut(selected_);
  const std::string name = w_.text(n_.tag_field).value_or("");
  if (a == nullptr || name.empty()) {
    return;
  }
  const Asset before = *a;
  a->tags.push_back({next_tag_++, name, true});
  record(before, 3, "添加标签 " + name);
  (void)w_.set_text(n_.tag_field, DG_PROP_TEXT, "");
  mark_dirty();
  last_ = "已添加标签「" + name + "」";
  refresh_tags();
  refresh_history();
  refresh_status();
}

void Inventory::on_click(const dg::events::ClickEvent& e) {
  if (!e.node.has_value()) {
    return;
  }
  const NodeId node = *e.node;
  if (node == n_.btn_new) {
    run(kCmdNew);
  } else if (node == n_.btn_save) {
    run(kCmdSave);
  } else if (node == n_.btn_delete) {
    run(kCmdDelete);
  } else if (node == n_.btn_filter) {
    filter_in_use_ = w_.checked(n_.btn_filter).value_or(false);
    refresh_table();
    set_last(filter_in_use_ ? "只显示在用的资产" : "显示全部资产");
  } else if (node == n_.table && e.clicks >= 2 && e.item_key != 0) {
    run(kCmdEdit, e.item_key);
  } else if (node == n_.f_in_use) {
    form_changed();
  } else if (node == n_.f_apply) {
    apply_form();
  } else if (node == n_.f_revert) {
    refresh_detail();
    set_last("已还原未应用的修改");
  } else if (node == n_.tag_add) {
    add_tag();
  } else if (const std::uint64_t tag = w_.user_data(node).value_or(0); tag != 0) {
    // A tag row's checkbox or remove button: user_data names the tag.
    Asset* a = find_mut(selected_);
    if (a == nullptr) {
      return;
    }
    const auto it =
        std::find_if(a->tags.begin(), a->tags.end(), [&](const Tag& t) { return t.id == tag; });
    if (it == a->tags.end()) {
      return;
    }
    const Asset before = *a;
    if (w_.kind_of(node) == NodeKind::kCheckbox) {
      it->on = w_.checked(node).value_or(it->on);
      record(before, 3, (it->on ? "启用标签 " : "停用标签 ") + it->name);
      last_ = (it->on ? "已启用标签「" : "已停用标签「") + it->name + "」";
    } else {
      last_ = "已移除标签「" + it->name + "」";
      record(before, 3, "移除标签 " + it->name);
      a->tags.erase(it);
    }
    mark_dirty();
    refresh_tags();
    refresh_history();
    refresh_status();
  }
}

void Inventory::on_context(const dg::events::ContextClickEvent& e) {
  if (!e.node.has_value()) {
    return;
  }
  const dg::PixelPoint at{e.x, e.y};
  if (*e.node == n_.table) {
    std::uint64_t key = e.item_key;
    if (key == 0 && e.item.has_value()) {
      key = w_.table(n_.table).value().key_of(*e.item).value_or(0);
    }
    if (key != 0) {
      open_row_menu(key, at);
    }
  } else if (*e.node == n_.tree) {
    std::uint64_t key = e.item_key;
    if (key == 0 && e.item.has_value()) {
      key = w_.tree(n_.tree).value().key_of(*e.item).value_or(0);
    }
    if (key != 0) {
      open_tree_menu(key, at);
    }
  }
}

void Inventory::on_row_part(const dg::events::RowPartEvent& e) {
  if (e.node == n_.table && e.part == dg::events::RowPartEvent::Part::kCheck) {
    Asset* a = find_mut(e.item_key);
    if (a == nullptr) {
      return;
    }
    const Asset before = *a;
    a->in_use = w_.table(n_.table).value().cell_checked(e.item, e.column).value_or(!a->in_use);
    record(before, 2, a->in_use ? "标记为在用" : "标记为闲置");
    mark_dirty();
    last_ = "「" + a->name + "」" + (a->in_use ? "已标记为在用" : "已标记为闲置");
    refresh_table();  // keeps the selection by key
    if (a->id == selected_) {
      refresh_detail();
    }
    refresh_status();
  } else if (e.node == n_.history && e.part == dg::events::RowPartEvent::Part::kAction) {
    undo(e.item_key);
  }
}

void Inventory::on_text(NodeId node) {
  if (node == n_.search) {
    search_ = w_.text(n_.search).value_or("");
    refresh_table();
    refresh_status();
  } else if (n_.dlg_name.has_value() && node == *n_.dlg_name) {
    update_dialog_ok();
  } else if (node == n_.f_name || node == n_.f_owner) {
    form_changed();
  }
}

void Inventory::on_event(const dg::Event& event) {
  using namespace dg::events;  // NOLINT(google-build-using-namespace)
  std::visit(dg::Overloaded{
                 [&](const ClickEvent& e) { on_click(e); },
                 [&](const ContextClickEvent& e) { on_context(e); },
                 [&](const ActionEvent& e) {
                   if (e.action_id >= dg::kUserActionBase) {
                     run(static_cast<Cmd>(e.action_id - dg::kUserActionBase));
                   }
                 },
                 [&](const MenuEvent& e) {
                   const std::uint64_t key = e.bar_menu.has_value() ? 0 : menu_target_;
                   menu_target_ = 0;
                   run(static_cast<Cmd>(e.command), key);
                 },
                 [&](const TextChangedEvent& e) {
                   if (e.node.has_value()) {
                     on_text(*e.node);
                   }
                 },
                 [&](const ValueChangedEvent& e) {
                   if (e.node == n_.f_type) {
                     form_changed();
                   }
                 },
                 [&](const DialogResultEvent& e) {
                   if (e.dialog_id == dialog_id_) {
                     on_dialog(e.button);
                   }
                 },
                 [&](const TableEvent& e) {
                   if (e.node != n_.table) {
                     return;
                   }
                   if (e.kind == TableEvent::Kind::kSort) {
                     sort_by(e.index);
                   } else {
                     select_asset(w_.table(n_.table).value().key_of(e.index).value_or(0));
                   }
                 },
                 [&](const TreeEvent& e) {
                   dg::TreeRef tree = w_.tree(n_.tree).value();
                   if (e.kind == TreeEvent::Kind::kSelect) {
                     tree_key_ = tree.key_of(e.item).value_or(kTreeAll);
                     refresh_table();
                     refresh_status();
                     set_last("筛选位置：" + std::to_string(visible_.size()) + " 项");
                   } else if (static_cast<std::size_t>(e.item) < tree_expanded_.size()) {
                     tree_expanded_[static_cast<std::size_t>(e.item)] =
                         e.kind == TreeEvent::Kind::kExpand;
                   }
                 },
                 [&](const RowPartEvent& e) { on_row_part(e); },
                 [&](const FocusChangedEvent& e) { focus_ = e.node; },
                 [&](const auto&) {},
             },
             event);
}

}  // namespace inventory
