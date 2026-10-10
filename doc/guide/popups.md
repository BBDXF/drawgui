# 弹层（Popups）：下拉菜单、菜单、tooltip、对话框

## 用 `dg::App`

```cpp
// 右键菜单：在指针位置打开，选择结果以 MenuEvent 报告
if (const auto* c = dg::event_as<dg::events::ContextClickEvent>(event)) {
  window->open_menu(std::nullopt, dg::PixelPoint{c->x, c->y},
                    {{"Copy"}, {"Paste"}, {"Delete"}});
}
// MenuEvent::item：选中项下标；nullopt 表示被 Escape/点外部关闭

window->set_tooltip(button, "This button opens a tooltip");  // 悬停延迟后自动显示
app.set_tooltip_delay(400);

dg::DialogOptions dialog;
dialog.title = "Delete file?";
dialog.buttons = {"Cancel", "Delete"};
dialog.cancel_button = 0;
window->open_dialog(dialog);   // 结果：DialogResultEvent::button（-1 = 无取消按钮时的 Escape）
```

- `MenuEntry` 还有 `separator`、`disabled`、`checked`（`std::optional<bool>`，有值即带勾选标记）；
  `open_menu(anchor, ...)` 传节点时菜单开在该节点下方，`MenuEvent::anchor` 原样带回。
- `DialogOptions::default_button` 默认 0（Enter 激活、打开时聚焦），`cancel_button` 是 Escape 报告的按钮。
- 下拉框是一个节点（`NodeKind::kDropdown` + `DropdownRef`），选项列表的打开、
  高亮移动（上下键循环）、Enter/点击提交、Escape/点外部取消都由运行时处理，
  提交时报告 `ValueChangedEvent{selected, previous}`。
- `ContextClickEvent::node` 是指针下**最内层**的 `create()` 节点（例如卡片里的文字标签），
  不一定是你关心的区域本身；判断"是否点在某区域内"用 `window->contains(area, *c->node)`，
  不要写 `c->node == area`。
- 右键在**按下**时触发；菜单打开时，在菜单外右键会先关闭它（报告一次 `item = nullopt`
  的 `MenuEvent`），同一次按下仍产生 `ContextClickEvent`，可直接在新位置重新打开。
  松开右键不会关闭或激活刚打开的菜单。
- 每个窗口同时最多一个菜单（`open_menu()` 或菜单栏的）和一个对话框；再次 `open_menu()` 会先
  关掉已开的菜单，对话框已开时 `open_dialog()` 返回 `kUnsupported`。`open_dialog()` 会先关掉
  已开的菜单、下拉列表和 tooltip。对话框是模态的，打开期间 Tab 不会离开它，点击它外面的控件
  不会产生事件，点在禁用按钮或空白处也不会移走焦点。
- tooltip 的计时、显示、按下后隐藏都由运行时完成，不需要 `tick()`。

### 叠放：对话框之上的下拉列表和菜单

- 对话框内容里的下拉框可以打开：它的选项列表叠在对话框**之上**，打开期间先收到
  指针/键盘/文字事件；Escape 只关闭最上层的列表，再按一次才关闭对话框；
  列表关闭后对话框的模态焦点陷阱照旧。
- 对话框打开期间也可以 `open_menu()`：菜单叠在对话框**之上**，先收到所有指针/键盘/
  文字事件，选中照常报 `MenuEvent`；Escape 或点菜单外只关闭菜单，对话框留着，
  焦点陷阱恢复。在对话框内容里右键产生 `ContextClickEvent`（菜单已开时先关掉它），
  据此即可在对话框里弹右键菜单。关闭对话框会先关掉其上的菜单。
- 叠放只有这一层：菜单里不能再开子菜单。

### 菜单的其它细节

- 行菜单：按在 List/Table 行或 Tree 条目上的右键会先选中该行，`ContextClickEvent` 带
  `item`/`column`/`item_key`，据此在 `c->x, c->y` 打开作用于该行的菜单；键盘的 Menu 键 /
  Shift+F10 产生同样的事件（`item` = 选中行，坐标是节点左下角），见 [input.md](input.md)。
