#include "showcase.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "drawgui/app/prop.h"
#include "drawgui/layout/layout_tree.h"
#include "drawgui/render/image_catalog.h"
#include "drawgui/render/prop_ids.generated.h"
#include "drawgui/render/render_tree.h"
#include "drawgui/theme/theme_loader.h"
#include "drawgui/theme/token_ids.generated.h"
#include "drawgui/widget/widget_set.h"

namespace showcase {
namespace {

using dg::NodeId;
using dg::NodeKind;

constexpr dg::Color argb(std::uint32_t v) {
  return dg::Color::from_argb(v);
}

struct Palette {
  dg::Color page;
  dg::Color subtle;
  dg::Color muted;
  dg::Color accent_soft;
  dg::Color chip;
  dg::Color header;
};

Palette palette(dg::ThemeVariant variant) {
  if (variant == dg::ThemeVariant::kDark) {
    return {argb(0xFF0F1218), argb(0xFF252B37), argb(0xFF8D96A5),
            argb(0xFF2A2950), argb(0xFF8F8CF6), argb(0xFF171B23)};
  }
  return {argb(0xFFF3F4F8), argb(0xFFEEF0F5), argb(0xFF5F6775),
          argb(0xFFE4E3FB), argb(0xFF6D6AE8), argb(0xFFFFFFFF)};
}

const char* const kTabNames[kTabCount] = {"基础控件", "文本", "布局", "数据",
                                          "弹层",     "图片", "窗口"};

const char* const kTextAreaText =
    "drawgui 的多行文本框支持中文、English 与 emoji 混排 🎨✨。\n"
    "这一段故意写得很长，用来演示自动换行：当一行放不下时，"
    "中文可以在任意两个字之间断开，而 English words wrap at spaces "
    "so that no word is split in the middle. 🚀\n"
    "\n"
    "试试看：点击这里，用方向键移动光标，Shift+方向键选择文字，"
    "Ctrl+C / Ctrl+V 复制粘贴，Ctrl+Z 撤销。滚轮可以上下滚动。\n"
    "第五行：数字 0123456789，标点「引号」、《书名号》……\n"
    "第六行：Lorem ipsum dolor sit amet, consectetur adipiscing elit. 🌏\n"
    "第七行：最后一行，确保内容超出可见区域，出现滚动条。";

const char* const kParagraph =
    "这是一段会自动换行的说明文字。标签的宽度固定，文字超出宽度时自动折到下一行，"
    "中英文混排也能正确断行 (mixed scripts wrap correctly)。字号、颜色都跟随主题，"
    "切换到深色模式时会一起变化。";

// Monochrome SVG icons (24x24 viewBox); the catalog tints them.
const char* const kIcons[] = {
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24"><path d="M12 3 2 12h3v8h5v-6h4v6h5v-8h3z"/></svg>)",
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24"><path d="M12 2l3.09 6.26L22 9.27l-5 4.87 1.18 6.88L12 17.77l-6.18 3.25L7 14.14 2 9.27l6.91-1.01z"/></svg>)",
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24"><path d="M12 21.35l-1.45-1.32C5.4 15.36 2 12.28 2 8.5 2 5.42 4.42 3 7.5 3c1.74 0 3.41.81 4.5 2.09C13.09 3.81 14.76 3 16.5 3 19.58 3 22 5.42 22 8.5c0 3.78-3.4 6.86-8.55 11.54z"/></svg>)",
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24"><path d="M12 22a2 2 0 0 0 2-2h-4a2 2 0 0 0 2 2zm6-6v-5c0-3.07-1.64-5.64-4.5-6.32V4a1.5 1.5 0 0 0-3 0v.68C7.63 5.36 6 7.92 6 11v5l-2 2v1h16v-1z"/></svg>)",
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24"><path d="M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zm-2 15-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8z"/></svg>)",
};

const std::vector<std::string> kDropdownItems = {"北京", "上海", "广州",  "深圳",
                                                 "杭州", "成都", "Tokyo", "Berlin"};
const std::vector<std::string> kMenuItems = {"复制",     "粘贴",           "",
                                             "显示网格", "删除（不可用）", "属性…"};
const std::vector<std::string> kTitles = {"drawgui 组件展示", "标题改好了 ✓",
                                          "drawgui showcase — 第三个标题"};

std::vector<dg::TreeEntry> tree_items() {
  std::vector<dg::TreeEntry> items;
  const std::vector<std::pair<std::string, std::vector<std::string>>> folders = {
      {"文档", {"周报.md", "设计稿.fig", "README.txt"}},
      {"图片", {"头像.png", "封面.jpg", "插图.webp", "图标.svg"}},
      {"源代码", {"main.cpp", "showcase.h", "CMakeLists.txt"}},
      {"音乐", {"夜曲.flac", "Morning.mp3"}},
      {"下载", {"安装包.deb", "论文.pdf"}},
  };
  bool first = true;
  for (const auto& [name, files] : folders) {
    items.push_back({name, 0, first});
    for (const std::string& file : files) {
      items.push_back({file, 1, false});
    }
    first = false;
  }
  return items;
}

std::vector<std::vector<std::string>> table_rows() {
  const char* const names[] = {"苹果", "香蕉", "樱桃", "葡萄", "芒果", "橙子", "梨",
                               "桃子", "西瓜", "草莓", "蓝莓", "柠檬", "椰子", "荔枝"};
  const char* const origins[] = {"山东", "海南", "智利", "新疆", "广西", "江西", "河北"};
  std::vector<std::vector<std::string>> rows;
  for (int i = 0; i < 14; ++i) {
    const int price = 3 + ((i * 37) % 29);
    const int stock = (i * 113) % 500;
    rows.push_back({names[i], std::to_string(price), std::to_string(stock), origins[i % 7]});
  }
  return rows;
}

std::vector<std::string> list_items() {
  std::vector<std::string> items;
  items.reserve(kListItems);
  for (int i = 1; i <= kListItems; ++i) {
    items.push_back("第 " + std::to_string(i) + " 项 · item #" + std::to_string(i));
  }
  return items;
}

// TextStyle::inset (space before left/right-aligned text) has no property.
void text_inset(dg::Window& w, dg::NodeId node, int px) {
  dg::NodeStyle style = w.layout().render().style(node);
  style.text.inset = px;
  w.layout().render().set_style(node, style);
  w.invalidate(node);
}

// Rough label width: CJK and emoji are about a full em, Latin about half.
float text_width(const std::string& text, float size) {
  float width = 0;
  for (std::size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    const std::size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    width += len >= 3 ? size : size * 0.55F;
    i += len;
  }
  return width;
}

std::string percent_text(float value) {
  return std::to_string(static_cast<int>(value * 100.0F + 0.5F)) + "%";
}

std::string number_text(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.0f", static_cast<double>(value));
  return buffer;
}

}  // namespace

dg::Window* open_window(dg::App& app) {
  dg::WindowOptions options;
  options.title = "drawgui 组件展示";
  options.width = showcase::kWidth;
  options.height = showcase::kHeight;
  options.resizable = true;
  options.min_width = 960;
  options.min_height = 700;
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
  set(DG_TOKEN_COLOR_SURFACE, 0xFFFFFFFF, 0xFF1A1F29);
  set(DG_TOKEN_COLOR_ON_SURFACE, 0xFF1C2230, 0xFFE6EAF2);
  set(DG_TOKEN_COLOR_BORDER, 0xFFDCE0E8, 0xFF323949);
  set(DG_TOKEN_COLOR_PRIMARY, 0xFF5B57E0, 0xFF8F8CF6);
  set(DG_TOKEN_COLOR_ON_PRIMARY, 0xFFFFFFFF, 0xFF12131C);
  set(DG_TOKEN_COLOR_PRIMARY_HOVER, 0xFF6E6AF0, 0xFFA3A0FA);
  set(DG_TOKEN_COLOR_PRIMARY_PRESSED, 0xFF4743C4, 0xFF7471DE);
  set(DG_TOKEN_COLOR_FOCUS_RING, 0xFFF59E0B, 0xFFFBBF24);
  set(DG_TOKEN_COLOR_DISABLED, 0xFFE9EBF0, 0xFF262C38);
  set(DG_TOKEN_COLOR_ON_DISABLED, 0xFF9AA1AD, 0xFF6B7383);
  set(DG_TOKEN_COLOR_SCROLLBAR_TRACK, 0xFFF1F2F6, 0xFF1E232D);
  set(DG_TOKEN_COLOR_SCROLLBAR_THUMB, 0xFFC3C8D2, 0xFF3A4252);
  return theme;
}

Showcase::Showcase(dg::App& app, dg::Window& window, std::shared_ptr<dg::Theme> theme,
                   dg::ThemeVariant variant)
    : app_(app), w_(window), theme_(std::move(theme)), variant_(variant) {
  app_.set_theme(theme_, variant_);
  rows_ = table_rows();
  build();
  restyle();
}

Showcase::~Showcase() {
  if (waker_thread_.joinable()) {
    waker_thread_.join();
  }
}

// -- Small builders ---------------------------------------------------------

void Showcase::pad(NodeId node, float px) {
  pad(node, px, px);
}

void Showcase::pad(NodeId node, float x, float y) {
  (void)w_.set_prop(node, DG_PROP_PADDING_L, dg::PropValue::number(x));
  (void)w_.set_prop(node, DG_PROP_PADDING_R, dg::PropValue::number(x));
  (void)w_.set_prop(node, DG_PROP_PADDING_T, dg::PropValue::number(y));
  (void)w_.set_prop(node, DG_PROP_PADDING_B, dg::PropValue::number(y));
}

void Showcase::radius(NodeId node, float px) {
  for (const std::uint16_t id : {DG_PROP_BORDER_RADIUS_TL, DG_PROP_BORDER_RADIUS_TR,
                                 DG_PROP_BORDER_RADIUS_BR, DG_PROP_BORDER_RADIUS_BL}) {
    (void)w_.set_prop(node, id, dg::PropValue::number(px));
  }
}

void Showcase::outline(NodeId node) {
  for (const std::uint16_t id : {DG_PROP_BORDER_WIDTH_L, DG_PROP_BORDER_WIDTH_T,
                                 DG_PROP_BORDER_WIDTH_R, DG_PROP_BORDER_WIDTH_B}) {
    (void)w_.set_prop(node, id, dg::PropValue::number(1));
  }
  (void)w_.bind_token(node, DG_PROP_BORDER_COLOR, DG_TOKEN_COLOR_BORDER);
}

void Showcase::align(NodeId node, std::uint32_t value) {
  (void)w_.set_prop(node, DG_PROP_ALIGN, dg::PropValue::option(value));
}

void Showcase::paint(NodeId node, std::uint16_t prop, Role role) {
  paints_.push_back({node, prop, role});
  (void)w_.set_prop(node, prop, dg::PropValue::color(color_of(role)));
}

dg::Color Showcase::color_of(Role role) const {
  const Palette p = palette(variant_);
  switch (role) {
    case Role::kPage:
      return p.page;
    case Role::kSubtle:
      return p.subtle;
    case Role::kMuted:
      return p.muted;
    case Role::kAccentSoft:
      return p.accent_soft;
    case Role::kChip:
      return p.chip;
    case Role::kHeader:
      return p.header;
  }
  return p.page;
}

NodeId Showcase::box(NodeId parent, bool row, float gap) {
  const NodeId node = w_.create(NodeKind::kBox, parent).value();
  (void)w_.set(node, {row ? dg::prop::row() : dg::prop::column(), dg::prop::gap(gap),
                      dg::prop::background(dg::Color{})});
  // A plain box starts 240x120; there is no "auto" property value, so clear
  // the size in its BoxStyle and let it hug its content.
  dg::BoxStyle style = w_.layout().box(node);
  style.width.reset();
  style.height.reset();
  w_.layout().set_box(node, style);
  w_.invalidate(node);
  return node;
}

NodeId Showcase::label(NodeId parent, const std::string& text, float size, bool muted,
                       float width) {
  const NodeId node = w_.create(NodeKind::kLabel, parent).value();
  (void)w_.set(node,
               {dg::prop::font_size(size), dg::prop::height(std::max(20.0F, size * 1.6F))});
  if (width > 0) {
    (void)w_.set(node, dg::prop::width(width));
  } else {
    (void)w_.set(node, dg::prop::width_percent(100));
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

NodeId Showcase::button(NodeId parent, const std::string& text, ControlLook look, float width) {
  const NodeId node = w_.create(NodeKind::kButton, parent).value();
  const float w = width > 0 ? width : 36.0F + (static_cast<float>(text.size()) * 6.0F);
  (void)w_.set(node, {dg::prop::width(w), dg::prop::height(34), dg::prop::font_size(14)});
  (void)w_.set_text(node, DG_PROP_TEXT, text);
  (void)w_.set_prop(node, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_POINTER));
  radius(node, 8);
  if (look == ControlLook::kPrimary) {
    (void)w_.bind_token(node, DG_PROP_TEXT_COLOR, DG_TOKEN_COLOR_ON_PRIMARY);
  } else {
    (void)w_.bind_token(node, DG_PROP_TEXT_COLOR, DG_TOKEN_COLOR_ON_SURFACE);
    outline(node);
  }
  controls_.push_back({node, look});
  return node;
}

// A checkbox or radio with its caption in a row; returns the control.
NodeId Showcase::toggle(NodeId parent, NodeKind kind, const std::string& text) {
  const NodeId row = box(parent, true, 8);
  (void)w_.set(row, dg::prop::height(32));
  align(row, DG_ALIGN_CENTER);
  const NodeId node = w_.create(kind, row).value();
  (void)w_.set(node, {dg::prop::width(26), dg::prop::height(26)});
  if (kind == NodeKind::kCheckbox) {
    pad(node, 5);
    radius(node, 6);
  } else {
    pad(node, 6);
    radius(node, 13);
  }
  outline(node);
  (void)w_.set_prop(node, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_POINTER));
  label(row, text, 14, false, 7.0F * static_cast<float>(text.size()) + 10.0F);
  controls_.push_back({node, ControlLook::kToggle});
  return node;
}

NodeId Showcase::card(NodeId parent, const std::string& title, const std::string& note) {
  const NodeId node = box(parent, false, 10);
  (void)w_.bind_token(node, DG_PROP_BACKGROUND_COLOR, DG_TOKEN_COLOR_SURFACE);
  outline(node);
  radius(node, 12);
  pad(node, 18, 16);
  align(node, DG_ALIGN_STRETCH);
  label(node, title, 16);
  if (!note.empty()) {
    label(node, note, 12.5F, true);
  }
  return node;
}

// -- The scene --------------------------------------------------------------

void Showcase::build() {
  n_.root = w_.create(NodeKind::kBox).value();
  (void)w_.set(n_.root, {dg::prop::width_percent(100), dg::prop::height_percent(100),
                         dg::prop::column(), dg::prop::gap(0)});
  align(n_.root, DG_ALIGN_STRETCH);
  paint(n_.root, DG_PROP_BACKGROUND_COLOR, Role::kPage);
  build_header();

  n_.tabs = w_.create(NodeKind::kTabs, n_.root).value();
  (void)w_.set(n_.tabs, {dg::prop::width_percent(100), dg::prop::grow(1)});
  dg::TabsRef tabs = w_.tabs(n_.tabs).value();
  for (int i = 0; i < kTabCount; ++i) {
    const NodeId page = tabs.add(kTabNames[i]).value();
    (void)w_.set(page, {dg::prop::column(), dg::prop::gap(16), dg::prop::width_percent(100),
                        dg::prop::height_percent(100)});
    align(page, DG_ALIGN_STRETCH);
    paint(page, DG_PROP_BACKGROUND_COLOR, Role::kPage);
    pad(page, 24, 20);
    n_.pages[static_cast<std::size_t>(i)] = page;
  }
  build_basics(n_.pages[kBasics]);
  build_text(n_.pages[kText]);
  build_layout(n_.pages[kLayout]);
  build_data(n_.pages[kData]);
  build_popups(n_.pages[kPopups]);
  build_images(n_.pages[kImages]);
  build_window(n_.pages[kWindow]);

  const NodeId bar = box(n_.root, true, 8);
  (void)w_.set(bar, {dg::prop::height(30), dg::prop::width_percent(100)});
  align(bar, DG_ALIGN_CENTER);
  pad(bar, 24, 0);
  paint(bar, DG_PROP_BACKGROUND_COLOR, Role::kHeader);
  (void)w_.set_prop(bar, DG_PROP_BORDER_WIDTH_T, dg::PropValue::number(1));
  (void)w_.bind_token(bar, DG_PROP_BORDER_COLOR, DG_TOKEN_COLOR_BORDER);
  label(bar, "最近事件", 12.5F, false, 64);
  n_.status = label(bar, "就绪 — 点点看，事件会显示在这里", 12.5F, true);
  (void)w_.set(n_.status, dg::prop::grow(1));
}

void Showcase::build_header() {
  n_.header = box(n_.root, true, 14);
  (void)w_.set(n_.header, {dg::prop::height(64), dg::prop::width_percent(100)});
  align(n_.header, DG_ALIGN_CENTER);
  pad(n_.header, 24, 0);
  paint(n_.header, DG_PROP_BACKGROUND_COLOR, Role::kHeader);
  (void)w_.set_prop(n_.header, DG_PROP_BORDER_WIDTH_B, dg::PropValue::number(1));
  (void)w_.bind_token(n_.header, DG_PROP_BORDER_COLOR, DG_TOKEN_COLOR_BORDER);

  const NodeId mark = w_.create(NodeKind::kLabel, n_.header).value();
  (void)w_.set(mark, {dg::prop::width(34), dg::prop::height(34), dg::prop::font_size(15)});
  (void)w_.set_text(mark, DG_PROP_TEXT, "dg");
  (void)w_.bind_token(mark, DG_PROP_BACKGROUND_COLOR, DG_TOKEN_COLOR_PRIMARY);
  (void)w_.bind_token(mark, DG_PROP_TEXT_COLOR, DG_TOKEN_COLOR_ON_PRIMARY);
  radius(mark, 10);
  const NodeId titles = box(n_.header, false, 0);
  (void)w_.set(titles, {dg::prop::grow(1), dg::prop::height(44)});
  label(titles, "drawgui 组件展示", 19);
  label(titles, "一个窗口，看遍全部控件 · Skia + SDL3 · C++ runtime", 12, true);
  n_.theme_button = button(n_.header, "", ControlLook::kSecondary, 128);
  (void)w_.set_tooltip(n_.theme_button, "整个应用实时切换浅色 / 深色主题");
}

void Showcase::build_basics(NodeId page) {
  const NodeId top = box(page, true, 16);
  align(top, DG_ALIGN_STRETCH);
  (void)w_.set(top, {dg::prop::width_percent(100), dg::prop::height(205)});

  const NodeId buttons = card(top, "按钮", "主要、次要和禁用三种样式；悬停可看提示");
  (void)w_.set(buttons, dg::prop::grow(1));
  const NodeId row = box(buttons, true, 10);
  align(row, DG_ALIGN_CENTER);
  n_.primary_button = button(row, "主要操作", ControlLook::kPrimary, 104);
  n_.secondary_button = button(row, "次要操作", ControlLook::kSecondary, 104);
  n_.disabled_button = button(row, "不可用", ControlLook::kSecondary, 88);
  (void)w_.set_disabled(n_.disabled_button, true);
  (void)w_.set_prop(n_.disabled_button, DG_PROP_CURSOR,
                    dg::PropValue::option(DG_CURSOR_NOT_ALLOWED));
  (void)w_.set_tooltip(n_.primary_button, "点击会让计数加一");
  (void)w_.set_tooltip(n_.secondary_button, "次要按钮：带边框的浅色样式");
  n_.click_count = label(buttons, "还没有点击", 13, true);

  const NodeId choices = card(top, "复选框与单选", "单选按钮同组互斥，方向键也能切换");
  (void)w_.set(choices, dg::prop::grow(1));
  n_.notify_check = toggle(choices, NodeKind::kCheckbox, "接收通知");
  (void)w_.set_tooltip(n_.notify_check, "普通复选框，可以反复勾选");
  const NodeId radios = box(choices, true, 18);
  for (const char* const text : {"小", "中", "大"}) {
    const NodeId radio = toggle(radios, NodeKind::kRadio, text);
    (void)w_.radio(radio).value().set_group(1);
    n_.size_radios.push_back(radio);
  }
  (void)w_.radio(n_.size_radios[1]).value().select();
  n_.size_label = label(choices, "字号：中", 13, true);

  const NodeId middle = box(page, true, 16);
  align(middle, DG_ALIGN_STRETCH);
  (void)w_.set(middle, {dg::prop::width_percent(100), dg::prop::height(192)});

  const NodeId sliders =
      card(middle, "滑块", "拖动、点击轨道，或聚焦后用方向键 / PageUp / Home");
  (void)w_.set(sliders, dg::prop::grow(1));
  const auto slider_row = [&](const std::string& caption, float min, float max, float step,
                              float value, NodeId& slider, NodeId& value_label) {
    const NodeId r = box(sliders, true, 12);
    align(r, DG_ALIGN_CENTER);
    (void)w_.set(r, dg::prop::height(30));
    label(r, caption, 14, false, 70);
    slider = w_.create(NodeKind::kSlider, r).value();
    (void)w_.set(slider, {dg::prop::grow(1), dg::prop::height(20)});
    (void)w_.set_prop(slider, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_RESIZE_EW));
    dg::SliderRef ref = w_.slider(slider).value();
    (void)ref.set_range(min, max, step);
    (void)ref.set_value(value);
    value_label = label(r, number_text(value), 14, true, 36);
  };
  slider_row("音量", 0, 100, 0, 35, n_.volume, n_.volume_label);
  slider_row("档位 0–10", 0, 10, 1, 4, n_.steps, n_.steps_label);
  (void)w_.set_tooltip(n_.volume, "连续滑块 0–100");
  (void)w_.set_tooltip(n_.steps, "步长为 1 的分档滑块");

