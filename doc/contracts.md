# 约定（Contracts）

这份文档列出的，是**调用方必须遵守、但代码不会强制**的约定。这不是设计缺陷，是刻意的取舍：这个引擎的多数结构在运行期不做校验（不检查 resize 前后尺寸是否一致、不检查事件处理顺序、不检查 overlay 挂在哪棵树下），把校验成本留给调用方自己承担。省下来的是每帧、每个事件都要付的检查开销。

代价是：这些约定绝大多数只以头文件注释或某个 example 的写法存在，破坏它们不会报错、不会崩溃，只会在特定条件下表现为一个具体的视觉症状——第一帧黑屏、菜单点开没有文字、Tab 键怎么按都进不了弹出层。这些症状离“这是一个 bug”很远，离“我的代码写错了”也很远，排查成本很高。

**最可靠的做法：不要凭这份文档里的描述从零写主循环，直接照抄 `examples/` 里的循环结构**，尤其是 `examples/08` 之后涉及 resize、overlay、焦点的例子。这份文档的作用是让你在偏离 example 写法时知道自己在动哪条约定。

---

## 1. resize 之后必须用 `drawable_size()` 重建 surface，不能用 `WindowSpec` 里的旧尺寸

**做什么**：收到 `needs_repaint` 后，重新调用 `drawable_size()` 取得当前物理像素尺寸，并按这个尺寸重建/重新分配光栅 surface，再布局、绘制。

**不做会怎样**：窗口被拖拽缩放后，继续用创建窗口时的 `WindowSpec` 尺寸分配 surface，会导致新一帧按旧尺寸光栅化——要么画面被拉伸/裁切，要么在 `present()` 里因尺寸不匹配而越界写入。这正是 commit `8ff1f4e` 修复前 5 个 example 里实际发生过的问题。

**依据**：`include/drawgui/window/window_manager.h:373-378`（`needs_repaint` 字段注释：“a resize is not reported separately … the only correct response … is to ask for the drawable size again”）；修复提交 `8ff1f4e`。

**正确写法**：
```cpp
for (const WindowId id : frame.needs_repaint) {
  const PixelSize size = windows.drawable_size(id);   // 不要用 WindowSpec.width/height
  surface = rebuild_surface(size);
  layout_tree.layout_full();
  // ... 绘制 ...
}
```

---

## 2. 必须先处理 `needs_repaint`，再处理 `pointer`

**做什么**：同一次 `pump()` 返回的事件里，先完成 `needs_repaint` 触发的重新布局，再处理 `pointer` 事件。

**不做会怎样**：resize 改变了每个控件的位置，如果先处理同一批里的指针事件，会拿旧布局做命中测试——点击位置和实际显示的控件对不上，通常是点偏或点空。

**依据**：`include/drawgui/window/window_manager.h:384-386`。

**正确写法**：按 `WindowManager::pump()` 返回结构体里字段声明的顺序处理：`closed`/`close_requested` → `needs_repaint` → `pointer` → `key`/`text_input`/`text_editing`。

---

## 3. `repaint_full()` 之后必须无条件 `present()`，不能用 `present_if_damaged` 的守卫

**做什么**：调用 `repaint_full()` 之后的这一帧，直接调用 `present()`，不要用“有没有 damage”来决定要不要 present。

**不做会怎样**：`repaint_full()` 会清空累积的 damage（它自己的文档注释这么说），紧接着如果用“检查 damage 是否非空”的守卫来决定是否 present，会因为 damage 刚被清空而判定“无需 present”，于是第一帧完全没有画面——直到第一次真实的鼠标移动产生新的 damage，窗口才会恢复正常显示。这是 `35b7c54` 修复前的实际 bug，`tests/unit/test_first_frame_present.cpp` 是它的回归测试。

**依据**：`src/render/render_tree.cpp:413-418`（`repaint_full()` 用整个视口构造 damage 后立即 `retire_damage()`，清空累积 damage）；`tests/unit/test_first_frame_present.cpp`；提交 `35b7c54`、`a758cd3`。

