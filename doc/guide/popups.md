# 弹层（Popups）：下拉菜单、菜单、tooltip、对话框

> 这块刚经历过一次大重构（commit `48c9cd3` 删除了整个 native-OS-popup 分支，
> commit `890a59a` 重写了 `PopupHandle`）。**本篇以当前代码为准。**

## 这个子系统解决什么问题

下拉菜单、右键菜单、tooltip、对话框都需要"在某个 widget 附近浮现一块内容，
点击外部或按 Escape 时消失"这套通用行为。`PopupHost` 只提供**容器和摆放**
这两件事——它不知道你的下拉菜单有几个选项、菜单该长什么样。往容器里放什么，
由它上面一层的**库组件**（`include/drawgui/ui/`：`dg::Menu`、`dg::Dropdown`、
`dg::Tooltip`、`dg::Dialog`）负责；需要组件做不到的形态时，也仍然可以直接在
`PopupHandle::content_root` 下面自己搭普通 widget 树。

## 当前真相：overlay-only

**popup 完全是 overlay，没有第二个 OS 窗口这条路了。** 之前存在过一个
native-OS 弹出窗口分支（`SDL_CreatePopupWindow`，能让弹层内容跑出父窗口的
边界），已经在 commit `48c9cd3` 整体删除——理由是这个能力从未被任何摆放逻辑
真正用到（`resolve_popup_placement()` 的适配测试对象一直是父窗口的可绘制
尺寸，从来不是屏幕尺寸），headless 测试用的 dummy video driver 根本无法
驱动这个分支，而且这一轮发现的所有缺陷（字体继承、expose 重绘、焦点环
溢出）全都只存在于那个分支里。**如果你在旧文档里看到 `PlatformCaps`、
`PopupWindowKind`、`WindowManager::open_popup()`/`close_popup()`/
`platform_caps()`，这些类型/方法已经不存在了。**

overlay 容器是宿主窗口 `RenderTree::root()` 的**直接子节点**：

- 祖先的 clip/opacity **影响不到**弹层内容（它不在任何祖先的子树下）。
- 但**弹层坐标是窗口绝对坐标**，与触发它的 anchor 所在的祖先链完全无关——
  弹层不会继承 anchor 父级的滚动偏移之类的东西，摆放逻辑直接算绝对坐标。

## 核心类型

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

（`include/drawgui/window/popup_host.h:89-229`）

**`handle_pointer()`/`handle_key()` 不再接受 `PopupFlags` 参数**——flags
在 `show()` 时就被捕获进 `handle.flags` 了，调用方不需要在每次事件处理时
重新传一遍。

**一个 `PopupHost` 可以同时持有任意多个打开的 `PopupHandle`**，每个 handle
自足：`tree`/`window`/`content_root`/`content_bounds`/`flags` 都存在 handle
自己身上，不依赖 `PopupHost` 保存任何跨 handle 的状态（`popup_host.h:108-155`
的长注释记录了这一点曾经踩过的坑：`tree` 字段一度被搬到 `PopupHost` 自身，
结果第二个弹层打开时覆盖了第一个 handle 的 tree 指针，第一个 handle 的
`close()` 就悄悄变成 no-op）。

## 库组件：Menu / Dropdown / Tooltip / Dialog

头文件在 `include/drawgui/ui/`，实现在 `src/ui/`（所有绘制/节点形状集中在
`src/ui/ui_visuals.cpp`）。它们**不是新的 `WidgetKind`**：组件自己调用
`PopupHost::show()`、在 `content_root` 下建节点、把行/按钮作为普通 `kButton`
注册进宿主的 `WidgetSet`，关闭时 `PopupHost::destroy()`——节点数回到基线。
没有回调：宿主循环把事件交给组件，读回带类型的结果。

### `UiContext`：每次调用传入的引用包

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
`dg::UiContext ui()` 现组装（见 `examples/22_dropdown_menu/dropdown_window.cpp`）。
`PopupStyle` 由已有 token 填充（surface/on-surface/border/primary*/on-disabled/
focus-ring、`radius.md`、`space.md`）；遮罩色 `scrim` 目前没有 token，直接改字段。

### `dg::Menu`：右键菜单 / 选项列表

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

### `dg::Dropdown`：`kDropdown` + `Menu`

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

### `dg::Tooltip`：悬停延迟气泡

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

### `dg::Dialog`：模态对话框

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

- 覆盖整个窗口（`tree.viewport()`）：遮罩吞掉所有点击，面板居中。
- 焦点陷阱就是现成的模态作用域：`enter_scope(panel, true)` + `set_guarded()`；
  Tab/Shift-Tab 在面板内循环；对话框自带一个挂在面板下的焦点环。
- `body()` 是留给调用方的区域（`DialogSpec::body_height` 预留高度，
  `body_content_top()` 是正文文字下方的起点）：自己在里面加控件后，用
  `focus.set_guarded()` + `dialog.refresh_ring(ctx)` 聚焦它。指针 kDown 的
  结果带 `target`，方便把点击转发给 body 里的控件（比如文本框定位光标）。

### 组件打开期间的事件路由

示例的约定：菜单/下拉/对话框打开时，**所有**指针和键盘事件先交给它，宿主场景
不再分发；tooltip 例外，它只旁听。组件返回 `closed()`/`kClosed` 后刷新宿主自己
的焦点环（焦点已被还给锚点）。

### 自己搭浮层（组件覆盖不到的形态）

直接用 `PopupHost::show()` 拿 `content_root`，在下面 `RenderTree::add_child()`
+ `WidgetSet::attach()`；`enter_scope(content_root)` 限制 Tab；关闭时先
`exit_scope()` 再 `destroy()`。`src/ui/menu.cpp` 本身就是这套流程最短的参考实现。