  const NodeId gate =
      card(middle, "禁用一组控件", "取消勾选「允许编辑」，右侧整组控件都不再响应");
  (void)w_.set(gate, dg::prop::grow(1));
  n_.enable_switch = toggle(gate, NodeKind::kCheckbox, "允许编辑");
  (void)w_.set_tooltip(n_.enable_switch, "勾选状态决定下面一组是否可用");
  n_.editable_group = box(gate, true, 14);
  align(n_.editable_group, DG_ALIGN_CENTER);
  paint(n_.editable_group, DG_PROP_BACKGROUND_COLOR, Role::kSubtle);
  radius(n_.editable_group, 10);
  pad(n_.editable_group, 12, 8);
  n_.group_check = toggle(n_.editable_group, NodeKind::kCheckbox, "自动保存");
  n_.group_slider = w_.create(NodeKind::kSlider, n_.editable_group).value();
  (void)w_.set(n_.group_slider, {dg::prop::grow(1), dg::prop::height(20)});
  (void)w_.slider(n_.group_slider).value().set_value(60);
  n_.group_button = button(n_.editable_group, "应用", ControlLook::kPrimary, 72);
  (void)w_.set_disabled(n_.editable_group, true);

  const NodeId cursors = card(page, "鼠标指针", "把鼠标移到下面的方块上，指针形状会变化");
  const NodeId tiles = box(cursors, true, 10);
  const std::vector<std::tuple<const char*, std::uint32_t, const char*>> kinds = {
      {"手形 pointer", DG_CURSOR_POINTER, "可点击的东西"},
      {"文本 text", DG_CURSOR_TEXT, "可以输入文字的地方"},
      {"十字 crosshair", DG_CURSOR_CROSSHAIR, "精确选取"},
      {"移动 move", DG_CURSOR_MOVE, "可以拖动"},
      {"禁止 not-allowed", DG_CURSOR_NOT_ALLOWED, "现在不能操作"},
      {"等待 wait", DG_CURSOR_WAIT, "正在忙"},
      {"左右 resize", DG_CURSOR_RESIZE_EW, "左右调整大小"},
      {"上下 resize", DG_CURSOR_RESIZE_NS, "上下调整大小"},
  };
  for (const auto& [text, cursor, tip] : kinds) {
    const NodeId tile = box(tiles, false, 0);
    (void)w_.set(tile, {dg::prop::grow(1), dg::prop::height(54)});
    (void)w_.set_prop(tile, DG_PROP_JUSTIFY, dg::PropValue::option(DG_JUSTIFY_CENTER));
    align(tile, DG_ALIGN_CENTER);
    paint(tile, DG_PROP_BACKGROUND_COLOR, Role::kSubtle);
    radius(tile, 10);
    (void)w_.set_prop(tile, DG_PROP_CURSOR, dg::PropValue::option(cursor));
    (void)w_.set_tooltip(tile, tip);
    const NodeId caption = label(tile, text, 12.5F, false);
    (void)w_.set_prop(caption, DG_PROP_TEXT_ALIGN, dg::PropValue::option(DG_TEXT_ALIGN_CENTER));
    n_.cursor_tiles.push_back(tile);
  }
}

