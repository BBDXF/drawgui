# 控件（Widget）

## 这个子系统解决什么问题

**这个内核没有 widget 树。** 这是理解整个 `WidgetSet` 之前必须先接受的一件事：没有 `Button`/`Checkbox` 这样的类，没有声明式重建，没有 `createButton()` 这种函数。

`WidgetSet` 只是一张**侧表**：每个 `NodeId` 上可选地挂一个 `Widget` 结构体，和渲染树、布局树是并列的、按下标对齐的向量。它不添加节点、不建立身份、不做遍历。使用者要做的是三步：

1. 自己用 `LayoutTree::add_child()` 建出节点（外层容器、内部的标签/指示器/滑块等子节点）
2. 把这些节点组织成想要的几何结构（比如按钮 = 一个行容器 + 里面一个文本子节点）
3. 对其中「作为交互入口」的那个节点调用 `WidgetSet::attach(NodeId, Widget)`，声明它是什么控件、状态是什么

一个完整的「建一个按钮」的例子（改编自 `examples/05_widgets/widget_scene.cpp`）：

```cpp
using namespace dg;

// 1. 建外层容器节点（按钮的盒子）
BoxStyle host;
host.kind = LayoutKind::kRow;
host.cross_align = CrossAlign::kStretch;
host.padding = EdgeInsets::symmetric(6, 0);
host.width = 104;
host.height = 30;

NodeStyle style;
style.fill = Color::from_argb(0xFF2C3644);
style.border_color = Color::from_argb(0xFF141A22);
style.border_width = BorderWidths::all(1.0F);

NodeId button = tree.add_child(parent, host, style);

// 2. 建内部的文本子节点
BoxStyle caption_box;
caption_box.grow = 1;
NodeStyle caption_style;
caption_style.text.text = "Apply";
caption_style.text.font = ui_font;
caption_style.text.size = 13.0F;
caption_style.text.color = Color::from_argb(0xFFE8EDF4);
caption_style.text.align = TextAlign::kCenter;

NodeId label = tree.add_child(button, caption_box, caption_style);

// 3. attach 一个 Widget 描述符，只挂在承担交互的那个节点上
Widget widget;
widget.kind = WidgetKind::kButton;
widget.fill_normal = Color::from_argb(0xFF2C3644);
widget.fill_hover = Color::from_argb(0xFF3B4A5E);
widget.fill_pressed = Color::from_argb(0xFF1F2731);
widgets.attach(button, widget);
widgets.attach(label, Widget{.kind = WidgetKind::kLabel});  // 标签本身不可交互
```

没有第 3 步，节点仍然会被绘制，但不会响应点击、不会有 hover/press 状态变化，也不会出现在 `owner_of()`/`widget_at()` 的结果里。

参考实现：`include/drawgui/widget/widget_set.h`、`include/drawgui/widget/interaction.h`、`include/drawgui/widget/focus.h`。

## 9 种 WidgetKind

`WidgetKind` 是闭集合，一共 9 个值（`widget_set.h` 头部注释仍写着「四种」，那是历史遗留，已经过时——实际数量以枚举本身为准）：

| kind | 是否接受指针输入 | 能否获得键盘焦点 | 说明 |
|---|---|---|---|
| `kPanel` | 否 | 否 | 纯容器，点击穿透到内部的东西 |
| `kLabel` | 否 | 否 | 静态文本，对命中测试透明 |
| `kButton` | 是 | 是 | `fill_normal`/`fill_hover`/`fill_pressed` 三态 |
| `kCheckbox` | 是 | 是 | `checked` + `indicator` 子节点；`group` 有值时变成单选按钮（见下） |
| `kScrollView` | 是（用于滚轮）| **否** | 裁剪节点 + 无界约束子节点 + 滚动偏移（三个已有机制的组合） |
| `kSlider` | 是 | 是 | 轨道（本节点）+ 滑块（`thumb` 子节点），`value` 是运行时状态 |
| `kTextField` | 是 | 是 | 单行可编辑文本框，详见 `doc/guide/text.md` |
| `kList` | 是（用于滚轮）| **否** | 固定回收池的虚拟化列表（见下） |
| `kDropdown` | 是 | 是 | 只是「锚点 + 选项 + 已选索引」，弹出层要自己搭（见下方「配方」一节） |

### radio 不是独立的 kind

单选按钮就是 `kCheckbox` 上多设了一个 `group` 字段：

