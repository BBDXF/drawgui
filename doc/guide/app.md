# C++ 运行时：`dg::App` / `dg::Window`

C ABI（`drawgui.h`）是这一层之上的薄壳。C++ 程序可以直接使用它：公开头文件都在
`include/drawgui/app/`。本页是运行时的总览与 API 参考；`doc/guide/` 里其它每篇都先讲
`dg::App` 的用法，再在「直接使用底层」一节讲不用运行时时要自己承担的义务。

建在运行时上的示例：`examples/11_form_controls`、`12_text_input`、`15_list`、
`22_dropdown_menu`、`23_menu_tooltip_dialog`、`25_showcase`（大多数控件放在一个窗口里）、
`27_data_widgets`、`29_enterprise`（一个完整的资产管理桌面应用：菜单栏、工具栏、树/表格/
页签主从界面、行菜单、快捷键、表单对话框、关闭确认、动画）。共用的启动代码在
`examples/common/example_app.h`；其余编号示例是直接使用底层的教学示例。

| 头文件 | 内容 |
|---|---|
| `app.h` | `dg::App`、`AppOptions`、`WindowOptions`、`Waker`、`kUserActionBase` |
| `window.h` | `dg::Window`：节点树、属性、用户数据、焦点、组件 ref、弹层、动画、OS 窗口操作、逃生舱 |
| `refs.h` | 组件引用：`ListRef`、`DropdownRef`、`TabsRef`、`TreeRef`、`TableRef`、`ProgressRef`、`SliderRef`、`RadioRef`、`MenuBarRef` |
| `items.h` | 值类型：`MenuEntry`、`MenuBarMenu`、`RowItem`、`RowAction`、`TableCell`、`TreeEntry`、`TableColumnSpec`、`DialogOptions` |
| `events.h` | `dg::Event`（`std::variant`）及 `Overloaded`、`event_as<E>()`、`window_of()` |
| `limits.h` | 数组上限 `dg::limits::kListMaxItems` 等（C ABI 用同一组常量） |
| `prop.h` | `dg::prop::width()/height()/width_auto()/row()/column()/gap()/grow()/visible()/checked()/row_height()/…`：配合 `Window::set()` 的类型化属性 |
| `status.h` / `node_kind.h` | `dg::Status`、`dg::NodeKind` |

## 线程模型

全部单线程，唯一例外是 `App::waker()` 返回的 `Waker`：它的 `wake()` 与
`post_user_event()` 可以从任意线程调用（`App` 必须仍然存活）。

## App 与窗口的生命周期

- `App::create()` → `Expected<App, AppError>`。`App` 可移动、不可复制。
- `create_window(WindowOptions)` → `Window*`，由 App 持有。`WindowOptions` 有标题、尺寸、
  背景色、`resizable`、最小/最大尺寸，以及 `close_confirmation`（见下文「关闭确认」）。
- 用户关闭窗口（或调用 `Window::close()`）后产生 `WindowClosedEvent`；`Window` 对象仍留在
  `windows()` 里（`closed()` 为真，之后对它的调用都是空操作），`find(id)` 不再返回它。
- `destroy_window(Window*)`：关闭 OS 窗口（若还开着）并释放 `Window`。之后该指针、指向它的
  所有 ref、所有逃生舱引用都悬空；已排队的事件仍带着它的 `WindowId`，但 `find()` 不再解析它。
  不要在运行时代表这个窗口执行的回调里调用它。C ABI 没有对应的 `dg_window_destroy`，窗口随
  `dg_app_destroy()` 一起释放（`dg_window_close()` 只关闭 OS 窗口）。
- `pump(timeout_ms)`：等待（`-1` 为无限，但会被 tooltip 截止时间、动画帧、定时器、`Waker`
  提前唤醒）、分发、重绘，返回期间产生的全部事件。`poll()` 只取走已排队的事件（例如
  `Window::focus()` 产生的 `FocusChangedEvent`），不等待。
- `run(on_event)`：循环 `pump(-1)`，逐个把事件交给 `on_event`，直到所有窗口都已关闭/销毁，
  或回调里调用了 `quit()`（同一批里剩下的事件留给下一次 `pump()`/`poll()`）。没有打开的窗口时
  立即返回。回调里可以创建、销毁、修改窗口。