void Showcase::build_text(NodeId page) {
  const NodeId top = box(page, true, 16);
  align(top, DG_ALIGN_STRETCH);
  (void)w_.set(top, {dg::prop::width_percent(100), dg::prop::height(258)});

  const NodeId fields =
      card(top, "单行输入", "Ctrl+A 全选 · Ctrl+C / Ctrl+V 复制粘贴 · Ctrl+Z 撤销");
  (void)w_.set(fields, {dg::prop::width(420)});
  label(fields, "用户名", 13, true);
  n_.name_field = w_.create(NodeKind::kTextField, fields).value();
  (void)w_.set(n_.name_field,
               {dg::prop::width_percent(100), dg::prop::height(36), dg::prop::font_size(15)});
  (void)w_.set_prop(n_.name_field, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_TEXT));
  // The runtime's text field has no placeholder; a muted label laid over
  // the field's text area stands in for one and hides once there is text.
  n_.name_placeholder = label(n_.name_field, "输入你的名字…", 15, true, 300);
  (void)w_.set(n_.name_placeholder, dg::prop::height(34));
  text_inset(w_, n_.name_placeholder, 0);
  label(fields, "邀请码（最多 12 个字符）", 13, true);
  const NodeId code_row = box(fields, true, 10);
  align(code_row, DG_ALIGN_CENTER);
  n_.code_field = w_.create(NodeKind::kTextField, code_row).value();
  (void)w_.set(n_.code_field,
               {dg::prop::grow(1), dg::prop::height(36), dg::prop::font_size(15)});
  (void)w_.set_prop(n_.code_field, DG_PROP_MAX_LENGTH, dg::PropValue::number(kFieldMaxLength));
  (void)w_.set_prop(n_.code_field, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_TEXT));
  (void)w_.set_text(n_.code_field, DG_PROP_TEXT, "DG-2026");
  (void)w_.set_tooltip(n_.code_field, "超过 12 个字符的输入会被截断");
  n_.code_counter = label(code_row, "7 / 12", 13, true, 56);

  const NodeId area = card(top, "多行文本框", "中英文、emoji 混排，自动换行，可滚动");
  (void)w_.set(area, dg::prop::grow(1));
  n_.text_area = w_.create(NodeKind::kTextArea, area).value();
  (void)w_.set(n_.text_area,
               {dg::prop::width_percent(100), dg::prop::height(160), dg::prop::font_size(15)});
  (void)w_.set_prop(n_.text_area, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_TEXT));
  (void)w_.set_text(n_.text_area, DG_PROP_TEXT, kTextAreaText);

  const NodeId bottom = box(page, true, 16);
  align(bottom, DG_ALIGN_STRETCH);
  (void)w_.set(bottom, {dg::prop::width_percent(100), dg::prop::height(232)});

  const NodeId sizes = card(bottom, "字号与对齐", "");
  (void)w_.set(sizes, dg::prop::width(420));
  const NodeId size_row = box(sizes, true, 18);
  align(size_row, DG_ALIGN_END);
  for (const float size : {12.0F, 16.0F, 22.0F, 28.0F}) {
    const std::string text = number_text(size) + "px 字";
    label(size_row, text, size, false, text_width(text, size) + 4.0F);
  }
  for (const auto& [text, how] : std::vector<std::pair<const char*, std::uint32_t>>{
           {"左对齐 left", DG_TEXT_ALIGN_LEFT},
           {"居中 center", DG_TEXT_ALIGN_CENTER},
           {"右对齐 right", DG_TEXT_ALIGN_RIGHT}}) {
    const NodeId line = label(sizes, text, 13);
    (void)w_.set(line, dg::prop::height(26));
    (void)w_.set_prop(line, DG_PROP_TEXT_ALIGN, dg::PropValue::option(how));
    paint(line, DG_PROP_BACKGROUND_COLOR, Role::kAccentSoft);
    radius(line, 6);
    text_inset(w_, line, 10);
  }

  const NodeId wrap = card(bottom, "自动换行段落", "宽度固定的标签，文字超出时折行");
  (void)w_.set(wrap, dg::prop::grow(1));
  n_.paragraph = label(wrap, kParagraph, 15);
  (void)w_.set(n_.paragraph, dg::prop::height(120));
  // No property turns on wrapping for a label; the render tree's TextStyle
  // has it, so set it there (a style of our own create() node).
  dg::NodeStyle style = w_.layout().render().style(n_.paragraph);
  style.text.wrap = true;
  w_.layout().render().set_style(n_.paragraph, style);
  w_.invalidate(n_.paragraph);
}

