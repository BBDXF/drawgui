# 快速上手

drawgui 是一个 C++20 自绘 GUI 内核：CPU 光栅（Skia），SDL3 窗口后端（仅 Linux），
并导出一份纯 C ABI 供其它语言嵌入。

C++ 程序从运行时 `dg::App` / `dg::Window` 开始（`include/drawgui/app/`）：
建窗口、建节点、设属性，然后 `App::run()`，运行时负责布局、绘制、输入分发、
焦点、弹层和主题。底层对象（`LayoutTree`、`WidgetSet`、`Focus`……）仍然公开，
需要时可以直接用，见本文第 5 节。

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

想看引擎能做到什么，直接跑 `./build/examples/drawgui_showcase`
——它用公开的 `dg::App` / `dg::Window` 运行时搭成一个窗口，按页签展示全部功能
（基础控件、文本、布局、数据、弹层、图片、窗口），右上角可实时切换浅色 / 深色，
底部状态栏显示最近一个事件；`--tab N --theme dark --dump-png out.png` 可直接截图。
其余 example 各演示一个切面。`examples/` 下每个子目录对应 `build/examples/` 里一个
`drawgui_*` 可执行文件；没有 `--help`，参数列在各自 `main` 源文件开头的注释里。

## 2. 最小 C++ 程序：`dg::App`

```cpp
#include "drawgui/app/app.h"
#include "drawgui/app/prop.h"
#include "drawgui/app/window.h"
#include "drawgui/render/prop_ids.generated.h"

int main() {
  dg::App app = dg::App::create().value();
  (void)app.add_font("/usr/share/fonts", "DejaVu Sans");

  dg::WindowOptions options;
  options.title = "hello";
  options.width = 320;
  options.height = 200;
  dg::Window* window = app.create_window(options).value();

  const dg::NodeId root = window->create(dg::NodeKind::kBox).value();
  window->set(root, {dg::prop::width_percent(100), dg::prop::height_percent(100),
                     dg::prop::column(), dg::prop::gap(8)});
  const dg::NodeId button = window->create(dg::NodeKind::kButton, root).value();
  window->set_text(button, DG_PROP_TEXT, "Click me");

  app.run([&](const dg::Event& event) {
    if (const auto* click = dg::event_as<dg::events::ClickEvent>(event)) {
      if (click->node == button) {
        window->set_text(button, DG_PROP_TEXT, "Clicked");
      }
    }
  });
}
```

要点：

- 第一个不带父节点的 `create()` 成为窗口的根。普通 `kBox` 默认是 240×120，
  做根或做行/列容器时记得给它尺寸（上面用了百分比）或 `grow`。
- `dg::prop::*`（`drawgui/app/prop.h`）是常用属性的类型化写法；其它属性用
  `set_prop(node, DG_PROP_..., dg::PropValue::...)`。
- 列表、下拉框、页签、树、表格、进度条通过 `window->list(node)` 等拿到
  类型化引用来填数据；用户操作以 `dg::Event` 报告。
- 完整 API、逃生舱约定：[`doc/guide/app.md`](guide/app.md)。
- 示例：`examples/25_showcase`、`11_form_controls`、`12_text_input`、`15_list`、
  `22_dropdown_menu`、`23_menu_tooltip_dialog`、`27_data_widgets` 都建在 `dg::App` 上，
  它们的 `--verify` 模式演示了如何在无显示器环境下注入输入并检查结果
  （`examples/common/example_app.h`）。

## 3. C++ 运行时 vs C ABI

C ABI（`drawgui.h`）是 `dg::App` 之上的薄壳，节点种类、属性、事件一一对应：

- 开窗并操作窗口（`dg_window_create`、`dg_window_set_size` 等）
- 十六种节点：`BOX`、`BUTTON`、`LABEL`、`CHECKBOX`、`RADIO`、`SLIDER`、`TEXT_FIELD`、
  `TEXT_AREA`、`SCROLL_VIEW`、`LIST`、`IMAGE`，以及组件 `DROPDOWN`/`PROGRESS`/`TABS`/
  `TREE`/`TABLE`；另有菜单、对话框、tooltip 函数（`examples/28_c_components`）
- 68 个 `dg_prop_id`（`dg_node_set_prop` / `dg_node_set_text`）
- 节点插入、移动、删除；焦点、定时器、跨线程唤醒；点击、右键、焦点、
  列表选择、文字变化、action、窗口状态等事件
- 字体目录、主题

`examples/19_c_client/main.c` 演示了其中大部分；`examples/26_text_area`
演示了行容器 + 多行编辑框。

只有直接使用 C++ 底层才有的：动画 API、渐变/阴影的专用 setter、自定义绘制、
原生对话框窗口（`WindowManager::open_dialog()`）等（见 `doc/limitations.md` C 节）。

## 4. 离屏光栅

不开窗，直接画一帧、编码 PNG，`examples/00_cpu_raster_png/main.cpp` 的骨架：

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

用 `dg::App` 时不需要自己做这一步：`Window::frame()` 返回上一次
`pump()` 画好的帧，`frame()->encode_png()` 即可。

## 5. 直接使用底层

`dg::App` 内部做的事，也可以自己做——`examples/10_scrolling`、`14_popup`、
`21_focus` 就是这样写的教学示例。这时一个交互式窗口至少要持有：

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

每帧的顺序是接口要求，不是风格建议：

```
pump()
  → 先处理 needs_repaint（含 resize + 重建 surface + 重新 layout）
  → 再处理 pointer
  → 再处理 key / text_input / text_editing
→ layout()
→ repaint()
→ present(damage)
```

依据：`include/drawgui/window/window_manager.h` 中 `PumpResult` 附近的注释——
resize 已经移动了所有 widget，先处理指针事件会命中旧布局。`doc/contracts.md`
有完整论证。各 guide 的「直接使用底层」一节列出每个主题要自己承担的义务；
如果只是想在 `dg::App` 里补一点底层操作，看 [`guide/app.md`](guide/app.md)
的逃生舱约定。

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
`dg::Window` 上同名的 `set_title()`/`set_size()`/…/`state()`。

## 7. 下一步

- 引擎的几个非常规设计假设（两棵树、侧表模式、属性寻址、damage 驱动、
  exactly-once layout 及其例外、文本不参与布局、整数设备像素、零 virtual）：
  见 [`doc/concepts.md`](concepts.md)。
- 运行时 API：[`doc/guide/app.md`](guide/app.md)；逐主题的用法：
  [`doc/guide/`](guide/)（layout、text、widgets、theming、input、popups、animation）。
- 必须遵守但代码不强制的约定（比如第 5 节的循环顺序为什么是约定）：
  [`doc/contracts.md`](contracts.md)。
- 能力边界与已知缺口（比如 C ABI 现状、`transform` 属性未实现）：
  [`doc/limitations.md`](limitations.md)。
- 属性 / token / action / C ABI 的完整清单：[`doc/reference/`](reference/)。
- 想贡献代码：[`doc/contributing.md`](contributing.md)。