- 快捷键列与命令号：`MenuEntry::shortcut_label` 在该行右侧以暗色右对齐显示（只是
  文字，常用 `app.chord_label("Ctrl+S")` 生成；绑定本身仍要 `App::add_shortcut()`），
  菜单按最宽的快捷键文字加宽；`MenuEntry::command` 在选中时原样放进
  `MenuEvent::command`（0 = 无 / 被关闭）。`open_menu()` 和菜单栏都支持。

### 菜单栏（`NodeKind::kMenuBar`）

```cpp
const dg::NodeId bar = window->create(dg::NodeKind::kMenuBar, root).value();
dg::MenuBarRef menus = window->menu_bar(bar).value();
std::vector<dg::MenuBarMenu> m(2);
m[0].title = "File";
m[0].entries = {entry("New", kCmdNew, app.chord_label("Ctrl+N").value()), entry("Open", kCmdOpen)};
m[1].title = "Edit";
m[1].entries = {entry("Undo", kCmdUndo)};
menus.set_menus(m);
// MenuEvent{anchor = bar, bar_menu = 1, item = 0, command = kCmdUndo}
```

- 一行主题色标题（默认高 28，宽度随父容器），不接受子节点。
- 鼠标：点标题在其下方展开菜单（复用 `open_menu()` 的同一套菜单机制，同一时刻每窗口
  仍只有一个菜单），再点同一标题收起（报一次 `item = nullopt` 的 `MenuEvent`）；
  菜单展开时指针移到另一个标题上直接切换过去，不报关闭事件。
- 键盘：F10，或**单独**按下并松开 Alt，进入键盘模式——焦点不动，高亮第一个标题；
  Left/Right 在标题间循环，Down/Enter 展开；菜单展开时 Left/Right 切换到相邻菜单；
  Escape 先关菜单（报关闭事件，仍在键盘模式），再按一次退出；其它键、F10、点击
  菜单栏以外都会退出键盘模式。窗口里有多个菜单栏时 F10/Alt 只作用于第一个可用的。
- 事件：选中报 `MenuEvent{anchor = 菜单栏, bar_menu, item, command}`，关闭报
  `item = nullopt`（`bar_menu` 仍有值）。`open_menu()` 的菜单 `bar_menu` 为空。
- `set_entry(menu, item, entry)` 替换一项（切换勾选/禁用等），越界 `kValueOutOfRange`；
  该菜单正展开时先收起。`set_menus()` 替换全部（会关闭已展开的）。
- 不支持子菜单和助记符（`&File`）。
- 示例：`examples/29_enterprise` 的「文件 / 编辑 / 视图 / 帮助」菜单栏，「视图」里的勾选项用
  `set_entry()` 切换。

### 表单对话框（`DialogOptions::content`）

```cpp
const dg::NodeId form = window->create(dg::NodeKind::kBox, root).value();  // 任意位置建好
const dg::NodeId name = window->create(dg::NodeKind::kTextField, form).value();
dg::DialogOptions d;
d.title = "Rename";
d.body = "New name:";
d.buttons = {"OK", "Cancel"};
d.cancel_button = 1;
d.content = form;
const std::int32_t id = window->open_dialog(d).value();
window->set_dialog_button_disabled(id, 0, true);   // 名字为空时禁用 OK
```

- `content` 是本窗口一个 live 的 `create()` 节点（不能是窗口根），打开时被挪进对话框
  正文：正文文字下方、按钮上方，宽度等于正文宽度（内容按 stretch 撑满），高度取内容的
  自然高度，面板随之加高。
- **关闭后的去向**：内容**脱离原父节点、保持存活**（`DialogOptions` 头文件的约定：
  "detached again (alive, parentless)"）——停放在窗口里一个不可见、禁用的停放盒里，
  文字/勾选等状态都保留；不会自动放回原父节点。要放回用 `Window::move(form, parent)`，
  也可以下次 `open_dialog()` 再用，或 `remove()`。打开期间删除内容是允许的。