void Showcase::build_layout(NodeId page) {
  const NodeId top = box(page, true, 16);
  align(top, DG_ALIGN_STRETCH);
  (void)w_.set(top, {dg::prop::width_percent(100), dg::prop::height(272)});
  const auto chip = [&](NodeId parent, float w, float h, Role role) {
    const NodeId c = box(parent, false, 0);
    (void)w_.set(c, {dg::prop::width(w), dg::prop::height(h)});
    paint(c, DG_PROP_BACKGROUND_COLOR, role);
    radius(c, 6);
    return c;
  };

  const NodeId flow = card(top, "行与列", "row 横向排列，column 纵向排列，gap 控制间距");
  (void)w_.set(flow, dg::prop::width(330));
  const NodeId pair = box(flow, true, 24);
  const NodeId row = box(pair, true, 8);
  for (int i = 0; i < 3; ++i) {
    chip(row, 40, 40, i == 1 ? Role::kChip : Role::kAccentSoft);
  }
  const NodeId col = box(pair, false, 8);
  for (int i = 0; i < 3; ++i) {
    chip(col, 72, 22, i == 1 ? Role::kChip : Role::kAccentSoft);
  }

  const NodeId justify = card(top, "主轴对齐 justify", "同样三个色块，剩余空间的分配方式不同");
  (void)w_.set(justify, dg::prop::grow(1));
  const NodeId grid = box(justify, true, 16);
  const std::vector<std::pair<const char*, std::uint32_t>> modes = {
      {"start", DG_JUSTIFY_START},
      {"center", DG_JUSTIFY_CENTER},
      {"end", DG_JUSTIFY_END},
      {"space_between", DG_JUSTIFY_SPACE_BETWEEN},
      {"space_around", DG_JUSTIFY_SPACE_AROUND},
      {"space_evenly", DG_JUSTIFY_SPACE_EVENLY},
  };
  std::array<NodeId, 2> columns = {box(grid, false, 6), box(grid, false, 6)};
  for (std::size_t i = 0; i < modes.size(); ++i) {
    const NodeId column = columns[i / 3];
    (void)w_.set(column, dg::prop::grow(1));
    align(column, DG_ALIGN_STRETCH);
    label(column, modes[i].first, 12, true);
    const NodeId strip = box(column, true, 0);
    (void)w_.set(strip, dg::prop::height(28));
    (void)w_.set_prop(strip, DG_PROP_JUSTIFY, dg::PropValue::option(modes[i].second));
    align(strip, DG_ALIGN_CENTER);
    paint(strip, DG_PROP_BACKGROUND_COLOR, Role::kSubtle);
    radius(strip, 6);
    for (int k = 0; k < 3; ++k) {
      chip(strip, 36, 22, Role::kChip);
    }
    n_.justify_rows.push_back(strip);
  }

  const NodeId percent = card(page, "百分比宽度", "25% · 50% · 25%，随窗口宽度伸缩");
  n_.percent_row = box(percent, true, 0);
  (void)w_.set(n_.percent_row, {dg::prop::width_percent(100), dg::prop::height(36)});
  for (const float share : {25.0F, 50.0F, 25.0F}) {
    const NodeId cell = box(n_.percent_row, false, 0);
    (void)w_.set(cell, {dg::prop::width_percent(share), dg::prop::height(36)});
    (void)w_.set_prop(cell, DG_PROP_JUSTIFY, dg::PropValue::option(DG_JUSTIFY_CENTER));
    paint(cell, DG_PROP_BACKGROUND_COLOR, share > 30 ? Role::kChip : Role::kAccentSoft);
    outline(cell);
    const NodeId text = label(cell, number_text(share) + "%", 13);
    (void)w_.set_prop(text, DG_PROP_TEXT_ALIGN, dg::PropValue::option(DG_TEXT_ALIGN_CENTER));
    if (share > 30) {
      (void)w_.bind_token(text, DG_PROP_TEXT_COLOR, DG_TOKEN_COLOR_ON_PRIMARY);
    }
    n_.percent_cells.push_back(cell);
  }

  const NodeId wrap = card(page, "自动换行容器", "放不下的标签自动排到下一行");
  n_.wrap_box = box(wrap, true, 8);
  (void)w_.set(n_.wrap_box, {dg::prop::width_percent(100), dg::prop::height(76)});
  (void)w_.set_prop(n_.wrap_box, DG_PROP_WRAP, dg::PropValue::boolean(true));
  (void)w_.set_prop(n_.wrap_box, DG_PROP_RUN_GAP, dg::PropValue::number(8));
  for (const char* const tag :
       {"C++20",    "Skia",     "SDL3",     "Flexbox 布局", "主题",
        "深色模式", "中文排版", "Emoji 🎉", "虚拟列表",     "树形视图",
        "表格排序", "对话框",   "右键菜单", "提示框",       "SVG 图标",
        "WebP",     "多窗口",   "定时器",   "跨线程唤醒",   "无障碍焦点"}) {
    const NodeId t = box(n_.wrap_box, true, 0);
    const float width = 28.0F + text_width(tag, 12.5F);
    (void)w_.set(t, {dg::prop::width(width), dg::prop::height(28)});
    paint(t, DG_PROP_BACKGROUND_COLOR, Role::kAccentSoft);
    radius(t, 14);
    const NodeId text = label(t, tag, 12.5F);
    (void)w_.set_prop(text, DG_PROP_TEXT_ALIGN, dg::PropValue::option(DG_TEXT_ALIGN_CENTER));
    (void)w_.set(text, dg::prop::height(28));
  }
}