**正确写法**：
```cpp
layout_tree.layout_full();
tree.repaint_full();     // 清空 damage，把整个视口标记为“已画”
paint_everything(canvas);
windows.present(id, image, full_rect);   // 无条件 present，不要查 damage 是否非空
```

---

## 4. 换行文本节点的尺寸必须先用 `Paragraph::build()` 测出来，节点不会自己测量

**做什么**：`TextStyle::wrap = true` 时，节点的高度不是布局引擎从文本内容反推出来的。必须在构造节点之前，先用 `dg::Paragraph::build()` 测出实际尺寸，再把测量结果写进节点的 box。

**不做会怎样**：不测量直接给一个随意高度，要么文字被裁切，要么留出大片空白——布局树不知道这段文字实际排出来是几行。

**依据**：`include/drawgui/render/render_tree.h:128-140`（“A wrapping node's HEIGHT is not derived here … that stays the caller's job … a box must know its own size before its content is resolved, never the other way around”）；`doc/guide/text.md`。

**正确写法**：
```cpp
auto paragraph = dg::Paragraph::build(fonts, text_style, max_width);
const dg::ParagraphMetrics measured = paragraph.value().metrics();   // 先测量
tree.set_local_bounds(node, {x, y, max_width, measured.height});      // 再定尺寸
```

---

## 5. overlay 内容必须 attach 进宿主自己的 `WidgetSet`，不能新建一个

**做什么**：`PopupHost::show()` 打开的浮层（下拉选项、菜单、tooltip 内容）里的控件，必须注册进**宿主窗口**已有的那个 `WidgetSet`，不能为浮层单独建一个。

**不做会怎样**：`dg::focus_order()` 只遍历一棵 `RenderTree` 对应的一个 `WidgetSet`。浮层里的行如果进了另一个 `WidgetSet`，Tab/上下键的焦点遍历根本看不到它们——键盘完全按不进浮层，只能用鼠标点。

**依据**：`examples/22_dropdown_menu/dropdown_options.h:22-26`（“the overlay appends into the HOST's own RenderTree, and dg::focus_order() walks that tree against ONE WidgetSet, so rows must land in the host's own table or Tab/Up/Down would never see them”）。

**正确写法**：`build()` 一类的浮层构造函数把宿主的 `WidgetSet&` 作为显式参数传入，不要在浮层内部 `WidgetSet{}`。

---

## 6. focus ring 必须挂在 popup 的 `content_root` 下，不能挂在宿主的 root 下

**做什么**：浮层内控件获得焦点时，焦点框（focus ring）节点要作为 `PopupHandle::content_root` 的子节点存在，而不是宿主窗口 root 的子节点。

**不做会怎样**：焦点框画在了宿主的坐标系/裁剪范围里，浮层关闭时容器被裁成空矩形，焦点框却还留在宿主树上，或者反过来焦点框跟着浮层的相对坐标却挂在错误的裁剪父节点下，出现位置错位或残留。

**依据**：commit `5e630ef`（“the focus ring belongs to the popup's container, not the host root”）；`include/drawgui/widget/focus.h`。

---

## 7. `FontCatalog` 必须在建任何文本节点之前设好

**做什么**：在创建第一个带文本的节点（或调用 `Paragraph::build()`）之前，`FontCatalog` 必须已经注册好要用的字体族。

**不做会怎样**：**完全静默**——`FontCatalog::holds(FontId)` 返回 false 时，`build_paragraph()`/`paragraph_runs` 直接返回空，不抛异常、不打日志，画面上该有文字的地方什么都没有。这是最难排查的一类问题，因为没有任何报错信号。

**依据**：`include/drawgui/render/render_tree.h:101-103`；`src/render/paragraph_build.cpp:45`（`if (text.text.empty() || !fonts.holds(text.font) || text.size <= 0.0F) return nullptr;`）；`src/render/paragraph_runs.cpp:9`。

