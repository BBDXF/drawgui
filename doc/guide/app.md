# C++ 运行时：`dg::App` / `dg::Window`

C ABI（`drawgui.h`）是这一层之上的薄壳。C++ 程序可以直接使用它：
公开头文件都在 `include/drawgui/app/`。本页是 API 参考；`doc/guide/` 里
每篇都先讲 `dg::App` 的用法，再在「直接使用底层」一节讲不用运行时时要自己
承担的义务。建在运行时上的示例：`examples/11_form_controls`、`12_text_input`、
`15_list`、`22_dropdown_menu`、`23_menu_tooltip_dialog`、`25_showcase`、
`27_data_widgets`（共用的启动代码在 `examples/common/example_app.h`）；其余编号
示例是直接使用底层的教学示例。

| 头文件 | 内容 |
|---|---|
| `app.h` | `dg::App`、`AppOptions`、`WindowOptions`、`Waker` |
| `window.h` | `dg::Window`：节点树、属性、焦点、弹层、OS 窗口操作、逃生舱 |
| `refs.h` | 组件引用：`ListRef`、`DropdownRef`、`TabsRef`、`TreeRef`、`TableRef`、`ProgressRef`、`SliderRef`、`RadioRef` |
| `items.h` | 值类型：`MenuEntry`、`TreeEntry`、`TableColumnSpec`、`DialogOptions` |
| `events.h` | `dg::Event`（`std::variant`）及 `Overloaded`、`event_as<E>()`、`window_of()` |
| `limits.h` | 数组上限 `dg::limits::kListMaxItems` 等（C ABI 用同一组常量） |
| `prop.h` | `dg::prop::width()/height()/row()/column()/gap()/grow()/…`：配合 `Window::set()` 的类型化属性 |
| `status.h` / `node_kind.h` | `dg::Status`、`dg::NodeKind` |

## 线程模型

全部单线程，唯一例外是 `App::waker()` 返回的 `Waker`：它的 `wake()` 与
`post_user_event()` 可以从任意线程调用（`App` 必须仍然存活）。

## App

- `App::create()` → `Expected<App, AppError>`。
- `create_window(WindowOptions)` → `Window*`，由 App 持有。
- `destroy_window(Window*)`：关闭 OS 窗口（若用户还没关）并释放 `Window`。
  之后该指针、指向它的所有 ref、所有逃生舱引用都悬空；已排队的事件仍带着
  它的 `WindowId`，但 `find()` 不再解析它。用户关闭的窗口（收到
  `WindowClosedEvent`、`Window::closed()` 为真）会一直留在 `windows()` 里，
  直到调用 `destroy_window()`。C ABI 没有对应的 `dg_window_destroy`，窗口随
  `dg_app_destroy()` 一起释放。
- `pump(timeout_ms)`：等待（`-1` 为无限，但会被 tooltip 截止时间、动画帧、
  定时器、`Waker` 提前唤醒）、分发、重绘，返回期间产生的全部事件。
  `poll()` 只取走已排队的事件，不等待。
- `run(on_event)`：循环 `pump(-1)`，逐个把事件交给 `on_event`，直到所有窗口
  都已关闭/销毁，或回调里调用了 `quit()`（同一批里剩下的事件留给下一次
  `pump()`/`poll()`）。没有打开的窗口时立即返回。
- 资源：`add_font()`、`images()`（`ImageCatalog`，解码后用
  `Window::set_image()` 显示）、`set_theme()` / `set_theme_variant()` /
  `override_color()` / `override_int()`（见 [theming.md](theming.md)）、`set_tooltip_delay()`、`add_timer()` / `cancel_timer()`。

## Window

节点用 `create(kind, parent, before)` 创建，之后以 `NodeId` 指代。组合类节点
（复选框的指示器、编辑器的光标、列表的行、组件内部部件）自带的子节点不会
由 `create()` 返回，只能通过拥有它们的节点访问。任何不是本窗口存活的
`create()` 节点的 id 都得到 `Status::kInvalidNode`。

- 树：`create`、`move`、`remove`（`set_on_removed` 回调对随之移除的每个节点
  触发一次）、`kind_of`、`child_container`。
- 属性：`set_prop`、`set(node, {dg::prop::width(120), dg::prop::row()})`（逐个应用，返回第一个失败）、`bind_token`、`set_text` / `text`、`checked`（checkbox 与 radio）、
  `set_disabled`（被禁用的子树不再接收指针/键盘；进行中的按下不会产生
  `ClickEvent`，焦点会移走）、`set_tooltip`、`set_image`、`scope_action`。
- 尺寸：`kBox` 等节点创建时带默认尺寸（`kBox` 240×120），`prop::width_auto()` /
  `prop::height_auto()` 去掉它，回到按内容/由父容器决定（[layout.md](layout.md)）。
- 主题：按钮、复选框、单选的颜色来自主题 token（与编辑框、滚动条一样），切换
  主题自动重涂；你在它们上 `set_prop`/`bind_token` 的 `BACKGROUND_COLOR`/
  `TEXT_COLOR`/`BORDER_COLOR` 优先（背景色的悬停/按下色由它派生），不必再改
  `widgets()` 里的 `fill_*`。
- 焦点：`focus`、`clear_focus`、`focused`；变化以 `FocusChangedEvent` 报告。
- 弹层：`open_menu(anchor, at, std::vector<MenuEntry>)`、`close_menu`、
  `open_dialog(DialogOptions)`、`close_dialog`；返回的 id 会出现在对应的
  `MenuEvent` / `DialogResultEvent` 里。

## 调试

- `node_count()`：本窗口渲染树里的全部节点数（含组合子节点和弹层），适合断言
  「虚拟化列表的节点数不随数据增长」「弹层关闭后节点数回到基线」。