```cpp
Widget radio_a; radio_a.kind = WidgetKind::kCheckbox; radio_a.group = 1;
Widget radio_b; radio_b.kind = WidgetKind::kCheckbox; radio_b.group = 1;
```

点击一个已设 `group` 的 checkbox 会**选中**它（已选中时点击是空操作，不会取消选中），同时把同组里其它 `kCheckbox` 的 `checked` 清空。`WidgetSet::toggle()` 只改数据，不重绘——调用方要自己拿 `group_members()` 把被顺带改变的那些节点也 `refresh()` 一遍。

## Interaction：hover/press 状态机

`dg::Interaction` 是一个**只吃 widget，不吃坐标**的纯状态机——它接收的是命中测试之后解析出的目标节点,不是原始指针位置：

```cpp
Interaction interaction;

// 每个指针事件，先做命中测试解析出目标：
std::optional<NodeId> target = widgets.widget_at(tree.render(), pointer_pos);

InteractionChange change = interaction.moved_over(target);
if (change.left) { widgets.refresh(tree.render(), *change.left, interaction.state_of(*change.left)); }
if (change.entered) { widgets.refresh(tree.render(), *change.entered, interaction.state_of(*change.entered)); }

// 按下/抬起：
InteractionChange press_change = interaction.pressed_on(target);
InteractionChange release_change = interaction.released_on(target);
if (release_change.clicked) {
  // 这是真正的「激活」——按下和抬起落在同一个 widget 上才会有这个字段
}
```

四个事务性迁移方法——`moved_over`/`pressed_on`/`released_on`/`left_window`——每一个都在**一次调用**里返回全部受影响的节点（`left`/`entered`/`pressed`/`released`/`clicked` 五个独立的 `std::optional`）。这保证不会出现「上一个失焦了但下一个还没获焦」这种中间状态被重绘捕捉到、导致边界闪烁的情况。`InteractionChange` 不是「发生了什么事」的单一分类，是「这些节点各自发生了什么」的并列报告——一次释放可能同时结束一次按压、触发一次点击、还改变了 hover。

## owner_of() / widget_at()：命中语义

`RenderTree::hit_test()` 只回答几何问题：这个像素点下面**最深、最上层**画的是哪个节点——完全不管这个节点有没有挂 `Widget`。

`WidgetSet::owner_of(tree, id)` 在这之上再做一步：从 `id` 出发，沿**父链**往上爬，找到第一个挂了可交互 `Widget` 的祖先（或自身）。这就是为什么一个按钮的标签子节点不需要被特殊标记为「对命中测试透明」——点击落在标签上，`owner_of()` 会一路爬到按钮本身。

`widget_at(tree, point)` = `hit_test()` + `owner_of()` 的组合，是事件循环唯一需要调用的入口。

**这条父链爬升不会横跨到兄弟子树**——如果一个面板里挂了两个互不相关的按钮，点击落在按钮 A 内部的某个几何上层叠着但不属于按钮 A 子树的东西上，`owner_of()` 绝不会跳到按钮 B。这个语义直接决定了「遮挡」行为：一个不可交互的装饰性覆盖层如果几何上盖住了按钮，会拦下点击（`hit_test()` 命中的是它），除非它下面确实有个可交互祖先能被爬到。

## 常见任务怎么做

### 虚拟化列表：1000 项跑在固定数量的节点上

```cpp
Widget list_widget;
list_widget.kind = WidgetKind::kList;
list_widget.list_axis = ScrollAxis::kVertical;
list_widget.list_pool = pool_nodes;       // 事先建好的、固定数量的节点池
list_widget.list_item_count = 1000;       // 逻辑项数量，与池大小无关
list_widget.list_item_extent = 32;        // 每项的像素高度
widgets.attach(list_node, list_widget);

// attach 之后必须至少调一次 list_sync 才会真正显示内容:
std::vector<ListSlot> changed = widgets.list_sync(tree.render(), list_node, /*top_index=*/0);
for (const ListSlot& slot : changed) {
  // slot.node 现在应该显示逻辑项 slot.logical_index —— 由调用方自己写内容
  tree.render().set_text(slot.node, make_text_for(slot.logical_index));
}

// 滚动:
std::vector<ListSlot> reassigned =
    widgets.list_scroll_by(tree.render(), list_node, viewport_extent, /*dx=*/0, /*dy=*/-64);
```

