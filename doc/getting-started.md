# 快速上手

drawgui 是一个 C++20 自绘 GUI 内核：CPU 光栅（Skia），SDL3 窗口后端（仅 Linux），
并导出一份纯 C ABI 供其它语言嵌入。它不是一个成品应用框架——没有
`createButton()`，没有声明式布局语言，你要自己编排每一帧。

## 1. 构建

前置条件：

- CMake ≥ 3.24
- Ninja
- clang 或 gcc，支持 C++20
- `libfreetype-dev`（链接期需要，`libskia.a` 无条件引用 `SkTypeface_FreeType`）
- `libfontconfig1-dev`（**只在配置链接时需要**——`skia2Config.cmake`
  在 Linux 上要求它。运行时不依赖：`src/render/font_catalog.cpp` 从不调用
  `SkFontMgr_New_FontConfig`，字体回退是手写的，不读 `/etc/fonts`。`ldd`
  任意 drawgui 二进制都不会看到 `libfontconfig`。）

```sh
sudo apt-get install -y libfreetype-dev libfontconfig1-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

首次 configure 会把 `BBDXF/libskia2` 的预编译包和 doctest 单头文件下载到
`third_party/`，两者都经过 SHA256 校验。下载完成后可以离线重复构建；
`-DDRAWGUI_BUILD_TESTS=OFF` 可以跳过 doctest（省去测试依赖）。

跑一个最简单的验证：

```sh
./build/examples/drawgui_render_png out.png
```

想看引擎能做到什么，直接跑 `./build/examples/25_showcase/drawgui_showcase`
——这是唯一一个把所有功能组合在一个场景里的 example，其余 25 个都只演示
一个切面。`examples/` 目录下每个子目录都有对应可执行文件，用 `--help`
看参数。

## 2. 先分岔：C++ 嵌入 vs C ABI 嵌入

这是最容易踩的坑：**两条路径的能力差距很大，不是同一套东西的两种绑定。**

- **C++ 路径**：完整能力——布局、9 种 `WidgetKind`、文本（含 CJK/BiDi/IME）、
  主题、动画、弹出层、焦点、剪贴板，全都可用。本文档下面的窗口骨架和
  `doc/guide/` 都是针对这条路径写的。
- **C ABI 路径**：能力比 C++ 路径小，今天能做这些事：
  - 开窗并操作窗口（`dg_window_create`、`dg_window_set_size` 等）
  - 建节点，十四种类型：`BOX`（写 `DG_PROP_DIRECTION` 变成行/列容器）、
    `BUTTON`、`LABEL`、`CHECKBOX`、`TEXT_FIELD`、`TEXT_AREA`、
    `SCROLL_VIEW`、`LIST`（虚拟化文本列表，`dg_list_set_items`）、
    `IMAGE`（`dg_node_set_image_data`/`_file`，PNG/JPEG/WebP/SVG），以及组件
    `DROPDOWN`/`PROGRESS`/`TABS`/`TREE`/`TABLE`（另有菜单、对话框、tooltip 函数，
    示例 `examples/28_c_components`）——见
    `include/drawgui/abi/drawgui.h`
  - 设置属性，61 个 `dg_prop_id` 里的任意一个（`dg_node_set_prop`；
    `DG_PROP_TEXT` 这种字符串属性走 `dg_node_set_text`），包括字号、对齐、
    百分比尺寸（`DG_LENGTH_UNIT_PERCENT`）
  - 按位置插入、在同一窗口内移动、整棵子树删除节点（`dg_node_insert_before`
    / `dg_node_remove`），足够让 keyed reconciler 驱动这棵树
  - 键盘焦点（`dg_node_focus`、Tab/Shift+Tab）、定时器（`dg_timer_add`）、
    从其它线程唤醒事件循环（`dg_wake`/`dg_post_user_event`）
  - 读复选框状态、编辑框内容、列表选中行、滚动偏移
  - 收点击（带连击数）、右键、焦点、列表选择、文字变化、action、定时器、
    用户事件和窗口状态事件
  - 注册字体目录（`dg_app_add_font`）、加载主题

  `examples/19_c_client/main.c` 演示了其中大部分；
  `examples/26_text_area` 演示了行容器 + 多行编辑框。

  菜单、下拉框、tooltip、对话框、Tabs、树、表格、进度条、滑块和动画今天
  只存在于 C++ 路径（见 `doc/limitations.md` C 节）。

## 3. 最小 C++ 程序：离屏光栅

不开窗，直接画一帧、编码 PNG。这是引擎最底层的一层，`examples/00_cpu_raster_png/main.cpp`
的骨架：

```cpp
#include "drawgui/graphics/canvas.h"
#include "drawgui/graphics/raster_surface.h"
#include "drawgui/graphics/types.h"