void Showcase::build_data(NodeId page) {
  const NodeId top = box(page, true, 16);
  align(top, DG_ALIGN_STRETCH);
  (void)w_.set(top, {dg::prop::width_percent(100), dg::prop::height(400)});

  const NodeId list_card = card(top, "列表", "1000 项，只创建可见的行");
  (void)w_.set(list_card, dg::prop::width(250));
  n_.list = w_.create(NodeKind::kList, list_card).value();
  (void)w_.set(n_.list, {dg::prop::width_percent(100), dg::prop::height(300)});
  (void)w_.list(n_.list).value().set_items(list_items(), 30);

  const NodeId tree_card = card(top, "树形视图", "方向键展开 / 折叠");
  (void)w_.set(tree_card, dg::prop::width(250));
  n_.tree = w_.create(NodeKind::kTree, tree_card).value();
  (void)w_.set(n_.tree, {dg::prop::width_percent(100), dg::prop::height(300)});
  (void)w_.tree(n_.tree).value().set_items(tree_items());

  const NodeId table_card = card(top, "表格", "点击表头排序（再点一次反向）");
  (void)w_.set(table_card, dg::prop::grow(1));
  n_.table = w_.create(NodeKind::kTable, table_card).value();
  (void)w_.set(n_.table, {dg::prop::width_percent(100), dg::prop::height(300)});
  dg::TableRef table = w_.table(n_.table).value();
  (void)table.set_columns({{"水果", std::nullopt, 30.0F, true},
                           {"单价", std::nullopt, 22.0F, true},
                           {"库存", std::nullopt, 22.0F, true},
                           {"产地", std::nullopt, std::nullopt, false}});
  (void)table.set_rows(rows_);

  const NodeId progress =
      card(page, "进度条", "上：定时器驱动的确定进度；下：不确定进度（动画）");
  const NodeId r = box(progress, true, 12);
  align(r, DG_ALIGN_CENTER);
  n_.progress = w_.create(NodeKind::kProgress, r).value();
  (void)w_.set(n_.progress, {dg::prop::grow(1), dg::prop::height(10)});
  n_.progress_label = label(r, "0%", 14, true, 44);
  n_.progress_button = button(r, "暂停", ControlLook::kSecondary, 72);
  n_.spinner = w_.create(NodeKind::kProgress, progress).value();
  (void)w_.set(n_.spinner, {dg::prop::width_percent(100), dg::prop::height(6)});
  (void)w_.progress(n_.spinner).value().set_indeterminate(true);
}