- `frame()`：上一次 `pump()` 画好的帧（`RasterSurface`），首帧前为 null；
  `frame()->encode_png()` 即可做 `--dump-png`。

## 组件引用（refs）

```cpp
dg::Expected<dg::ListRef, dg::Status> list = window->list(node);
```

`list()/dropdown()/tabs()/tree()/table()/progress()/slider()/radio()` 检查一次节点种类：
不存在为 `kInvalidNode`，种类不对为 `kUnsupported`。ref 是 `{窗口, 节点}`
的廉价值，可随意复制；它不保活节点，每次调用都会重新检查，节点被移除后
返回 `kInvalidNode`。ref 不得比它的 `Window` 活得更久。

用 ref 的 `select()`/`set_value()` 改选择或值不会产生事件；只有用户操作才产生事件。
`SliderRef`：`set_value()`（夹取并对齐 step；非有限数为 `kValueOutOfRange`）、
`value()`、`set_range(min, max, step)`、`min_value()/max_value()/step()`。
`RadioRef`：`select()`、`selected()`、`set_group(int)`、`group()`（默认 0；一个已选中的
单选加入已有选中项的组时会放弃自己的选中）。
表格表头点击只报告 `TableEvent::Kind::kSort` 请求，排序由调用方完成后
`set_rows()` + `set_sort_indicator()`。

## 事件

```cpp
for (const dg::Event& e : app.pump(-1)) {
  std::visit(dg::Overloaded{
      [&](const dg::events::ClickEvent& c) { /* c.node, c.clicks */ },
      [&](const dg::events::WindowClosedEvent& c) { app.destroy_window(app.find(c.window)); },
      [](const auto&) {},
  }, e);
}
```

`event_as<E>(e)` 返回指针或 null；`window_of(e)` 给出事件所属窗口
（`TimerEvent`/`UserEvent` 没有）。

- `SliderEvent{window, node, value, previous}`：用户（按下、拖动、按键）把滑块移到了
  不同的值；`value`/`previous` 是 float。C ABI 对应 `DG_EVENT_SLIDER_CHANGED`（不带值，
  用 `dg_slider_get_value()` 查询）。
- 复选框和单选不新增事件：`ClickEvent` 到达时状态已经改好，用 `checked()` 查询；
  方向键选中单选时 `ClickEvent::clicks == 0`。

## 逃生舱约定

`Window::layout()`、`widgets()`、`focus_state()`、`interaction()`、
`theme_bindings()`、`animation()`、`action_scopes()`、`popups()` 以及
`App::window_manager()`、`fonts()`、`images()` 暴露运行时内部对象：

1. 在两次 `pump()` 之间读取总是安全的。
2. 可以修改运行时不组合的东西（在 `kBox` 下自建的节点、`create()` 节点的
   样式）；改完调用 `Window::invalidate(node)`，下一次 `pump()` 会重新布局并
   重绘该子树。
3. 不要通过它们移除/移动 `create()` 节点或组合子节点（用 `remove()`/`move()`），
   也不要跨 `destroy_window()` 持有引用。
4. 主题切换、尺寸变化时运行时会重建组合子节点和主题绑定属性，覆盖你直接写入
   的值（按钮/复选框/单选的 `Widget::fill_*` 也会被主题重写——改颜色请写属性）。
5. 尺寸、占位符、换行、内边距、图片着色都有属性（`width_auto()`、
   `DG_PROP_PLACEHOLDER`、`wrap_text()`、`padding_*`、`image_tint()`），不需要
   再改 `BoxStyle`/`TextStyle`/`ImageStyle`。

测试用的输入注入走 `App::window_manager()` 的 `post_pointer_button()`、
`post_key()`、`post_logical_key()`、`post_text_input()` 等——与 C ABI 的
`dg_debug_*` 是同一条路径（见 `tests/unit/runtime_fixture.h`、
`examples/common/example_app.h`）。注意：SDL dummy 驱动下同时开着多个窗口时，
`warp_pointer()` 产生的移动事件可能不会送达后建的窗口，需要拖动的检查最好只开一个窗口。

## 示例

```cpp
#include "drawgui/app/app.h"
#include "drawgui/app/window.h"
#include "drawgui/render/prop_ids.generated.h"

int main() {
  dg::App app = dg::App::create().value();
  (void)app.add_font("/usr/share/fonts", "Noto Sans");
  dg::WindowOptions options;
  options.title = "hello";
  options.width = 320;
  options.height = 240;
  dg::Window* window = app.create_window(options).value();

  const dg::NodeId root = window->create(dg::NodeKind::kBox).value();
  window->set_prop(root, DG_PROP_DIRECTION, dg::PropValue::option(DG_DIRECTION_COLUMN));
  const dg::NodeId button = window->create(dg::NodeKind::kButton, root).value();
  window->set_text(button, DG_PROP_TEXT, "Add");
  const dg::NodeId list_node = window->create(dg::NodeKind::kList, root).value();
  // 外观与 kTree/kTable 一致：1px color.border 边框、color.surface 底，滚动条在边框内，
  // 选中行是同样的浅色主色调圆角底，行高/字号/内边距取同一套控件样式；
  // 应用自己设置的 DG_PROP_BACKGROUND_COLOR / DG_PROP_BORDER_COLOR 优先，切换主题也不会被覆盖。
  dg::ListRef list = window->list(list_node).value();
  std::vector<std::string> rows;

  app.run([&](const dg::Event& event) {
    if (const auto* click = dg::event_as<dg::events::ClickEvent>(event)) {
      if (click->node == button) {
        rows.push_back("row " + std::to_string(rows.size()));
        (void)list.set_items(rows);
      }
    }
  });
}
```