#include <fstream>
#include <optional>
#include <vector>

int main() {
  std::optional<dg::RasterSurface> surface = dg::RasterSurface::create(480, 320);
  if (!surface) {
    return 1;
  }

  dg::Canvas canvas = surface->canvas();
  canvas.clear(dg::Color::rgba(0x1E, 0x20, 0x26));
  canvas.fill_rrect(dg::Rect::from_xywh(40, 40, 400, 110),
                     dg::Radii::all(16), dg::Color::rgba(0x2E, 0x86, 0xDE));

  const std::vector<std::uint8_t> png = surface->encode_png();
  std::ofstream out("out.png", std::ios::binary);
  out.write(reinterpret_cast<const char*>(png.data()),
            static_cast<std::streamsize>(png.size()));
  return 0;
}
```

**这只是光栅，不是 GUI。** 没有布局、没有窗口、没有事件。真正的 GUI 程序
要多编排一整套对象——见下一节。

## 4. 带窗口的 C++ 骨架

从 `examples/05_widgets/`（`demo.h`/`demo.cpp`/`widget_scene.cpp`）提炼，
已在仓库现有构建里验证可编译（`ninja -C build drawgui_widgets`）。一个
交互式窗口至少要同时持有这些对象：

```
dg::WindowManager        事件源，负责 pump() / present()
dg::LayoutTree           内含一棵 dg::RenderTree（真正的节点树）
dg::WidgetSet            NodeId -> Widget 描述符的侧表
dg::Interaction          指针状态机（hover/press/click）
dg::Focus (+ dg::FocusRing) 键盘 Tab 顺序、可视焦点框
dg::FontCatalog          有任何文本节点之前必须先建好
dg::Theme + dg::ThemeBindings  主题 token 与节点的绑定
dg::RasterSurface        画布，调用方自己持有、自己在 resize 时重建
dg::AnimationEngine      可选，只有用到动画时才需要
dg::PopupHost            可选，只有用到菜单/提示/对话框时才需要
```

骨架（省略头文件与参数解析，只保留控制流）：

```cpp
dg::Expected<dg::WindowManager, dg::WindowError> created = dg::WindowManager::create();
dg::WindowManager manager = std::move(created).value();

const dg::WindowId window = manager.open(
    dg::WindowSpec{"my app", 1120, 800, dg::Color::from_argb(0xFF11161D)}).value();

dg::Expected<dg::FontCatalog, dg::FontError> fonts = dg::FontCatalog::scan("/usr/share/fonts");

dg::TreeSpec spec;
spec.fonts = std::move(fonts).value();     // 有文本节点时必须设置
dg::LayoutTree tree{spec};
dg::WidgetSet widgets;
dg::Interaction interaction;

std::optional<dg::RasterSurface> surface;   // 收到第一个 needs_repaint 时创建

for (;;) {
  const dg::PumpResult pumped = manager.pump(/*timeout_ms=*/200);
  if (!pumped.closed.empty()) break;

  // 见下一节：needs_repaint 必须最先处理
  for (dg::WindowId w : pumped.needs_repaint) {
    // resize：重建 surface，tree.layout()
  }
  for (const dg::PointerEvent& e : pumped.pointer) {
    // 喂给 interaction，更新 widgets/tree
  }
  // 处理 pumped.key / pumped.text_input / pumped.text_editing（若使用文本）

  tree.layout();
  const dg::RepaintStats stats = tree.render().repaint(*surface);
  const dg::PixelView view = surface->peek_pixels();
  const dg::ImageView image{view.pixels, view.width, view.height, view.row_bytes,
                            dg::PixelFormat::kBgra8888};
  manager.present(window, image, tree.render().painted().rects());
}
```

## 5. 每帧循环的强制顺序

这个顺序不是风格建议，是接口要求：

```
pump()
  → 先处理 needs_repaint（含 resize + 重建 surface + 重新 layout）
  → 再处理 pointer
  → 再处理 key / text_input / text_editing