- 资源：`add_font()`（第一个加入的字体族是默认字体）、`load_image()` / `load_image_file()`
  （PNG/JPEG/WebP/SVG，返回的 `ImageId` 用于 `Window::set_image()`、`RowItem::icon`、
  `TreeEntry::icon`、`TableCell::icon`）、`set_theme()` / `set_theme_variant()` /
  `override_color()` / `override_int()`（见 [theming.md](theming.md)）、`set_tooltip_delay()`、
  `add_timer()` / `cancel_timer()`。
- 动画：`set_reduced_motion(bool)`、`set_clock(fn)`，见 [animation.md](animation.md)。
- 快捷键：`add_shortcut()`、`remove_shortcut()`、`chord_label()`，见下文「快捷键」。

## Window

节点用 `create(kind, parent, before)` 创建，之后以 `NodeId` 指代。组合类节点（复选框的
指示器、编辑器的光标、列表的行、组件内部部件）自带的子节点不会由 `create()` 返回，只能
通过拥有它们的节点访问。任何不是本窗口存活的 `create()` 节点的 id 都得到
`Status::kInvalidNode`。

- 树：`create`、`move`、`remove`（`set_on_removed` 回调对随之移除的每个 `create()` 节点
  触发一次）、`kind_of`、`child_container`（滚动视图的子节点实际在它的内容节点下）、
  `contains(ancestor, node)`（事件报告的是指针下最内层的 `create()` 节点，判断"是否在某区域内"
  用它）。
- 属性：`set_prop`、`set(node, {dg::prop::width(120), dg::prop::row()})`（逐个应用，返回第一个
  失败）、`bind_token`、`set_text` / `text`、`checked`（复选框、单选，以及切换按钮）、
  `set_disabled`（被禁用的子树不再接收指针/键盘；进行中的按下不会产生 `ClickEvent`，焦点会
  移走）、`set_tooltip`、`set_image`、`scope_action`。`prop::visible(false)` 把节点按
  `display:none` 隐藏（见 [layout.md](layout.md)）。
- 用户数据：`set_user_data(node, std::uint64_t)` / `user_data(node)` 在节点上存一个不透明的
  64 位值（下标、转成整数的指针……），省得自己维护 `NodeId → 对象` 的表。运行时从不读取、
  从不解引用它，事件里也不带；没设过为 0，`move` 保留，`remove` 后随节点消失（之后
  `kInvalidNode`，复用同一槽位的新节点从 0 开始）。组合类节点的内部子节点不能设
  （`kInvalidNode`）。`examples/29_enterprise` 用它给「标签」页里动态生成的复选框和移除按钮记标签 id。
- 尺寸：`kBox` 等节点创建时带默认尺寸（`kBox` 240×120），`prop::width_auto()` /
  `prop::height_auto()` 去掉它，回到按内容/由父容器决定（[layout.md](layout.md)）。
- 主题：按钮、复选框、单选等控件的颜色来自主题 token，切换主题自动重涂；你在它们上
  `set_prop`/`bind_token` 的 `BACKGROUND_COLOR`/`TEXT_COLOR`/`BORDER_COLOR` 优先（背景色的
  悬停/按下色由它派生），不必去改 `widgets()` 里的 `fill_*`。
- 组件：`list()/dropdown()/tabs()/tree()/table()/progress()/slider()/radio()/menu_bar()` 返回
  类型化 ref，见下文「组件引用」。
- 弹层：`open_menu`、`close_menu`、`open_dialog`、`close_dialog`、`has_dialog`、
  `set_dialog_button_disabled`，见 [popups.md](popups.md)。
- 动画：`animate`、`set_transition`、`cancel_animation`、`animating`，见
  [animation.md](animation.md)。
- OS 窗口：`set_title`/`title`、`set_size`/`size`、`set_position`/`position`、`minimize`、
  `maximize`、`restore`、`set_fullscreen`、`set_resizable`、`set_min_size`/`set_max_size`、
  `set_icon`、`state`；用户造成的变化以 `WindowStateEvent` 报告。关闭见下文「关闭确认」。

### 焦点：`focused()` 与 `focused_node()`

