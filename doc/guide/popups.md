# 弹层（Popups）：下拉菜单、菜单、tooltip、对话框

> 这块刚经历过一次大重构（commit `48c9cd3` 删除了整个 native-OS-popup 分支，
> commit `890a59a` 重写了 `PopupHandle`）。**本篇以当前代码为准。**

## 这个子系统解决什么问题

下拉菜单、右键菜单、tooltip、对话框都需要"在某个 widget 附近浮现一块内容，
点击外部或按 Escape 时消失"这套通用行为。`PopupHost` 只提供**容器和摆放**
这两件事——它不知道你的下拉菜单有几个选项、菜单该长什么样，那些都是调用方
自己在 `PopupHandle::content_root` 下面搭建的普通 widget 树。

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

## 打开一个下拉菜单：完整流程

以下改编自 `examples/22_dropdown_menu/dropdown_window.cpp`。

### 1. 打开

```cpp
void Runner::open_dropdown() {
  const dg::PixelRect anchor = scene_.tree.render().absolute_bounds(scene_.handles.dropdown);
  const dg::PixelSize size = dropdown_options::size_for(option_count, anchor.width);

  const auto shown = popup_host_.show(host_window_, scene_.tree.render(), anchor, size,
                                      dg::PopupPlacement::kBelow, dg::PopupFlags{});
  if (!shown) { /* 打开失败，比如父窗口尺寸查询失败 */ return; }
  popup_ = shown.value();

  // 每次开弹层都要一个全新的 FocusRing —— 见 doc/guide/input.md 的
  // "焦点环:内缩,且每个弹层需要自己的实例"一节。
  popup_ring_ = dg::FocusRing{};

  // 选项列表是应用层自己搭的普通 widget 树,PopupHost 从不知道"下拉菜单"
  // 这个概念 —— 见 dropdown_options.cpp。
  rows_ = dropdown_options::build(scene_.tree.render(), scene_.widgets,
                                  popup_->content_root, kOptions, size.width,
                                  ui_font, 15.0F, selected_index, row_color);

  // 把 Tab 限制在弹层内部,焦点挂到弹层自己的 content_root 子树上
  scene_.focus.enter_scope(popup_->content_root);
  scene_.focus.set(rows_[start_index]);
  refresh_ring();
}
```

（`dropdown_window.cpp:224-256`）

### 2. 事件路由：谁先看到指针/键盘事件

```cpp
void Runner::handle_pointer(const dg::PointerEvent& event) {
  if (popup_.has_value() && popup_->open) {
    if (popup_host_.handle_pointer(*popup_, event.window, event)) {
      close_dropdown(std::nullopt);   // 点击外部/其它 dismissal 关闭了它
      return;
    }
    // 没被关闭,说明点在弹层内部 —— 应用层自己算命中了哪一行
    ...
    return;
  }
  // 弹层没打开时,走宿主窗口自己的普通指针分发
}
```

（`dropdown_window.cpp:276-307`）

`handle_pointer()`/`handle_key()` **只负责判断这次事件是否触发了 dismissal**
（点击外部、Escape），返回 `true` 表示已经把 `handle` 关掉了。如果事件落
在弹层内部但不是 dismissal，函数返回 `false`，调用方需要自己接着处理——
比如判断点在哪一行、要不要选中它。**`PopupHost` 完全不知道你的内容里有
"行"这个概念**。

### 3. 关闭

```cpp
void Runner::close_dropdown(std::optional<int> select_index) {
  if (select_index.has_value()) { /* 应用层自己的选中逻辑 */ }
  scene_.focus.exit_scope(scene_.tree.render());   // 先退出焦点作用域
  popup_host_.close(*popup_);                       // 再关闭弹层容器
  popup_.reset();
  rows_.clear();
  // 关闭后把焦点还给触发它的 anchor,不留下"什么都没聚焦"的状态
  apply_focus_change(scene_, scene_.handles.dropdown);
}
```

（`dropdown_window.cpp:258-274`）

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

## `close()` 不回收节点

```cpp
void PopupHost::close(PopupHandle& handle);
```

关闭只是把弹层容器**裁成空矩形**（`Overflow::kClip` 在空盒子上），这会让
它从绘制和命中测试里消失，但**不删除任何节点**。

这是一个**选择，不是被迫的**：`RenderTree::remove_child()`
（`include/drawgui/render/render_tree.h:582`）从 8-5 起就已经存在，是真正
的节点移除。选它而不是裁空的原因是：真正移除会让 `content_root` 和调用方
持有的每一个内容 node id 全部失效，还要求调用方在移除后手动跑
`on_node_removed()` 涉及的六张侧表清理（`src/render/node_lifecycle.cpp`）。
裁空是这个类型给出的默认值，不是每个调用方都需要处理这件事的复杂度。

**代价被明确记录**：反复开关同一个弹层会每次留下一小撮死节点，就像重复
构建任何"从不删除"的 widget 一样。如果你的应用会高频开关同一类弹层
（比如每次悬停都弹一个 tooltip），这是需要考虑的累积成本。

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

## 必须知道的两条组装规则

1. **焦点环必须挂在 `content_root` 下**，不是宿主窗口的根——见上文
   "打开一个下拉菜单"第 1 步和 `doc/guide/input.md` 的焦点环一节。
2. **弹层内容必须 attach 进宿主的 `WidgetSet`**（同一个 `scene_.widgets`
   实例），否则 Tab 遍历（`focus_order()` 读的是 `WidgetSet`）根本看不到
   这些内容——它们会存在于 `RenderTree` 里、可以被点击命中，但永远不会
   被 Tab 到达。

## 选项列表是应用层自己搭的

**`PopupHost` 只提供容器和摆放，不提供"菜单/下拉框/tooltip"这些概念。**
每一行、每一个选项、hover 高亮、选中态——全部是调用方在 `content_root`
下用普通 `LayoutTree::add_child()` + `WidgetSet::attach()` 搭出来的，和
搭宿主窗口的其它 UI 没有任何区别。

必抄模板：
- `examples/22_dropdown_menu` — 单个下拉菜单：选项行、上下键高亮、Enter
  选中、点击外部/Escape 取消。
- `examples/23_menu_tooltip_dialog` — 同一套机制搭菜单、tooltip、对话框
  三种不同形态，能看出"容器相同、内容不同"这条边界具体落在哪里。

## 边界与坑

- overlay-only：不存在跑出父窗口边界的能力，也不存在第二个 OS 窗口。
- `close()` 不回收节点，反复开关有累积成本。
- 不支持嵌套 popup。
- 焦点环必须每个弹层单独一份实例。
- 弹层内容必须 attach 进宿主的 `WidgetSet` 才能被 Tab 到达。
- 更多边界见 `doc/limitations.md`。

## 相关 example

- `examples/14_popup` — 最基础的 overlay 弹层：摆放、dismissal。
- `examples/22_dropdown_menu` — 下拉菜单的完整流程。
- `examples/23_menu_tooltip_dialog` — 菜单/tooltip/对话框三种形态。