→ layout()
→ repaint()
→ present(damage)
```

依据：`include/drawgui/window/window_manager.h:442` 附近的注释——一次
`pump()` 里，resize 已经移动了所有 widget 的位置，如果先处理指针事件，
命中测试会打在旧的布局上。`doc/contracts.md` 有更完整的论证，这里只记
顺序本身。

## 6. 最小 C ABI 程序

从 `examples/19_c_client/main.c` 提炼，严格顺序：

```c
#include "drawgui/abi/drawgui.h"
#include <string.h>

int main(void) {
  dg_app_t* app = dg_app_create(NULL);
  if (app == NULL) { return 1; }

  dg_window_opts opts;
  memset(&opts, 0, sizeof(opts));   /* 先清零，再设 size —— */
  opts.size = sizeof(opts);         /* 所有 ABI 结构体首字段是 size，
                                        这是给未来版本用的兼容机制 */
  opts.title = "my app";
  opts.width = 320;
  opts.height = 200;
  opts.background_argb = 0xFF14171FU;
  opts.resizable = 1;               /* 0.2 起：可拖拽改变大小，最小 240x160 */
  opts.min_width = 240;
  opts.min_height = 160;

  dg_window_t* window = dg_window_create(app, &opts);
  if (window == NULL) { dg_app_destroy(app); return 1; }

  /* 待附加节点：此时还不是 live 节点 */
  dg_node_t* button = dg_node_create(app, DG_NODE_TYPE_BUTTON);

  /* 附加到窗口后才 live */
  if (dg_window_set_root(window, button) != DG_ERR_OK) {
    dg_app_destroy(app);
    return 1;
  }

  /* 可选：设属性 */
  dg_value color;
  color.size = sizeof(color);
  color.type = DG_VALUE_COLOR;
  color.bits = 0xFF2E7D32U;
  dg_node_set_prop(button, DG_PROP_BACKGROUND_COLOR, &color);

  for (;;) {
    dg_event events[16];
    int32_t pending = dg_wait_events(app, 40);
    if (pending < 0) { break; }
    pending = dg_poll_events(app, events, 16);
    for (int32_t i = 0; i < pending; ++i) {
      if (events[i].kind == DG_EVENT_CLICK) {
        /* ... */
      } else if (events[i].kind == DG_EVENT_WINDOW_RESIZED) {
        /* x/y 是新的可绘制宽高；drawgui 已经按新尺寸重绘过了 */
      } else if (events[i].kind == DG_EVENT_WINDOW_CLOSED) {
        goto done;
      }
    }
  }
done:
  dg_app_destroy(app);
  return 0;
}
```

窗口创建后还可以用 `dg_window_set_title()`、`dg_window_set_size()`、
`dg_window_set_position()`、`dg_window_set_fullscreen()`、
`dg_window_minimize()` 等操作它，用 `dg_window_get_state()` 查询状态——
完整列表见 [`doc/reference/c-abi.md`](reference/c-abi.md) 第 5 节。C++ 侧对应的是
`dg::WindowManager` 上同名的 `set_title()`/`set_size()`/…/`state()`。

## 7. 下一步

- 引擎的几个非常规设计假设（两棵树、侧表模式、属性寻址、damage 驱动、
  exactly-once layout 及其例外、文本不参与布局、整数设备像素、零 virtual）：
  见 [`doc/concepts.md`](concepts.md)。
- 逐主题的用法：[`doc/guide/`](guide/)（layout、text、widgets、theming、
  input、popups、animation）。
- 必须遵守但代码不强制的约定（比如上面第 5 节的循环顺序为什么是约定）：
  [`doc/contracts.md`](contracts.md)。
- 能力边界与已知缺口（比如 C ABI 现状、`transform` 属性未实现）：
  [`doc/limitations.md`](limitations.md)。
- 属性 / token / action / C ABI 的完整清单：[`doc/reference/`](reference/)。
- 想贡献代码：[`doc/contributing.md`](contributing.md)。