void Showcase::build_popups(NodeId page) {
  const NodeId top = box(page, true, 16);
  align(top, DG_ALIGN_STRETCH);
  (void)w_.set(top, {dg::prop::width_percent(100), dg::prop::height(220)});

  const NodeId drop = card(top, "下拉选择", "点击展开，方向键 + 回车也能选");
  (void)w_.set(drop, dg::prop::grow(1));
  n_.dropdown = w_.create(NodeKind::kDropdown, drop).value();
  (void)w_.set(n_.dropdown, {dg::prop::width(240), dg::prop::height(36)});
  (void)w_.set_text(n_.dropdown, DG_PROP_TEXT, "选择城市…");
  (void)w_.dropdown(n_.dropdown).value().set_items(kDropdownItems);
  n_.dropdown_label = label(drop, "尚未选择", 13, true);

  const NodeId menu = card(top, "右键菜单", "在下面的区域里点右键");
  (void)w_.set(menu, dg::prop::grow(1));
  n_.menu_area = box(menu, false, 0);
  (void)w_.set(n_.menu_area, {dg::prop::width_percent(100), dg::prop::height(90)});
  (void)w_.set_prop(n_.menu_area, DG_PROP_JUSTIFY, dg::PropValue::option(DG_JUSTIFY_CENTER));
  paint(n_.menu_area, DG_PROP_BACKGROUND_COLOR, Role::kAccentSoft);
  radius(n_.menu_area, 10);
  (void)w_.set_prop(n_.menu_area, DG_PROP_CURSOR, dg::PropValue::option(DG_CURSOR_CROSSHAIR));
  const NodeId hint = label(n_.menu_area, "右键点击这里", 14);
  (void)w_.set_prop(hint, DG_PROP_TEXT_ALIGN, dg::PropValue::option(DG_TEXT_ALIGN_CENTER));
  n_.menu_label = label(menu, "菜单还没打开过", 13, true);

  const NodeId bottom = box(page, true, 16);
  align(bottom, DG_ALIGN_STRETCH);
  (void)w_.set(bottom, {dg::prop::width_percent(100), dg::prop::height(165)});

  const NodeId dialogs =
      card(bottom, "对话框", "模态：打开时其它控件不响应；回车 = 默认按钮，Esc = 取消");
  (void)w_.set(dialogs, dg::prop::grow(1));
  const NodeId row = box(dialogs, true, 10);
  n_.delete_button = button(row, "删除文件…", ControlLook::kPrimary, 112);
  n_.info_button = button(row, "关于", ControlLook::kSecondary, 80);
  n_.dialog_label = label(dialogs, "对话框结果会显示在这里", 13, true);

  const NodeId tips = card(bottom, "提示框", "把鼠标停在按钮上片刻");
  (void)w_.set(tips, dg::prop::grow(1));
  const NodeId tip_row = box(tips, true, 10);
  for (const auto& [text, tip] :
       std::vector<std::pair<const char*, const char*>>{{"保存", "保存当前文档 (Ctrl+S)"},
                                                        {"分享", "生成一个分享链接"},
                                                        {"Emoji", "提示里也能用 emoji 🙂"}}) {
    const NodeId b = button(tip_row, text, ControlLook::kSecondary, 84);
    (void)w_.set_tooltip(b, tip);
    n_.tooltip_buttons.push_back(b);
  }
}

void Showcase::build_images(NodeId page) {
  const NodeId bitmaps =
      card(page, "位图", "启动时用 Skia 画好，再分别编码成 PNG / JPEG / WebP 后解码显示");
  const NodeId row = box(bitmaps, true, 20);
  const std::array<std::vector<std::uint8_t>, 3> encoded = sample_images();
  const char* const formats[] = {"PNG · 圆角透明", "JPEG · 质量 88", "WebP · 有损 85"};
  for (std::size_t i = 0; i < 3; ++i) {
    const NodeId cell = box(row, false, 8);
    const NodeId image = w_.create(NodeKind::kImage, cell).value();
    (void)w_.set(image, {dg::prop::width(240), dg::prop::height(150)});
    if (!encoded[i].empty()) {
      const dg::Expected<dg::ImageId, dg::ImageError> id =
          app_.images().decode(encoded[i].data(), encoded[i].size());
      if (id) {
        (void)w_.set_image(image, id.value());
        n_.bitmap_ids[i] = id.value();
      }
    }
    (void)w_.set_tooltip(image, std::to_string(encoded[i].size()) + " 字节");
    label(cell, formats[i], 13, true, 240);
    n_.bitmaps[i] = image;
  }

  const NodeId icons =
      card(page, "SVG 图标", "矢量图标按主题着色，16 / 24 / 32 / 48 像素都清晰");
  const dg::ThemeVariant variants[] = {dg::ThemeVariant::kLight, dg::ThemeVariant::kDark};
  for (const float size : {16.0F, 24.0F, 32.0F, 48.0F}) {
    const NodeId r = box(icons, true, 18);
    align(r, DG_ALIGN_CENTER);
    (void)w_.set(r, dg::prop::height(std::max(size, 24.0F) + 4));
    label(r, number_text(size) + " px", 13, true, 60);
    for (std::size_t k = 0; k < std::size(kIcons); ++k) {
      Icon icon;
      icon.node = w_.create(NodeKind::kImage, r).value();
      (void)w_.set(icon.node, {dg::prop::width(size), dg::prop::height(size)});
      icon.accent = k % 2 == 1;
      const std::string svg = kIcons[k];
      for (std::size_t v = 0; v < 2; ++v) {
        for (std::size_t a = 0; a < 2; ++a) {
          dg::SvgOptions options;
          options.tint = theme_->color_value(
              a == 0 ? DG_TOKEN_COLOR_ON_SURFACE : DG_TOKEN_COLOR_PRIMARY, variants[v]);
          const auto* bytes = reinterpret_cast<const std::uint8_t*>(svg.data());  // NOLINT
          const dg::Expected<dg::ImageId, dg::ImageError> id =
              app_.images().decode_svg(bytes, svg.size(), options);
          if (id) {
            icon.ids[v][a] = id.value();
          }
        }
      }
      icons_.push_back(icon);
    }
  }
}

void Showcase::build_window(NodeId page) {
  const NodeId top = box(page, true, 16);
  align(top, DG_ALIGN_STRETCH);
  (void)w_.set(top, {dg::prop::width_percent(100), dg::prop::height(330)});

  const NodeId os = card(top, "窗口操作", "这些按钮直接调用 dg::Window 的窗口方法");
  (void)w_.set(os, dg::prop::grow(1));
  const NodeId row = box(os, true, 10);
  n_.title_button = button(row, "改标题", ControlLook::kPrimary, 84);
  n_.maximize_button = button(row, "最大化", ControlLook::kSecondary, 84);
  n_.restore_button = button(row, "还原", ControlLook::kSecondary, 72);
  n_.fullscreen_button = button(row, "全屏", ControlLook::kSecondary, 96);
  n_.minimize_button = button(row, "最小化", ControlLook::kSecondary, 84);
  label(os, "窗口状态事件（最新在上）", 13);
  const NodeId log = box(os, false, 2);
  paint(log, DG_PROP_BACKGROUND_COLOR, Role::kSubtle);
  radius(log, 8);
  pad(log, 12, 8);
  align(log, DG_ALIGN_STRETCH);
  for (int i = 0; i < 6; ++i) {
    n_.log_lines.push_back(label(log, i == 0 ? "（暂无）" : "", 13, i != 0));
  }

  const NodeId side = box(top, false, 16);
  (void)w_.set(side, dg::prop::width(360));
  align(side, DG_ALIGN_STRETCH);
  const NodeId clock = card(side, "定时器", "App::add_timer() 每秒触发一次");
  n_.tick_label = label(clock, "已运行 0 秒", 22);
  (void)w_.set(n_.tick_label, dg::prop::height(36));

  const NodeId waker = card(side, "跨线程唤醒", "后台线程 300ms 后用 Waker 投递一个 UserEvent");
  n_.waker_button = button(waker, "启动后台任务", ControlLook::kPrimary, 140);
  n_.waker_label = label(waker, "等待中", 13, true);
}