**正确写法**：在事件循环、任何节点构造之前完成：
```cpp
FontCatalog fonts = FontCatalog::scan("/usr/share/fonts").value();
const FontId body_font = fonts.add("Noto Sans", /*bold=*/false).value();   // 先注册
// 之后才能构造带 TextStyle 的节点
```

---

## 8. 节点删除后必须跑 `dg::on_node_removed()`，否则六张侧表会留着悬空引用

**做什么**：从 `LayoutTree`/`RenderTree` 上摘除一个节点（或其子树）之后，必须调用 `dg::on_node_removed()`，让 `WidgetSet`、`ThemeBindings`、`Focus`、`AnimationEngine`、`ActionScopes`、`Interaction` 这六张按 `NodeId` 索引的表一起清理。

**不做会怎样**：节点本身没了，但六张侧表里仍留着指向这个已删 id 的记录——焦点系统可能仍认为一个已经不存在的节点持有焦点（`blur_if_any_of` 没被调用），动画引擎可能仍在为它跑插值，`WidgetSet`/`ThemeBindings` 的查找可能命中一个悬空 id。

**依据**：`src/render/node_lifecycle.cpp:23-47`，函数体逐一调用 `focus.blur_if_any_of(subtree)`、`widgets.forget()`、`theme_bindings.forget()`、`animation.cancel_all_for()`、`action_scopes.forget()`、`interaction.forget()`，正好六项。已核实与提交时一致。

**正确写法**：
```cpp
dg::on_node_removed(layout_tree, node_id, widgets, theme_bindings, focus, animation, action_scopes, interaction);
```
不要单独调用 `RenderTree::remove_child()`/`LayoutTree::remove_child()` 而不跟进这一步。

**移动不算删除**：`LayoutTree::move_child()`/`RenderTree::move_child()` 把子树挪到新父节点或新位置时保留每个 `NodeId`，六张侧表里的记录仍然有效，**不要**为移动调用 `on_node_removed()`——那会把一个仍然活着的节点的控件状态、焦点等清掉。

---

## 给「改动者」而非「调用方」的一条约束

以下这条不是给使用这个库的人的义务，是给**修改这个库本身**的人的：如果将来有人打算用 `LayoutTree::add_child()`（而不是现在的 `RenderTree::add_child()`）来挂载 overlay 容器，"overlay 打开期间可以安全 `layout()`" 这条安全性会立刻失效。

原因：`LayoutTree::Impl::run()` 的脏节点遍历只沿 `LayoutNode::children` 走（`src/layout/layout_tree.cpp` 的 `dirty`/`run()` 逻辑），而 overlay 容器目前只通过 `RenderTree::add_child()` 挂树，从未进入任何 `LayoutNode::children` 列表，因此永远不会被 `apply_bounds()` 遍历、覆写。这不是巧合设计出的“不需要管”，是两条不同挂树路径导致的偶然安全——一旦挂树方式改成走 `LayoutTree::add_child()`，容器就会被纳入布局遍历，`layout()` 会覆写它的 bounds。`examples/25_showcase/showcase_check.cpp:225` 附近的断言是这条安全性的回归测试；改动挂树路径前必须先确认这个测试还有意义。

---

## 已经失效、不再成立的说法

以下两条曾经是真实约束，最近的提交已经把它们从代码里去掉了，不要再遵守：

- ~~一个 `PopupHost` 同时只能开一个 handle~~ —— `PopupHandle` 现在自带 `tree`/`window`/`flags`（`include/drawgui/window/popup_host.h` 的 `struct PopupHandle`），`PopupHost::show()` 的注释明确写着“may have any number of handles open at once, each entirely self-contained”（同文件约 :171）。
- ~~overlay 打开期间不能调用 `layout()`~~ —— 已被证伪，见上一节的解释。
- 也不再需要在多处调用重复传同一个 `PopupFlags` 值——`show()` 现在把 flags 捕获进 `handle.flags`，后续 `handle_pointer()`/`handle_key()` 不再接受这个参数（`popup_host.h` 的 `PopupHandle::flags`）。