- 焦点陷阱包含内容里的控件：Tab/Shift+Tab 按树序在内容控件和按钮之间循环。
  焦点在内容里时，键盘、文字输入和指针都照常交给控件（编辑、勾选、点击）；
  Tab 和 Escape 仍归对话框；**Enter 触发默认按钮**（焦点在按钮上时触发该按钮），
  但焦点在 `TextArea` 里时 Enter 是换行。Escape 报 `cancel_button`。
- `Window::set_dialog_button_disabled(id, button, disabled)`：被禁用的按钮变暗、
  Tab 跳过，点击和 Enter（作为默认按钮时）都不会触发；对话框未打开/id 不符为
  `kUnknownId`，按钮越界为 `kValueOutOfRange`。
- 对话框打开期间不路由应用快捷键。
- 示例：`examples/29_enterprise` 的「新建 / 编辑资产」和「重命名」表单（文本框加下拉框；名称为空时
  禁用确定按钮）。它每次打开都新建表单，对话框结束后自己 `remove()` 掉停放的内容。

其它示例：`examples/22_dropdown_menu`、`examples/23_menu_tooltip_dialog`、`examples/25_showcase`。

下面的「直接使用底层」讲 `PopupHost` 和库组件本身。

## 这个子系统解决什么问题

下拉菜单、右键菜单、tooltip、对话框都需要"在某个 widget 附近浮现一块内容，
点击外部或按 Escape 时消失"这套通用行为。`PopupHost` 只提供**容器和摆放**
这两件事——它不知道你的下拉菜单有几个选项、菜单该长什么样。往容器里放什么，
由它上面一层的**库组件**（`include/drawgui/ui/`：`dg::Menu`、`dg::Dropdown`、
`dg::Tooltip`、`dg::Dialog`）负责；需要组件做不到的形态时，也仍然可以直接在
`PopupHandle::content_root` 下面自己搭普通 widget 树。

## 直接使用底层

直接使用 `PopupHost` 和 `include/drawgui/ui/` 组件时，下面这些事要自己做：

- 每次调用都传入 `UiContext`；把指针/键盘事件按顺序先交给打开着的组件
  （见「组件打开期间的事件路由」）。
- `dg::Tooltip` 要每帧 `tick()`，并用 `next_deadline()` 决定 `pump()` 等多久。
- 关闭后 `destroy()` 回收节点；对话框的焦点作用域、焦点环由你维护。