`focus(node)`、`clear_focus()` 改焦点，变化以 `FocusChangedEvent` 报告（`focus()` 产生的事件
用 `poll()` 或下一次 `pump()` 取到；隐藏或禁用的节点返回 `kUnsupported`）。

- `focused()` 返回焦点所在的最近 `create()` 节点：焦点落在表格的外框、某个页签这类内部部件
  上时，它返回组件本身，与 `FocusChangedEvent::node` 一致，所以 `focused() == table` 成立。
  表单对话框内容里的控件是你 `create()` 的，照常返回；焦点只在弹层自带的部件上（菜单行、
  对话框自带按钮）或没有焦点时返回 `nullopt`。
- `focused_node()` 返回原始渲染节点（组合部件、菜单里高亮的那一行都算），用于诊断和示例的
  自检。

`kList` 和树/表格一样可聚焦（键盘导航见 [widgets.md](widgets.md)「列表的键盘」）。

## 组件引用（refs）

```cpp
dg::Expected<dg::ListRef, dg::Status> list = window->list(node);
```

`list()` 等函数检查一次节点种类：不存在为 `kInvalidNode`，种类不对为 `kUnsupported`。ref 是
`{窗口, 节点}` 的廉价值，可随意复制；它不保活节点，每次调用都会重新检查，节点被移除后返回
`kInvalidNode`。ref 不得比它的 `Window` 活得更久。用 ref 的 `select()`/`set_value()` 等改选择
或值不会产生事件；只有用户操作才产生事件。

| ref | 主要方法 |
|---|---|
| `ListRef` | `set_items(strings, item_height)`、`set_rows(RowItem…, item_height)`、`set_row_actions()`、`row_checked()`、`select()`/`selected()`、行键 `set_row_keys()`/`key_of()`/`index_of()`/`select_key()` |
| `TreeRef` | `set_items(TreeEntry…)`、`select()`/`selected()`、`set_expanded()`、`row_checked()`、行键四件套、`set_row_height()` |
| `TableRef` | `set_columns()`、`column_count()`、`set_rows()`/`set_cells()`、`cell_checked()`、`select()`/`selected()`、`set_sort_indicator()`、行键四件套、`set_row_height()` |
| `DropdownRef` | `set_items()`、`select()`/`selected()`（占位符是 `set_text(DG_PROP_TEXT)`） |
| `TabsRef` | `add(label, disabled)` 返回面板节点、`select()`/`selected()` |
| `ProgressRef` | `set_value()`/`value()`、`set_indeterminate()`/`indeterminate()` |
| `SliderRef` | `set_value()`（夹取并对齐 step）、`value()`、`set_range(min, max, step)`、`min_value()/max_value()/step()` |
| `RadioRef` | `select()`、`selected()`、`set_group()`/`group()`（默认 0；已选中的单选加入已有选中项的组时会放弃自己的选中） |
| `MenuBarRef` | `set_menus(MenuBarMenu…)`、`set_entry(menu, item, entry)` |

表格表头点击只报告 `TableEvent::Kind::kSort` 请求，排序由调用方完成后 `set_rows()`/
`set_cells()`（行键随之清空，要重新 `set_row_keys()`）+ `set_sort_indicator()`。富行、行键、
行部件、行高的细节见 [widgets.md](widgets.md)。

## 事件

```cpp
for (const dg::Event& e : app.pump(-1)) {
  std::visit(dg::Overloaded{
      [&](const dg::events::ClickEvent& c) { /* c.node, c.clicks, c.item_key */ },
      [&](const dg::events::WindowClosedEvent& c) { app.destroy_window(app.find(c.window)); },
      [](const auto&) {},
  }, e);
}
```

`event_as<E>(e)` 返回指针或 null；`window_of(e)` 给出事件所属窗口（`TimerEvent`/`UserEvent`
没有）。完整列表（`events.h`，每种对应一个 C ABI 的 `DG_EVENT_*`）：