// -- Theme ------------------------------------------------------------------

void Showcase::set_variant(dg::ThemeVariant variant) {
  variant_ = variant;
  (void)app_.set_theme_variant(variant);
  for (const Paint& p : paints_) {
    (void)w_.set_prop(p.node, p.prop, dg::PropValue::color(color_of(p.role)));
  }
  restyle();
}

// The runtime paints buttons, checkboxes and radios in fixed colours and
// keeps no theme binding for their fills; re-colour them from the theme.
void Showcase::restyle() {
  const Palette p = palette(variant_);
  const auto token = [&](dg_token_id id) {
    return theme_->color_value(id, variant_).value_or(dg::Color{});
  };
  const dg::Color surface = token(DG_TOKEN_COLOR_SURFACE);
  const dg::Color border = token(DG_TOKEN_COLOR_BORDER);
  const dg::Color primary = token(DG_TOKEN_COLOR_PRIMARY);
  dg::RenderTree& render = w_.layout().render();
  for (const Control& c : controls_) {
    if (!w_.widgets().has(c.node)) {
      continue;
    }
    dg::Widget widget = w_.widgets().at(c.node);
    switch (c.look) {
      case ControlLook::kPrimary:
        widget.fill_normal = primary;
        widget.fill_hover = token(DG_TOKEN_COLOR_PRIMARY_HOVER);
        widget.fill_pressed = token(DG_TOKEN_COLOR_PRIMARY_PRESSED);
        break;
      case ControlLook::kSecondary:
        widget.fill_normal = p.subtle;
        widget.fill_hover = border;
        widget.fill_pressed = p.accent_soft;
        break;
      case ControlLook::kToggle:
        widget.fill_normal = surface;
        widget.fill_hover = p.subtle;
        widget.fill_pressed = border;
        widget.indicator_on = primary;
        widget.indicator_off = surface;
        break;
    }
    w_.widgets().attach(c.node, widget);
    w_.widgets().refresh(render, c.node, w_.interaction().state_of(c.node));
    if (c.look == ControlLook::kToggle && widget.indicator.index != 0) {
      dg::NodeStyle style = render.style(widget.indicator);
      style.radii = dg::Radii::all(w_.kind_of(c.node) == NodeKind::kRadio ? 7.0F : 3.0F);
      render.set_style(widget.indicator, style);
      w_.widgets().refresh(render, widget.indicator, dg::PointerState{});
    }
    w_.invalidate(c.node);
  }
  const std::size_t v = variant_ == dg::ThemeVariant::kDark ? 1 : 0;
  for (const Icon& icon : icons_) {
    const dg::ImageId id = icon.ids[v][icon.accent ? 1 : 0];
    if (id.is_valid()) {
      (void)w_.set_image(icon.node, id);
    }
  }
  (void)w_.set_text(n_.theme_button, DG_PROP_TEXT,
                    variant_ == dg::ThemeVariant::kDark ? "☀ 浅色模式" : "☾ 深色模式");
}

// -- Behaviour --------------------------------------------------------------

void Showcase::start_timers() {
  progress_timer_ = app_.add_timer(kProgressTickMs, true);
  clock_timer_ = app_.add_timer(kClockTickMs, true);
}

void Showcase::set_progress(float value) {
  progress_ = std::clamp(value, 0.0F, 1.0F);
  (void)w_.progress(n_.progress).value().set_value(progress_);
  (void)w_.set_text(n_.progress_label, DG_PROP_TEXT, percent_text(progress_));
}

void Showcase::select_tab(int index) {
  (void)w_.tabs(n_.tabs).value().select(index);
}

std::string Showcase::status_text() const {
  const dg::RenderTree& render = w_.layout().render();
  return render.style(n_.status).text.text;
}

void Showcase::status(const std::string& text) {
  (void)w_.set_text(n_.status, DG_PROP_TEXT, text);
}

void Showcase::sort_table(int column) {
  sort_ascending_ = sort_column_ == column ? !sort_ascending_ : true;
  sort_column_ = column;
  const auto c = static_cast<std::size_t>(column);
  const bool numeric = column == 1 || column == 2;
  const bool ascending = sort_ascending_;
  std::stable_sort(rows_.begin(), rows_.end(), [&](const auto& a, const auto& b) {
    if (numeric) {
      const int x = std::stoi(a[c]);
      const int y = std::stoi(b[c]);
      return ascending ? x < y : y < x;
    }
    return ascending ? a[c] < b[c] : b[c] < a[c];
  });
  dg::TableRef table = w_.table(n_.table).value();
  (void)table.set_rows(rows_);
  (void)table.set_sort_indicator(column, ascending);
}

void Showcase::log_window(const std::string& line) {
  log_.insert(log_.begin(), line);
  log_.resize(std::min(log_.size(), n_.log_lines.size()));
  for (std::size_t i = 0; i < n_.log_lines.size(); ++i) {
    (void)w_.set_text(n_.log_lines[i], DG_PROP_TEXT, i < log_.size() ? log_[i] : "");
  }
  status("窗口：" + line);
}

void Showcase::open_context_menu(dg::PixelPoint at) {
  std::vector<dg::MenuEntry> entries;
  for (std::size_t i = 0; i < kMenuItems.size(); ++i) {
    dg::MenuEntry e;
    e.text = kMenuItems[i];
    e.separator = kMenuItems[i].empty();
    e.disabled = i == 4;
    if (i == 3) {
      e.checked = true;
    }
    entries.push_back(e);
  }
  const dg::Expected<std::int32_t, dg::Status> id = w_.open_menu(std::nullopt, at, entries);
  if (id) {
    menu_id_ = id.value();
    status("右键菜单已打开");
  }
}

void Showcase::on_window_click(NodeId node) {
  if (node == n_.title_button) {
    title_index_ = (title_index_ + 1) % static_cast<int>(kTitles.size());
    (void)w_.set_title(kTitles[static_cast<std::size_t>(title_index_)]);
    log_window("标题改为「" + kTitles[static_cast<std::size_t>(title_index_)] + "」");
  } else if (node == n_.maximize_button) {
    (void)w_.maximize();
    status("已请求最大化");
  } else if (node == n_.restore_button) {
    (void)w_.restore();
    status("已请求还原");
  } else if (node == n_.fullscreen_button) {
    fullscreen_ = !fullscreen_;
    (void)w_.set_fullscreen(fullscreen_);
    (void)w_.set_text(n_.fullscreen_button, DG_PROP_TEXT, fullscreen_ ? "退出全屏" : "全屏");
    status(fullscreen_ ? "已请求全屏" : "已请求退出全屏");
  } else if (node == n_.minimize_button) {
    (void)w_.minimize();
    status("已请求最小化");
  } else if (node == n_.waker_button) {
    if (waker_thread_.joinable()) {
      waker_thread_.join();
    }
    (void)w_.set_text(n_.waker_label, DG_PROP_TEXT, "后台线程运行中…");
    const dg::Waker waker = app_.waker();
    waker_thread_ = std::thread([waker] {
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      (void)waker.post_user_event(kWakerPayload);
    });
    status("后台线程已启动");
  }
}