改完要遵守 [`app.md` 的逃生舱约定](app.md#逃生舱约定)：只改运行时不组合的东西，改完调用 `Window::invalidate(node)`。

### 当前真相：overlay-only

**popup 完全是 overlay，不会创建第二个 OS 窗口。** 早先的 native-OS 弹出窗口
分支（`SDL_CreatePopupWindow`）已删除：摆放逻辑从来只按父窗口的可绘制尺寸适配，
headless 测试用的 dummy 驱动也驱动不了它。旧文档里的 `PlatformCaps`、
`PopupWindowKind`、`WindowManager::open_popup()`/`close_popup()`/
`platform_caps()` 都已不存在。

overlay 容器是宿主窗口 `RenderTree::root()` 的**直接子节点**：

- 祖先的 clip/opacity **影响不到**弹层内容（它不在任何祖先的子树下）。
- 但**弹层坐标是窗口绝对坐标**，与触发它的 anchor 所在的祖先链完全无关——
  弹层不会继承 anchor 父级的滚动偏移之类的东西，摆放逻辑直接算绝对坐标。

### 核心类型

```cpp
struct PopupFlags {
  bool dismiss_on_click_outside = true;
  bool dismiss_on_escape = true;
};

struct PopupHandle {
  bool open = false;
  RenderTree* tree = nullptr;      // 弹层挂在哪棵树上
  WindowId window;                 // 弹层挂在哪个窗口上
  NodeId content_root;             // 内容的挂载点（局部坐标原点 (0,0)）
  PixelRect content_bounds;        // 摆放结果（窗口物理像素坐标）
  PopupFlags flags;                // show() 时捕获的 flags
};

class PopupHost {
 public:
  explicit PopupHost(WindowManager& windows);
  Expected<PopupHandle, WindowError> show(WindowId parent_window, RenderTree& parent_tree,
                                          const PixelRect& anchor_rect, PixelSize content_size,
                                          PopupPlacement preferred, PopupFlags flags);
  void close(PopupHandle& handle);
  bool handle_pointer(PopupHandle& handle, WindowId event_window, const PointerEvent& event);
  bool handle_key(PopupHandle& handle, WindowId event_window, const KeyEvent& event);
};
```

（`include/drawgui/window/popup_host.h`）

`handle_pointer()`/`handle_key()` 不接受 `PopupFlags` 参数——flags 在 `show()`
时已存进 `handle.flags`。

**一个 `PopupHost` 可以同时持有任意多个打开的 `PopupHandle`**，每个 handle
自足：`tree`/`window`/`content_root`/`content_bounds`/`flags` 都存在 handle
自己身上，`PopupHost` 不保存跨 handle 的状态（`popup_host.h` 的注释
记录了把 `tree` 挪到 `PopupHost` 上曾导致第二个弹层让第一个的 `close()` 失效）。

### 库组件：Menu / Dropdown / Tooltip / Dialog

头文件在 `include/drawgui/ui/`，实现在 `src/ui/`（所有绘制/节点形状集中在
`src/ui/ui_visuals.cpp`）。它们**不是新的 `WidgetKind`**：组件自己调用
`PopupHost::show()`、在 `content_root` 下建节点、把行/按钮作为普通 `kButton`
注册进宿主的 `WidgetSet`，关闭时 `PopupHost::destroy()`——节点数回到基线。
没有回调：宿主循环把事件交给组件，读回带类型的结果。

#### `UiContext`：每次调用传入的引用包

```cpp
struct UiContext {
  PopupHost& popups;
  WindowId window;
  RenderTree& tree;
  NodeSideTables tables;           // widgets/theme_bindings/focus/animation/action_scopes/interaction
  const FontCatalog* fonts = nullptr;  // 文字实测宽度；Dropdown 提交选择也需要它
  FontId font;
  PopupStyle style;                // 颜色/尺寸
};
PopupStyle popup_style_from_theme(const Theme& theme, ThemeVariant variant);
```

组件**从不保存** `UiContext`——每次 `handle_*()` 都要传。通常在宿主里写一个
`dg::UiContext ui()` 现组装（运行时自己的写法见 `src/app/components/dispatch.cpp` 的 `ui()`）。
`PopupStyle` 由已有 token 填充（surface/on-surface/border/primary*/on-disabled/
focus-ring、`radius.md`、`space.md`、对话框遮罩 `color.scrim`）。

#### `dg::Menu`：右键菜单 / 选项列表

```cpp
MenuSpec spec;
spec.items = {menu_item("复制"), menu_item("粘贴", /*disabled=*/true), menu_separator(),
              menu_check_item("自动换行", true)};
spec.min_width = 140;
menu.open(ctx, PixelRect{x, y, 0, 0}, spec, /*restore_focus=*/focus.current());

MenuResult r = menu.handle_pointer(ctx, event);   // 或 handle_key / handle_text
if (r.event == MenuEvent::kActivated) { use(r.index); }
if (r.closed()) { refresh_host_focus_ring(); }
```

- 高亮项就是 `Focus::current()`（菜单自己的焦点作用域里）；高亮行用
  `PopupStyle::item_hover` 填充，菜单不画焦点环。
- 键盘：Up/Down（环绕）、Home/End、Enter 激活、Escape 关闭、Tab 被吞掉；
  `handle_text()` 做首字母 type-ahead。分隔线没有控件、禁用项带
  `NodeStyle::disabled`，`focus_order()` 本来就跳过它们。
- 指针：移动到可用项上即高亮；在可用项上 kDown 即激活；点外部（或其它窗口）关闭；
  点禁用项/分隔线什么也不做。
- 关闭（激活、Escape、点外部、`close()`）时：退出作用域、`destroy()` 浮层、
  焦点还给 `restore_focus`。结果 `kActivated`/`kDismissed` 时菜单已经关好了。

#### `dg::Dropdown`：`kDropdown` + `Menu`

```cpp
dg::Dropdown dropdown{anchor_node};             // anchor 是已经 attach 好的 kDropdown
DropdownResult r = dropdown.handle_key(ctx, event);   // 关闭时：锚点有焦点 + Down/Enter 打开
DropdownResult r = dropdown.handle_pointer(ctx, event); // 关闭时：主键点锚点打开
if (r.event == DropdownEvent::kChanged) { on_change(*r.selected, r.previous); }
```

打开时：选项来自 `WidgetSet::dropdown_options()`，菜单至少和锚点一样宽，当前
选中项打勾并作为初始高亮。选中不同项时调用 `dropdown_select()`（刷新锚点标签）
并返回 `kChanged`；Escape/点外部/选中同一项返回 `kClosed`。焦点总是回到锚点。
宿主也可以自己判断触发条件后直接调用 `open(ctx)`。

#### `dg::Tooltip`：悬停延迟气泡

```cpp
dg::Tooltip tooltip{/*delay_ms=*/400};
tooltip.bind(help_button, "提示文字");
// 每次 pump 之后：
tooltip.tick(ctx, interaction.hovered(), steady_anim_time());   // 测试里传任意 AnimTime
// 每个指针事件（不消费事件，宿主照常分发）：
tooltip.handle_pointer(ctx, event);
// pump 超时：tooltip.next_deadline() 有值时短轮询
```

计时就是 `HoverTimer`；目标被连续悬停 `delay_ms` 后在其下方弹出（放不下就翻到
上方/夹紧）。离开目标即销毁；按下/松开/滚轮/离开窗口也销毁，并且在指针离开该
目标之前**不会再弹出**。绑定是组件自己持有的 `(NodeId, text)` 列表，不是侧表——
节点被删后 NodeId 的 generation 不同，永远不会再匹配。

#### `dg::Dialog`：模态对话框

```cpp
DialogSpec spec;
spec.title = "删除文件？";
spec.body = "此操作不能撤销。";
spec.buttons = {"取消", "删除"};
spec.default_button = 1;   // Enter（焦点不在按钮上时）激活它，打开时聚焦它，强调色
spec.cancel_button = 0;    // Escape 报告它
dialog.open(ctx, spec, /*restore_focus=*/focus.current());

DialogResult r = dialog.handle_key(ctx, event);
if (r.event == DialogEvent::kClosed) { act_on(r.button); }   // nullopt = 无取消按钮时的 Escape
if (r.event == DialogEvent::kIgnored && dialog.is_open()) { /* 交给 body 里聚焦的控件 */ }
```

- 覆盖整个窗口（`tree.viewport()`）：遮罩吞掉所有点击，面板居中。指针按下只在
  命中面板内可聚焦、未禁用的控件时移动焦点；按在遮罩、面板空白或禁用按钮上焦点不变。
- 焦点陷阱就是现成的模态作用域：`enter_scope(panel, true)` + `set_guarded()`；
  Tab/Shift-Tab 在面板内循环；对话框自带一个挂在面板下的焦点环。
- `adopt(ctx, node)` 把调用方的节点（仅渲染树一侧）挂进 `body()`，`close()` 在销毁
  浮层前把它挂回原父节点——运行时的表单对话框就是这样把布局好的内容放进来的；
  `set_button_disabled(ctx, i, disabled)` 禁用按钮（变暗、Tab 跳过、点击和 Enter 都不触发）。
- `body()` 是留给调用方的区域（`DialogSpec::body_height` 预留高度，
  `body_content_top()` 是正文文字下方的起点）：自己在里面加控件后，用
  `focus.set_guarded()` + `dialog.refresh_ring(ctx)` 聚焦它。指针 kDown 的
  结果带 `target`，方便把点击转发给 body 里的控件（比如文本框定位光标）。

#### 组件打开期间的事件路由

示例的约定：菜单/下拉/对话框打开时，**所有**指针和键盘事件先交给它，宿主场景
不再分发；tooltip 例外，它只旁听。组件返回 `closed()`/`kClosed` 后刷新宿主自己
的焦点环（焦点已被还给锚点）。

#### 自己搭浮层（组件覆盖不到的形态）

直接用 `PopupHost::show()` 拿 `content_root`，在下面 `RenderTree::add_child()`
+ `WidgetSet::attach()`；`enter_scope(content_root)` 限制 Tab；关闭时先
`exit_scope()` 再 `destroy()`。`src/ui/menu.cpp` 本身就是这套流程最短的参考实现。

### `resolve_popup_placement()`：双轴 flip + clamp

```cpp
PixelRect resolve_popup_placement(const PixelRect& anchor, PixelSize content,
                                  PopupPlacement preferred, PixelSize parent_size);
```

- **垂直**：优先 `preferred`（below/above），放不下就翻到另一侧；翻过去
  还是放不下，就 clamp 进 `[0, parent_size.height - content.height]`；如果
  内容本身比父窗口还高，对齐到顶部。
- **水平**：优先与 `anchor` 左边对齐，放不下就翻到与 `anchor` 右边对齐；
  还放不下就 clamp/左对齐。
- `content` 的尺寸永远不变，只有位置会挪动。
- 适配测试的对象是**父窗口的可绘制尺寸**，不是屏幕尺寸。

### `close()` 隐藏，`destroy()` 回收

```cpp
void PopupHost::close(PopupHandle& handle);
void PopupHost::destroy(PopupHandle& handle, const NodeSideTables& tables);
```

`close()` 只是把弹层容器**裁成空矩形**（`Overflow::kClip` 在空盒子上），
让它从绘制和命中测试里消失，但**不删除任何节点**：`content_root` 和你持有
的内容 node id 都仍然有效，适合想保留这些 id 的场景。代价是节点一直占着，
反复开关会每次留下一小撮死节点。

`destroy()` 真正回收：它调用 `dg::remove_subtree()` 的 RenderTree 重载
（`include/drawgui/render/node_lifecycle.h`）删除 `content_root` 整棵子树，
清理六张侧表（弹层里持有焦点的节点会被 blur，控件/主题/动画/交互状态全部
清掉），并重绘它覆盖过的区域；随后把 handle 置为 `open = false`、
`tree = nullptr`、`content_root = {}`。对已 `close()` 的 handle 同样有效，
重复调用是空操作。

注意：
- `destroy()` 之后 `content_root` 和其下所有 id **全部失效**（槽位可能被
  后续 `add_child()` 复用，旧 id 因 generation 不同不会被误认），要再显示
  请重新 `show()` 拿新 id。
- `tables` 必须是 `handle.tree` 所在窗口**自己的**侧表，传错窗口会清错表。
- 每次打开都重建内容的弹层（下拉、右键菜单、tooltip）应在关闭时用
  `destroy()`，节点数不再随开关次数增长。

### 弹层打开期间可以 `layout()`

调用方可以在任意数量的弹层打开期间自由调用 `layout()`/`layout_full()`
（`popup_host.h`）。原因是 `LayoutTree` 的布局只从 dirty 节点出发、沿
`LayoutNode::children` 递归，从不按 `RenderTree` 的 index 范围判断可达性；
overlay 容器是用 `RenderTree::add_child()` 加的，从不出现在任何
`LayoutNode::children` 里，所以布局永远走不到它。

这条安全性依赖"overlay 容器永远通过 `RenderTree::add_child()` 添加，绝不通过
`LayoutTree::add_child()`"。如果以后换成后者，下一次 `layout()` 就会悄悄覆盖
`PopupHost` 用 `apply_bounds()` 摆好的位置——目前没有机制阻止这种改动。

### 嵌套 popup：只支持"对话框之上一层下拉列表或菜单"

`PopupHost` 不维护"父弹层是谁"，后 `show()` 的容器挂在渲染树根的末尾，所以
自然画在前一个之上。`Focus` 仍是单一活跃作用域（不是栈），`dg::Menu` 自己补上
这一层：`open()` 记下当时的作用域（比如对话框的模态作用域），`close()` 退出菜单
作用域后再把它恢复。运行时用到两种叠法——对话框内容里的 `Dropdown`，以及对话框
打开期间的 `open_menu()` 菜单；路由顺序是：对话框之上的菜单 → 打开的下拉列表 → 对话框
→（无对话框时的）菜单。子菜单等更深的嵌套仍不支持。

### Dismissal 路由细节

- `handle_pointer()`：一次落在 `handle.window` 上、坐标在 `content_bounds`
  之外的 `kDown` 会关闭弹层；落在**其它**窗口上的 `kDown` 一定算"外部"
  （按 `event_window` 比较，不只比较坐标）。
- `handle_key()`：Escape 只有当事件的窗口等于 `handle.window` 时才生效。
- 两者都遵守 `handle.flags` 里的 `dismiss_on_click_outside`/`dismiss_on_escape`。

### 必须知道的两条组装规则（自己搭浮层时；库组件已经遵守）

1. **焦点环必须挂在 `content_root` 下**，不是宿主窗口的根——见
   [input.md](input.md) 的焦点环一节（`dg::Dialog` 的环挂在面板下；`dg::Menu`
   不画环，用高亮行）。
2. **弹层内容必须 attach 进宿主的 `WidgetSet`**（同一个实例），否则 Tab 遍历
   （`focus_order()` 读的是 `WidgetSet`）根本看不到这些内容——它们会存在于
   `RenderTree` 里、可以被点击命中，但永远不会被 Tab 到达。

### 组件之外的东西仍是应用层的

组件只覆盖"文字行菜单 / 文字气泡 / 标题+正文+按钮对话框"这几种形态。图标菜单项、
子菜单、富内容 tooltip、非模态面板等，仍然在 `content_root` 下自己搭（见上文"自己搭浮层"）。
带控件的对话框不用自己搭：`dg::Dialog::body()`/`adopt()` 可以放自己的控件，运行时的
`open_dialog()` 通过 `DialogOptions::content` 提供表单对话框。

## 边界与坑

- overlay-only：不存在跑出父窗口边界的能力，也不存在第二个 OS 窗口。
- `close()` 不回收节点，反复开关有累积成本。
- 叠放只有「对话框之上一层下拉列表或菜单」，没有子菜单。
- 焦点环必须每个弹层单独一份实例。
- 弹层内容必须 attach 进宿主的 `WidgetSet` 才能被 Tab 到达。
- 更多边界见 `doc/limitations.md`。

## 相关 example

- `examples/14_popup` — 直接使用 `PopupHost` 的最基础 overlay 弹层：摆放、dismissal。
- `examples/21_focus` — 弹层里的焦点作用域和独立焦点环（直接使用底层）。
- `examples/22_dropdown_menu` — 运行时的 `kDropdown`（底层是 `dg::Dropdown`）。
- `examples/23_menu_tooltip_dialog` — 运行时的 `open_menu()`/`set_tooltip()`/`open_dialog()`。
- `examples/25_showcase` — 运行时里的下拉框、右键菜单、CJK tooltip、确认对话框。
- `examples/29_enterprise` — 运行时的菜单栏（快捷键列、命令号、勾选项）、行右键菜单、表单对话框、
  对话框里的下拉框、关闭确认对话框。
- `tests/unit/test_ui_components.cpp` — 库组件（直接使用）的行为单测。
- `examples/28_c_components` — 纯 C：`dg_menu_open()`（锚定与右键）、`DG_NODE_TYPE_DROPDOWN`、
  `dg_node_set_tooltip()`、`dg_dialog_open()`，全部走轮询事件（`tests/unit/test_abi_phase_b.cpp` 是对应单测）。

## 从 C 使用

C ABI（0.2 phase B）把这四个组件包成了不需要回调的形状：事件泵（`dg_wait_events()`）
替你调 `handle_pointer()`/`handle_key()`/`handle_text()`/`tick()`，组件给出的类型化结果变成
`dg_event`——`DG_EVENT_MENU_ACTIVATE`（`x` = 项，`y` = 菜单 id）/`_DISMISS`、
`DG_EVENT_VALUE_CHANGED`（下拉框，`x` 新、`y` 旧）、`DG_EVENT_DIALOG_RESULT`（`x` = 按钮，-1 =
无取消按钮时的 Escape）。tooltip 的延迟（`dg_app_set_tooltip_delay()`）会缩短
`dg_wait_events()` 的等待，所以 `-1` 永久等待也能按时弹出。完整说明见
`doc/reference/c-abi.md` 第 5 节「组件」，边界见 `doc/limitations.md` C 节。