| 事件 | 何时产生 |
|---|---|
| `ClickEvent{node, x, y, clicks, item, column, item_key}` | 完成一次主键点击。`clicks` 是连击数；按在 List/Table 行或 Tree 条目上时 `item`（树为先序下标）、`column`（只有表格）和 `item_key`（行键，0 = 无）有值。复选框/单选/切换按钮到达时状态已改好（问 `checked()`）；方向键选中单选时 `clicks == 0`、`x/y == 0` |
| `ContextClickEvent{node, x, y, clicks, item, column, item_key}` | 右键按下（先选中被按的行）；或键盘的 Menu 键 / Shift+F10（`clicks == 0`，见 [input.md](input.md)） |
| `ActionEvent{node, action_id}` | 路由到的快捷键 action；`node` 为空表示应用级。`action_id >= kUserActionBase` 是应用自己绑定的 |
| `TextChangedEvent{node}` | 用户编辑改变了编辑框的已提交文字 |
| `FocusChangedEvent{node}` | 焦点移动；`node` 为空表示失焦 |
| `ListSelectEvent{node, index, clicks}` | 列表选中变化（键盘时 `clicks == 0`） |
| `ValueChangedEvent{node, selected, previous}` | 下拉框选中变化 |
| `MenuEvent{anchor, item, menu_id, bar_menu, command}` | 菜单项被选中（`item`）或菜单被关闭（`item` 为空）；菜单栏的菜单带 `bar_menu`；`command` 是 `MenuEntry::command` |
| `DialogResultEvent{button, dialog_id}` | 对话框结束；`button == -1` 表示没有取消按钮时按了 Escape |
| `TabSelectEvent{node, index}` | 用户切换页签 |
| `TreeEvent{kind, node, item}` | 树的选中/展开/折叠 |
| `TableEvent{kind, node, index, clicks}` | 表格行选中（`index` = 行）或排序请求（`index` = 列） |
| `SliderEvent{node, value, previous}` | 用户把滑块移到不同的值（float）。C ABI 的 `DG_EVENT_SLIDER_CHANGED` 不带值，用 `dg_slider_get_value()` 查询 |
| `RowPartEvent{node, item, column, part, action, item_key}` | 按在行部件上：`kCheck`（行复选框，新状态已生效）或 `kAction`（列表行尾按钮，`action` 是它在 `set_row_actions()` 里的下标）。不产生 `ClickEvent`，也不改变选中 |
| `WindowStateEvent{kind, x, y}` | OS 改变了窗口：尺寸、位置、最小化/最大化/还原、获得/失去焦点、进入/离开全屏 |
| `CloseRequestedEvent{window}` | 开启了关闭确认的窗口，用户请求关闭；窗口仍然打开 |
| `WindowClosedEvent{window}` | 窗口已关闭 |
| `AnimationFinishedEvent{node, animation}` | `Window::animate()` 的动画走到终点（取消、节点删除、隐式过渡不报）。C ABI 为 `DG_EVENT_ANIMATION_FINISHED`（`x` = id） |
| `TimerEvent{timer}` | `App::add_timer()` 到期 |
| `UserEvent{payload}` | `Waker::post_user_event()` |

## 快捷键

```cpp
constexpr std::uint16_t kCmdSave = dg::kUserActionBase + 1;
constexpr std::uint16_t kCmdRename = dg::kUserActionBase + 2;
app.add_shortcut("Ctrl+S", kCmdSave);              // 应用级：所有窗口，ActionEvent::node 为空
window->add_shortcut(table, "F2", kCmdRename);      // 作用域级：焦点在 table 子树内时，node = table
std::string label = app.chord_label("Ctrl+S").value();   // "Ctrl+S"，放进 MenuEntry::shortcut_label
```

- `action_id` 必须 `>= kUserActionBase`（0x8000）；更小的是 `input/shortcuts.toml` 的内置 action。
  组合键解析失败或 id 太小返回 `kInvalidArgument`。
- `App::add_shortcut()` 对同一组合键再绑定即替换；`Window::add_shortcut()` 在同一个 scope 上
  重复绑定同一组合键返回 `kAlreadyAttached`。`App::remove_shortcut(chord)` 删除该组合键的全部
  用户绑定（应用级和所有窗口的作用域级），没有则 `kUnknownId`。
- `scope_action(node, action_id)` 让一个节点消费某个内置 action（第 2 级路由）。
- 编辑键优先、遮蔽规则、对话框期间的行为见 [input.md](input.md)「应用自定义快捷键」。
  菜单项显示快捷键文字、带命令号见 [popups.md](popups.md)。完整用法：`examples/29_enterprise`
  （菜单栏与快捷键共用一个命令分发）。