回收池大小由**视口**决定，不由 `list_item_count` 决定——1000 项和 10 项用的是同一个池（例子里是 14 个节点）。`list_sync()` 只重绘真正改变了显示内容的槽位，不会全量刷新。

### 图片

```cpp
ImageCatalog images;
Expected<ImageId, ImageError> decoded = images.decode(png_bytes.data(), png_bytes.size());
ImageId source = decoded.value_or(ImageId{});

spec.images = images;   // TreeSpec 里注册，跟 fonts 的位置一样

NodeStyle style;
style.image.source = source;
style.image.fit = ImageFit::kCover;             // kFill / kContain / kCover / kNone 四选一
style.image.placeholder = Color::from_argb(0xFF3A4152);  // source 无效时画这个颜色，不留空洞
tree.add_child(parent, box, style);
```

只支持**位图**（BMP/GIF/ICO/JPEG/PNG/WBMP），**不支持 SVG，也没有九宫格（nine-patch）拉伸**。`ImageCatalog::decode()` 是同步解码，没有异步/线程池。

### ★ dropdown / 右键菜单 / tooltip / 对话框 —— 这些是「配方」，不是「控件」

这是本文档最重要的一条边界。`kDropdown` 这个 `WidgetKind` 本身极其单薄——`widget_set.h:117-132` 附近能看到，它只持有一个锚点（本节点）、一份 `options` 字符串列表和一个 `selected_index`。**弹出的那个选项列表，是应用层自己用 `PopupHost` 现搭出来的一组普通 `kButton`**，不是 `kDropdown` 内部管理的东西——参见 `examples/22_dropdown_menu/dropdown_options.cpp`：每一行都是手写的 `NodeId row = tree.add_child(...)` 加 `widgets.attach(row, Widget{.kind = WidgetKind::kButton, ...})`，和普通按钮没有任何区别。

`src/widget/tooltip.cpp` 更加极简——整个「tooltip」能力**只有一个 hover 计时器**（`HoverTimer`/`update_hover_timer()`/`hover_ready()`），负责回答「鼠标在这个节点上停留够久了吗」。真正把提示框画出来、定位、加个背景色，这些全部在 `examples/23_menu_tooltip_dialog/tooltip_content.cpp` 里手写。

右键菜单、模态对话框同理：`examples/23_menu_tooltip_dialog/` 里的 `menu_rows.cpp`/`dialog_panel.cpp` 都是应用层代码，用的都是已经存在的原语（`PopupHost` 弹出定位、`kButton` 做菜单项、`Focus::enter_scope(root, /*modal=*/true)` 做模态焦点陷阱），没有一行是库内部实现的「菜单控件」。

**如果你要做下拉菜单/右键菜单/tooltip/对话框，正确的做法是把 `examples/22_dropdown_menu/` 和 `examples/23_menu_tooltip_dialog/` 当模板抄**，而不是去 `widget_set.h` 里找一个现成的 API——它不存在。

### kScrollView/kList 不能获得焦点

`focus.h:109-123` 的 `is_focusable()` 是个穷举 switch：`kButton`/`kCheckbox`/`kSlider`/`kTextField`/`kDropdown` 可以被 Tab 到，`kPanel`/`kLabel`/`kScrollView`/`kList` 不行。后两者被排除是**故意的**而非疏漏——这个内核目前没有任何键盘滚动的代码，把一个 Tab 能到但没有键盘操作能力的东西做成焦点停靠点，体验比压根不能到达更差。

## 边界与坑

- **没有 widget 树、没有 `createXxx()` 工厂函数**——上文已经讲透。
- **dropdown/菜单/tooltip/对话框是应用层配方，不是库能力**——见上文★。
- **`kScrollView`/`kList` 不能获得键盘焦点**。
- 完整清单见 `doc/limitations.md` 对应条目。

## 相关 example

- `examples/05_widgets` —— 按钮/checkbox/radio/panel 的基本搭法
- `examples/11_form_controls` —— slider、更完整的表单控件组合
- `examples/12_text_input` —— 文本框（另见 `doc/guide/text.md`）
- `examples/13_image` —— 四种 `ImageFit` 对比
- `examples/15_list` —— 虚拟化列表的回收池
- `examples/21_focus` —— Tab 顺序、focus ring
- `examples/22_dropdown_menu` —— **下拉菜单的完整配方**（必抄模板）
- `examples/23_menu_tooltip_dialog` —— **右键菜单/tooltip/模态对话框的完整配方**（必抄模板）