## `resolve_popup_placement()`：双轴 flip + clamp

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
- 适配测试的对象是**父窗口的可绘制尺寸**，不是屏幕尺寸——这也是为什么
  native-OS 分支被删除时"逃出父窗口边界"这个能力没有被使用过（见上文）。

## `close()` 隐藏，`destroy()` 回收

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

## "打开期间不能 layout()"——这条禁令已被证伪并删除

旧版注释曾经写"overlay 弹层打开期间不得调用 `layout()`/`layout_full()`"，
理由是"nodes 数组只覆盖 `LayoutTree` 自己创建的节点"。**这个理由是错的**，
已经被现在的注释纠正（`popup_host.h:26-46`）：

- `LayoutTree::add_child()` 会做 `nodes.resize(id.index + 1)`，所以一旦有
  一个只经 `RenderTree::add_child()` 添加的节点（比如 overlay 容器）存在于
  更高的 index，`nodes` 向量本来就会出现"空洞"，这不是问题所在。
- 真正的安全性来自：`LayoutTree::Impl::run()` 只从 `dirty` 出发，只沿着
  `LayoutNode::children` 递归——从来不靠 `RenderTree::node_count()` 或任何
  基于 index 的边界判断可达性。overlay 容器是通过 `RenderTree::add_child()`
  加入的，从未出现在任何 `LayoutNode::children` 列表里，所以任何一次 dirty
  walk（无论是增量的还是 `layout_full()` 那种"全标脏再从根走"的）都永远
  到达不了它。它对布局系统是完全惰性的。

**结论：调用方现在可以在任意数量的弹层打开期间自由调用 `layout()`/
`layout_full()`。** 一个 `PopupHost` 也可以同时持有任意数量并发打开的
`PopupHandle`，互不干扰。

**留给未来维护者的条件性约束**：这条安全性依赖"overlay 容器永远通过
`RenderTree::add_child()` 添加，绝不通过 `LayoutTree::add_child()`"这个
事实。如果未来有改动把 overlay 创建路径换成后者，容器就会变得能被 dirty
walk 到达，下一次 `layout()`/`layout_full()` 就会悄悄覆盖掉 `PopupHost`
通过 `apply_bounds()` 摆放的位置——这里没有任何机制强制阻止这种改动，
只是先记录在案。

## 不支持嵌套 popup

一个 popup 内部再打开第二个 popup（比如子菜单）目前不被支持。`PopupHost`
从不维护"父弹层是谁"这种关系，`Focus` 的单一活跃作用域（不是栈）设计也
建立在"从不嵌套"这个前提上（见 `doc/guide/input.md`）。

## Dismissal 路由细节

- `handle_pointer()`：一次落在 `handle.window` 上、坐标在 `content_bounds`
  之外的 `kDown` 会关闭弹层。落在**其它**窗口上的 `kDown` 一定算"外部"——
  这条现在是真的按 `event_window` 比较实现的，不是只比较坐标（旧实现曾经
  用 `(void)event_window` 丢弃窗口 id，只比较坐标，导致坐标碰巧落在
  `content_bounds` 内的其它窗口点击被误判为"内部点击"）。
- `handle_key()`：Escape 只有当事件的窗口等于 `handle.window` 时才生效
  （同样，旧实现曾经对任意窗口的 Escape 都关闭弹层，现已修正）。
- 两者都遵守 `handle.flags` 里的 `dismiss_on_click_outside`/`dismiss_on_escape`。

## 必须知道的两条组装规则（自己搭浮层时；库组件已经遵守）

1. **焦点环必须挂在 `content_root` 下**，不是宿主窗口的根——见
   `doc/guide/input.md` 的焦点环一节（`dg::Dialog` 的环挂在面板下；`dg::Menu`
   不画环，用高亮行）。
2. **弹层内容必须 attach 进宿主的 `WidgetSet`**（同一个 `scene_.widgets`
   实例），否则 Tab 遍历（`focus_order()` 读的是 `WidgetSet`）根本看不到
   这些内容——它们会存在于 `RenderTree` 里、可以被点击命中，但永远不会
   被 Tab 到达。

## 组件之外的东西仍是应用层的

组件只覆盖"文字行菜单 / 文字气泡 / 标题+正文+按钮对话框"这几种形态。图标菜单项、
子菜单、富内容 tooltip、非模态面板等，仍然在 `content_root` 下自己搭（见上文
"自己搭浮层"）。`examples/23_menu_tooltip_dialog` 的原生（第二个 OS 窗口）对话框
分支、`examples/25_showcase` 带输入框的资料对话框也仍是手搭的。

## 边界与坑

- overlay-only：不存在跑出父窗口边界的能力，也不存在第二个 OS 窗口。
- `close()` 不回收节点，反复开关有累积成本。
- 不支持嵌套 popup（组件也一样：菜单里不能开子菜单，对话框里不能再开下拉）。
- 焦点环必须每个弹层单独一份实例。
- 弹层内容必须 attach 进宿主的 `WidgetSet` 才能被 Tab 到达。
- 更多边界见 `doc/limitations.md`。

## 相关 example

- `examples/14_popup` — 最基础的 overlay 弹层：摆放、dismissal。
- `examples/22_dropdown_menu` — `dg::Dropdown`。
- `examples/23_menu_tooltip_dialog` — `dg::Menu`（右键菜单）、`dg::Tooltip`、`dg::Dialog`（overlay 分支）。
- `examples/25_showcase` — 排序下拉、右键菜单、CJK tooltip 都用组件。
- `tests/unit/test_ui_components.cpp` — 组件行为的单测。