void Showcase::on_click(const dg::events::ClickEvent& click) {
  if (!click.node.has_value()) {
    return;
  }
  const NodeId node = *click.node;
  if (node == n_.theme_button) {
    set_variant(variant_ == dg::ThemeVariant::kDark ? dg::ThemeVariant::kLight
                                                    : dg::ThemeVariant::kDark);
    status(variant_ == dg::ThemeVariant::kDark ? "主题：深色" : "主题：浅色");
  } else if (node == n_.primary_button || node == n_.secondary_button) {
    ++clicks_;
    (void)w_.set_text(n_.click_count, DG_PROP_TEXT,
                      "已点击 " + std::to_string(clicks_) + " 次");
    status(std::string("点击：") + (node == n_.primary_button ? "主要操作" : "次要操作"));
  } else if (node == n_.notify_check) {
    status(w_.checked(node).value_or(false) ? "接收通知：开" : "接收通知：关");
  } else if (std::find(n_.size_radios.begin(), n_.size_radios.end(), node) !=
             n_.size_radios.end()) {
    const char* const names[] = {"小", "中", "大"};
    const auto i = static_cast<std::size_t>(
        std::find(n_.size_radios.begin(), n_.size_radios.end(), node) - n_.size_radios.begin());
    (void)w_.set_text(n_.size_label, DG_PROP_TEXT, std::string("字号：") + names[i]);
    status(std::string("单选：") + names[i]);
  } else if (node == n_.enable_switch) {
    const bool on = w_.checked(node).value_or(false);
    (void)w_.set_disabled(n_.editable_group, !on);
    status(on ? "整组控件已启用" : "整组控件已禁用");
  } else if (node == n_.group_check || node == n_.group_button) {
    status(node == n_.group_button ? "点击：应用" : "自动保存已切换");
  } else if (node == n_.progress_button) {
    progress_running_ = !progress_running_;
    (void)w_.set_text(node, DG_PROP_TEXT, progress_running_ ? "暂停" : "继续");
    status(progress_running_ ? "进度继续" : "进度暂停");
  } else if (node == n_.delete_button) {
    dg::DialogOptions options;
    options.title = "删除文件？";
    options.body = "「周报.md」将被永久删除，这个操作不能撤销。";
    options.buttons = {"删除", "取消"};
    options.default_button = 0;
    options.cancel_button = 1;
    (void)w_.open_dialog(options);
    status("对话框已打开");
  } else if (node == n_.info_button) {
    dg::DialogOptions options;
    options.title = "关于 drawgui";
    options.body = "一个基于 Skia 与 SDL3 的 C++ GUI 库。";
    (void)w_.open_dialog(options);
    status("对话框已打开");
  } else {
    on_window_click(node);
  }
}

void Showcase::on_timer(const dg::events::TimerEvent& timer) {
  if (timer.timer == progress_timer_) {
    if (progress_running_) {
      progress_ += 0.01F;
      if (progress_ > 1.0001F) {
        progress_ = 0.0F;
      }
      set_progress(progress_);
    }
  } else if (timer.timer == clock_timer_) {
    ++ticks_;
    (void)w_.set_text(n_.tick_label, DG_PROP_TEXT, "已运行 " + std::to_string(ticks_) + " 秒");
  }
}

void Showcase::on_event(const dg::Event& event) {
  using namespace dg::events;  // NOLINT(google-build-using-namespace)
  std::visit(dg::Overloaded{
                 [&](const ClickEvent& e) { on_click(e); },
                 [&](const ContextClickEvent& e) {
                   if (e.node == n_.menu_area) {
                     open_context_menu({e.x, e.y});
                   }
                 },
                 [&](const TextChangedEvent& e) {
                   if (e.node == n_.name_field) {
                     const std::string text = w_.text(n_.name_field).value_or("");
                     (void)w_.set_text(n_.name_placeholder, DG_PROP_TEXT,
                                       text.empty() ? "输入你的名字…" : "");
                     status("用户名：" + text);
                   } else if (e.node == n_.code_field) {
                     const std::string text = w_.text(n_.code_field).value_or("");
                     (void)w_.set_text(
                         n_.code_counter, DG_PROP_TEXT,
                         std::to_string(text.size()) + " / " + std::to_string(kFieldMaxLength));
                     status("邀请码：" + text);
                   } else {
                     status("多行文本已修改");
                   }
                 },
                 [&](const ListSelectEvent& e) {
                   status("列表：选中第 " + std::to_string(e.index + 1) + " 项");
                 },
                 [&](const ValueChangedEvent& e) {
                   if (e.selected >= 0) {
                     const std::string& city =
                         kDropdownItems[static_cast<std::size_t>(e.selected)];
                     (void)w_.set_text(n_.dropdown_label, DG_PROP_TEXT, "已选择：" + city);
                     status("下拉：" + city);
                   }
                 },
                 [&](const MenuEvent& e) {
                   const std::string text =
                       e.item.has_value()
                           ? "选择了「" + kMenuItems[static_cast<std::size_t>(*e.item)] + "」"
                           : std::string("菜单已关闭，未选择");
                   (void)w_.set_text(n_.menu_label, DG_PROP_TEXT, text);
                   status("菜单：" + text);
                 },
                 [&](const DialogResultEvent& e) {
                   const std::string text = e.button == 0   ? "确认（按钮 0）"
                                            : e.button == 1 ? "取消（按钮 1）"
                                                            : "Esc 关闭";
                   (void)w_.set_text(n_.dialog_label, DG_PROP_TEXT, "对话框结果：" + text);
                   status("对话框：" + text);
                 },
                 [&](const TabSelectEvent& e) {
                   status(std::string("切换到「") + kTabNames[e.index] + "」页");
                 },
                 [&](const TreeEvent& e) {
                   const char* const what[] = {"选中", "展开", "折叠"};
                   status(std::string("树：") + what[static_cast<int>(e.kind)] + "第 " +
                          std::to_string(e.item) + " 项");
                 },
                 [&](const TableEvent& e) {
                   if (e.kind == TableEvent::Kind::kSort) {
                     sort_table(e.index);
                     status("表格：按第 " + std::to_string(e.index + 1) + " 列" +
                            (sort_ascending_ ? "升序" : "降序") + "排序");
                   } else {
                     status("表格：选中第 " + std::to_string(e.index + 1) + " 行");
                   }
                 },
                 [&](const WindowStateEvent& e) {
                   switch (e.kind) {
                     case WindowStateEvent::Kind::kResized:
                       log_window("尺寸 " + std::to_string(e.x) + "×" + std::to_string(e.y));
                       break;
                     case WindowStateEvent::Kind::kMaximized:
                       log_window("已最大化");
                       break;
                     case WindowStateEvent::Kind::kMinimized:
                       log_window("已最小化");
                       break;
                     case WindowStateEvent::Kind::kRestored:
                       log_window("已还原");
                       break;
                     case WindowStateEvent::Kind::kEnteredFullscreen:
                       log_window("进入全屏");
                       break;
                     case WindowStateEvent::Kind::kLeftFullscreen:
                       log_window("退出全屏");
                       break;
                     case WindowStateEvent::Kind::kFocusGained:
                       log_window("获得焦点");
                       break;
                     case WindowStateEvent::Kind::kFocusLost:
                       log_window("失去焦点");
                       break;
                     case WindowStateEvent::Kind::kMoved:
                       break;
                   }
                 },
                 [&](const TimerEvent& e) { on_timer(e); },
                 [&](const UserEvent& e) {
                   woken_ = e.payload;
                   (void)w_.set_text(n_.waker_label, DG_PROP_TEXT,
                                     "收到 UserEvent，payload = " + std::to_string(e.payload));
                   status("跨线程唤醒：payload " + std::to_string(e.payload));
                 },
                 [&](const SliderEvent& e) {
                   if (e.node == n_.volume) {
                     (void)w_.set_text(n_.volume_label, DG_PROP_TEXT, number_text(e.value));
                     status("音量：" + number_text(e.value));
                   } else if (e.node == n_.steps) {
                     (void)w_.set_text(n_.steps_label, DG_PROP_TEXT, number_text(e.value));
                     status("档位：" + number_text(e.value));
                   }
                 },
                 [](const auto&) {},
             },
             event);
}

}  // namespace showcase