## 关闭确认

默认用户点标题栏关闭会立即关闭窗口并产生 `WindowClosedEvent`。需要「有未保存的修改，确定
关闭吗？」时打开关闭确认：

```cpp
dg::WindowOptions options;
options.close_confirmation = true;          // 或之后 win->set_close_confirmation(true)
...
[&](const dg::events::CloseRequestedEvent& c) {
  if (!dirty) { win->close(); return; }      // 接受
  (void)win->open_dialog(save_prompt);       // 什么都不做 = 拒绝
},
```

- 打开后，用户/平台发起的关闭只产生 `CloseRequestedEvent`，窗口保持打开。
- `Window::close()`：立即关闭 OS 窗口并排入 `WindowClosedEvent`（下一次 `pump()`/`poll()` 取到）；
  `Window` 仍留在 `windows()` 里（`closed()` 为真），直到 `destroy_window()`。已关闭时是空操作。
  `App::destroy_window()` 也算接受（不产生事件）。`close()`/`destroy_window()` 总是直接关闭，
  不受关闭确认影响。
- C ABI：`dg_window_set_close_confirmation(window, on)`、`DG_EVENT_CLOSE_REQUESTED`、
  `dg_window_close(window)`。
- 示例：`examples/29_enterprise` 把标题栏关闭和「文件 → 退出」走同一条路径：有未保存修改时弹出「保存并退出 / 不保存 / 取消」对话框。

## 调试

- `node_count()`：本窗口渲染树里的全部节点数（含组合子节点和弹层），适合断言「虚拟化列表的
  节点数不随数据增长」「弹层关闭后节点数回到基线」。
- `frame()`：上一次 `pump()` 画好的帧（`RasterSurface`），首帧前为 null；
  `frame()->encode_png()` 即可做 `--dump-png`。
- 测试用的输入注入走 `App::window_manager()` 的 `warp_pointer()`、`post_pointer_button()`、
  `post_key()`、`post_logical_key()`、`post_text_input()` 等——与 C ABI 的 `dg_debug_*` 是同一条
  路径（见 `tests/unit/runtime_fixture.h`、`examples/common/example_app.h`、
  `examples/29_enterprise/probe.h`）。SDL dummy 驱动下同时开着多个窗口时，`warp_pointer()` 产生的
  移动事件可能不会送达后建的窗口，需要拖动的检查最好只开一个窗口。

## 逃生舱约定

`Window::layout()`、`widgets()`、`focus_state()`、`interaction()`、`theme_bindings()`、
`animation()`、`action_scopes()`、`popups()` 以及 `App::window_manager()`、`fonts()`、`images()`
暴露运行时内部对象（`Window::impl()` 是运行时自己的状态，不属于公开约定）：

1. 在两次 `pump()` 之间读取总是安全的。
2. 可以修改运行时不组合的东西（在 `kBox` 下经 `layout()` 自建的节点、`create()` 节点的样式）；
   改完调用 `Window::invalidate(node)`，下一次 `pump()` 会重新布局并重绘该子树。
3. 不要通过它们移除/移动 `create()` 节点或组合子节点（用 `remove()`/`move()`），也不要跨
   `destroy_window()` 持有引用。
4. 主题切换、尺寸变化时运行时会重建组合子节点和主题绑定属性，覆盖你直接写入的值（按钮/
   复选框/单选的 `Widget::fill_*` 也会被主题重写——改颜色请写属性）。
5. 常见需求都已有属性，不需要再碰内部对象：尺寸（`width_auto()`）、隐藏（`visible()`）、
   占位符（`DG_PROP_PLACEHOLDER`）、换行（`wrap_text()`）、内边距（`padding_*`）、图片着色
   （`image_tint()`）、行高（`row_height()`）、切换按钮（`checked()`）。

这条约定也写在 [contracts.md](../contracts.md) 第 9 条。

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

`kList` 的外观与 `kTree`/`kTable` 一致（1px `color.border` 边框、`color.surface` 底、滚动条在
边框内、选中行是浅色主色调圆角底）；应用自己设置的 `DG_PROP_BACKGROUND_COLOR` /
`DG_PROP_BORDER_COLOR` 优先，切换主题也不会被覆盖。
