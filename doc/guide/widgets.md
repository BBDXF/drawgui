# 控件（Widget）

## 用 `dg::App`

每种控件就是一个 `NodeKind`：

| NodeKind | 读写 | 事件 |
|---|---|---|
| `kButton` | `set_text(DG_PROP_TEXT)` | `ClickEvent`（`clicks` 为连击数） |
| `kCheckbox` | `checked(node)` | `ClickEvent`（点击自动切换） |
| `kList` | `window->list(node)` → `ListRef::set_items/select/selected` | `ListSelectEvent` |
| `kScrollView` | 子节点直接 `create()` 到它下面；`scroll_offset()` | — |
| `kImage` | `set_image(node, app.images()...)` | — |
| `kDropdown` | `dropdown(node)` → `DropdownRef` | `ValueChangedEvent` |
| `kProgress` | `progress(node)` → `ProgressRef` | — |
| `kTabs` | `tabs(node)` → `TabsRef::add()` 返回面板节点 | `TabSelectEvent` |
| `kTree` | `tree(node)` → `TreeRef` | `TreeEvent` |
| `kTable` | `table(node)` → `TableRef` | `TableEvent`（`kSort` 时由你排序后 `set_rows()`） |
| `kSlider` | `slider(node)` → `SliderRef::set_value/value/set_range`；或 `DG_PROP_VALUE/MIN_VALUE/MAX_VALUE/STEP` | `SliderEvent`（`value`/`previous`，float） |
| `kRadio` | `radio(node)` → `RadioRef::select/selected/set_group`；`checked(node)`；或 `DG_PROP_RADIO_GROUP` | `ClickEvent`（和 checkbox 一样；方向键选中时 `clicks == 0`） |

- 列表只建固定数量的行节点并循环复用，1000 项和 10 项节点数相同；滚轮、
  滚动条拖动、点击选择都由运行时处理（`examples/15_list`）。
- `set_disabled(node, true)`：整棵子树不再响应指针/键盘，进行中的按下不会产生
  `ClickEvent`，焦点会移走——不需要自己取消 hover/press 或模糊焦点。
- 数据控件见 `examples/27_data_widgets`。`kList`/`kTree`/`kTable` 跟随布局后的
  尺寸：`grow`、父容器 stretch、百分比高度都生效（树和表格高度变化后在下一次布局
  同步时重建内部视图）。`kTabs` 的标签栏背景铺满整个节点宽度。
- 主题：`kButton`（`color.primary`/`-hover`/`-pressed`，文字 `color.on-primary`）、
  `kCheckbox`/`kRadio`（`color.surface` 底、`color.border` 边框、选中指示器
  `color.primary`，复选框指示器是圆角方块、单选是圆点）都从主题取色并随切换重涂。
  在它们上面写或绑定 `BACKGROUND_COLOR`（悬停/按下色由它派生）、`TEXT_COLOR`、
  `BORDER_COLOR` 时，你的值优先，主题不再覆盖那一项。复选框默认 32×32、内边距 6。
- 图片：`dg::prop::image_tint(color)`（`DG_PROP_IMAGE_TINT`）在绘制时给图片整体
  着色（SVG 和位图都可，保留 alpha），可 `bind_token()` 到主题色——单色 SVG 图标
  解码一次即可在多个节点上用不同颜色。
- 滑块（`kSlider`）：默认 200x20，范围 0..100、`step` 0（连续）、值 0。按下轨道
  任意处，滑块圆点跳到指针下并获得焦点，按住拖动会一直跟随直到松开（拖出轨道两端会
  夹住）；获得焦点后 Left/Down、Right/Up 移动一个 `step`（连续时为范围的 1/100），
  PageDown/PageUp 移动范围的 1/10，Home/End 到两端。每次用户改变值产生一个
  `SliderEvent`；`SliderRef::set_value()`/`DG_PROP_VALUE` 不产生事件。值总会被夹进
  范围并对齐到 `min + k*step`；`set_range()` 要求 `min < max`、`step >= 0`，改范围后
  当前值重新夹取。轨道/圆点颜色取主题的 `color.border` / `color.primary`，切主题自动
  重涂；`DG_PROP_HEIGHT` 会让圆点跟着变成同高的圆。滑块不接受子节点。
- 单选（`kRadio`）：圆形的 checkbox，属于本窗口里的一个组（`DG_PROP_RADIO_GROUP` /
  `RadioRef::set_group()`，整数，默认 0）。点击选中它并清掉同组其它项（已选中时点击
  不会取消），之后才排队 `ClickEvent`——和 checkbox 的「先切换、再发 CLICK、再查询
  `checked()`」完全一致；被清掉的兄弟节点由运行时重绘。获得焦点的单选按 Left/Up、
  Right/Down 会把焦点**和选中**移到同组上一个/下一个未禁用的单选（按创建顺序，循环），
  并在新节点上产生 `clicks == 0`、`x/y == 0` 的 `ClickEvent`。每个单选都是独立的 Tab
  停靠点。`RadioRef::select()` 不产生事件。完整示例：`examples/11_form_controls`。

下面的「直接使用底层」讲 `WidgetSet` / `Interaction` 本身。

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

## 直接使用底层

直接持有 `WidgetSet` 时，下面这些事要自己做（`dg::App` 都替你做了）：

- 滚动视图/列表：每次布局后 `sync_scrollbar()`；`list_scroll_by()` / `list_reset()`
  返回的 `ListSlot` 要自己按新逻辑下标重画。
- 禁用：写 `DG_PROP_DISABLED` 之后 `refresh_subtree()` 重画，`cancel_disabled()`
  取消进行中的按下/悬停，`blur_if_disabled()` 移走焦点。
- 组件（`Dropdown`、`Tabs`、`TreeView`、`Table`、`ProgressBar`）：布局后调用各自的
  `sync(ctx)`，事件用 `handle_pointer/handle_key` 逐个转发，不确定进度条要 `tick()`。
- 多行编辑框：布局后 `text_area_refresh()`。

改完要遵守 [`app.md` 的逃生舱约定](app.md#逃生舱约定)：只改运行时不组合的东西，改完调用 `Window::invalidate(node)`。

### 11 种 WidgetKind

`WidgetKind` 是闭集合，一共 11 个值（`widget_set.h` 头部注释仍写着「四种」，那是历史遗留，已经过时——实际数量以枚举本身为准）：

| kind | 是否接受指针输入 | 能否获得键盘焦点 | 说明 |
|---|---|---|---|
| `kPanel` | 否 | 否 | 纯容器，点击穿透到内部的东西 |
| `kLabel` | 否 | 否 | 静态文本，对命中测试透明 |
| `kButton` | 是 | 是 | `fill_normal`/`fill_hover`/`fill_pressed` 三态 |
| `kCheckbox` | 是 | 是 | `checked` + `indicator` 子节点；`group` 有值时变成单选按钮（见下） |
| `kScrollView` | 是（用于滚轮）| **否** | 裁剪节点 + 无界约束子节点 + 滚动偏移（三个已有机制的组合） |
| `kSlider` | 是 | 是 | 轨道（本节点）+ 滑块（`thumb` 子节点），`value` 是运行时状态 |
| `kTextField` | 是 | 是 | 单行可编辑文本框，详见 `doc/guide/text.md` |
| `kTextArea` | 是 | 是 | 多行可编辑文本框，自带垂直滚动，见下方「多行编辑框」 |
| `kList` | 是（用于滚轮）| **否** | 固定回收池的虚拟化列表（见下） |
| `kDropdown` | 是 | 是 | 只是「锚点 + 选项 + 已选索引」，弹出层由 `dg::Dropdown` 组件负责（见下方★一节） |
| `kScrollbar` | 是 | **否** | 滚动条的轨道或滑块，由 `attach_scrollbar()` 挂上，不要手动 attach（见下方「滚动条」） |

#### radio 不是独立的 kind

单选按钮就是 `kCheckbox` 上多设了一个 `group` 字段：

```cpp
Widget radio_a; radio_a.kind = WidgetKind::kCheckbox; radio_a.group = 1;
Widget radio_b; radio_b.kind = WidgetKind::kCheckbox; radio_b.group = 1;
```

点击一个已设 `group` 的 checkbox 会**选中**它（已选中时点击是空操作，不会取消选中），同时把同组里其它 `kCheckbox` 的 `checked` 清空。`WidgetSet::toggle()` 只改数据，不重绘——调用方要自己拿 `group_members()` 把被顺带改变的那些节点也 `refresh()` 一遍。

### Interaction：hover/press 状态机

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

### owner_of() / widget_at()：命中语义

`RenderTree::hit_test()` 只回答几何问题：这个像素点下面**最深、最上层**画的是哪个节点——完全不管这个节点有没有挂 `Widget`。

`WidgetSet::owner_of(tree, id)` 在这之上再做一步：从 `id` 出发，沿**父链**往上爬，找到第一个挂了可交互 `Widget` 的祖先（或自身）。这就是为什么一个按钮的标签子节点不需要被特殊标记为「对命中测试透明」——点击落在标签上，`owner_of()` 会一路爬到按钮本身。

`widget_at(tree, point)` = `hit_test()` + `owner_of()` 的组合，是事件循环唯一需要调用的入口。

**这条父链爬升不会横跨到兄弟子树**——如果一个面板里挂了两个互不相关的按钮，点击落在按钮 A 内部的某个几何上层叠着但不属于按钮 A 子树的东西上，`owner_of()` 绝不会跳到按钮 B。这个语义直接决定了「遮挡」行为：一个不可交互的装饰性覆盖层如果几何上盖住了按钮，会拦下点击（`hit_test()` 命中的是它），除非它下面确实有个可交互祖先能被爬到。

### 禁用状态（`disabled`）

禁用是一个**节点属性**，不是 `Widget` 字段：`DG_PROP_DISABLED`（id 52，`bool`），写进
`NodeStyle::disabled`。C++ 走 `dg::set_prop(tree, node, DG_PROP_DISABLED,
PropValue::boolean(true))`，C ABI 走 `dg_node_set_prop()` 加 `DG_VALUE_BOOL`。

**继承**：`dg::is_disabled(tree, id)`（`widget/interaction.h`）沿父链往上爬，自身或任一祖先
被禁用即为禁用。所以禁用一个容器会禁用它的整棵子树；重新启用容器后，子节点自己的
`disabled` 位原样保留（不是复制下去的）。

禁用后的行为：

- **指针**：`owner_of()`/`widget_at()` 爬到一个被禁用的可交互 widget 时返回 `nullopt`——
  而且是**吞掉**这次指针，不会继续往上交给启用中的祖先（禁用卡片里的按钮不会点到卡片）。
  因此没有 hover/press/click/toggle，文本框点不进去；`slidable_owner_of()` 同样返回
  `nullopt`，滑块拖不动。
- **按压中被禁用**：调用 `Interaction::cancel_disabled(tree)`，它把正被按住的 widget 报告为
  `released`、**不报告 `clicked`**，并清掉 hover。即便调用方没调它，之后的抬起也会落在
  `nullopt` 上，同样不会触发点击。
- **焦点**：`focus_order()` 跳过被禁用的节点及其整棵子树（Tab/Shift-Tab 不会停留）；
  `Focus::set_guarded()` 拒绝禁用目标；`Focus::blur_if_disabled(tree)` 在焦点变成禁用时
  **失焦**（不是跳到下一个——与 `blur_if_any_of()`/`exit_scope()` 的约定一致，要前进的
  调用方自己再调 `focus_next()`）。`Focus::set()` 依旧无条件。
- **快捷键**：`resolve_action()`/`route_key_event()` 把禁用的焦点视为没有焦点，挂在它
  （及其祖先链）上的节点级 action 不会触发；应用级（level 4）action 不受影响。
- **tooltip**：不显示。`widget_at()` 对禁用控件返回 `nullopt`，hover 计时器看不到它。
  这是刻意的简化：要给禁用控件解释「为什么不可用」，应把提示挂在外层容器上。
- **滚动**：`kScrollView`/`kList` 的滚轮滚动**不受影响**（`scrollable_owner_of()`/
  `list_owner_of()` 不看禁用位）——查看内容不算交互；被禁用列表里的行照样不能点。
  滚动条**照常显示**（滑块位置继续跟随滚轮），但**拖不动、点轨道也不翻页**：
  `owner_of()` 对禁用的滚动条部件返回 `nullopt`，`scrollbar_press()`/`scrollbar_drag()`
  自己也再查一次禁用位。
- **鼠标指针**：控件的默认光标（文本框的 I 形）失效，显示箭头；节点上显式设置的
  `DG_PROP_CURSOR` 仍然生效（见 `doc/guide/input.md`「鼠标指针形状」）。
- **程序化修改不受限**：`toggle()`/`set_slider_value()`/`text_field_insert()` 等仍然有效，
  禁用只拦截用户输入。

写完 `DG_PROP_DISABLED` 后，C++ 调用方要自己做同步（C ABI 的 `dg_node_set_prop()` 已替你做）：

```cpp
dg::set_prop(tree, group, DG_PROP_DISABLED, dg::PropValue::boolean(true));
InteractionChange c = interaction.cancel_disabled(tree.render());   // 取消按压/hover
FocusChange f = focus.blur_if_disabled(tree.render());              // 失焦
widgets.refresh_subtree(tree.render(), group, interaction);         // 重绘子树
```

外观：禁用控件只用两种颜色，都来自 `WidgetSet` 上的 `dg::DisabledPalette`
（`set_disabled_palette()`，取主题 token `color.disabled`/`color.on-disabled`，见
`doc/guide/theming.md`）：

| 部位 | 禁用时 |
|------|--------|
| 按钮/勾选框/文本框的填充 | `fill`（不再有 hover/press 变化） |
| 边框（任何有边框的节点） | `fill`——边框融进填充，控件变「平」，不会因为禁用反而多出一圈轮廓 |
| 文字（标签、按钮文字、文本框内容） | `on` |
| 已勾选的 checkbox/radio 标记 | `on`；未勾选的保持 `indicator_off` |
| 滑块轨道 / 圆点 | `fill` / `on` |
| 滚动条轨道 / 滑块 | `fill` / `on` |

禁用的控件本来就不会获得焦点，所以也不会画焦点环。单个 widget 仍可用
`Widget::fill_disabled`/`indicator_disabled` 覆盖调色板。

`refresh()` 只管填充和勾选标记；文字、边框、滑块颜色由 `refresh_subtree()` 处理，它会
遍历子树里的**每个节点**（不只是可交互的）。启用时的原颜色会被记住，重新启用后放回去；
如果禁用期间有人改写了这个颜色（例如主题绑定重新应用），就以新写入的值为准。所以写完
`DG_PROP_DISABLED`、或者切换主题后，都要对相关子树调一次 `refresh_subtree()`。
已知限制：禁用期间直接写某个节点的文字颜色，要到下一次 `refresh_subtree()` 才会变灰。

### 常见任务怎么做

#### 虚拟化列表：1000 项跑在固定数量的节点上

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

#### 滚动条

`kScrollView` 和 `kList` 可以带一个滚动条：两个**由调用方创建**的普通节点（和滑块的
轨道/圆点一样），交给 `attach_scrollbar()`：

```cpp
// 1. 视口在滚动条那一侧留出「槽」（reserved gutter），宽度 = 滚动条粗细
BoxStyle viewport_box;
viewport_box.scroll_axis = ScrollAxis::kVertical;
viewport_box.padding.right = widgets.scrollbar_style().size;   // 横向滚动则是 padding.bottom
// kList 不按 padding 摆放池节点，所以列表项本身要窄 size 个像素

// 2. 内容之后再加轨道，滑块是轨道唯一的子节点（盒子随便给，sync 会覆盖）
NodeId track = tree.add_child(viewport, BoxStyle{}, NodeStyle{});
NodeId thumb = tree.add_child(track, BoxStyle{}, NodeStyle{});
widgets.attach_scrollbar(tree.render(), viewport, track, thumb);

// 3. 布局之后摆一次
tree.layout_full();
widgets.sync_scrollbar(tree.render(), viewport, viewport_extent);
```

`viewport_extent` 是滚动方向上的可见长度——`kScrollView` 传
`content_bounds(viewport)` 的高（或宽），`kList` 传和 `list_scroll_by()` 相同的值。

**几何**：每个滚动视图只在它唯一的滚动轴上有一根滚动条（纵向滚动在右侧，横向在底部），
贴着视口边框盒的末端，粗 `ScrollbarStyle::size`。滑块长度 = 轨道长 × 视口 / 内容，
夹在 `[min_thumb_length, 轨道长]`；位置随偏移在 `0..(轨道长 - 滑块长)` 之间线性移动。
`scrollbar_geometry()` 返回这些数，测试和自定义绘制可以直接用。

**外观**：`refresh()` 把轨道和滑块都画成圆角胶囊（圆角半径 = `size / 2`）。滑块节点占满
整条轨道的宽度，看到的胶囊比它窄一圈：滑块带一圈和轨道同色的边框（静止时 `size / 5`，
至少 1 像素），所以看起来离轨道边缘留了空隙。指针移上去或按住时边框变成 `size / 10`，
胶囊变宽，再配合 hover/pressed 颜色一级级加深（暗色主题是变亮）。这圈空隙只是画出来的，
**点击范围仍是整个滑块节点**，不会因此变难抓。禁用时边框改用禁用调色板的 `fill`，
和变灰的轨道融为一体。颜色全部来自主题（`color.scrollbar-*`），没有写死的值。

**跟随滚动**：`scroll_by()`/`list_scroll_by()` 自己会更新滚动条，所以滚轮、键盘
（`apply_keyboard_scroll()`）和程序化的 `scroll_by()` 都不用额外操作。需要手动
`sync_scrollbar()` 的场合：建好场景后、任何碰到这个视口的 `layout()` 之后（布局会把轨道
放回内容原点，和 `resync_sliders()` 是同一种义务）、写完 `DG_PROP_SCROLLBAR` 后、以及绕过
`scroll_by()` 直接调 `RenderTree::set_scroll_offset()` 之后。

**槽还是覆盖**：选的是**预留槽**——视口自己留出 padding，滚动条永远不盖住可点的内容。
不留 padding 也能用，那时滚动条就覆盖在内容边缘上（库不会替你改布局）。`auto` 模式下槽
始终保留（内容变短、滚动条隐藏时也不重新布局），相当于 CSS 的 `scrollbar-gutter: stable`。

**可见性**：`DG_PROP_SCROLLBAR`（id 54，枚举）写在视口节点上：

| 值 | 行为 |
|----|------|
| `auto`（默认） | 内容溢出时显示，放得下时隐藏 |
| `always` | 总是显示；内容放得下时滑块占满整条轨道 |
| `never` | 不显示；滚轮/键盘/拖内容照常滚动 |

隐藏的滚动条把轨道和滑块缩成空矩形，命中测试永远找不到它们。

**交互**：轨道和滑块都是 `kScrollbar` widget，走普通的 `Interaction`（hover/press/按住
捕获），**都不能获得焦点**——点滚动条不会抢走正在输入的文本框的焦点。事件循环这样接：

```cpp
// 按下
auto owner = widgets.owner_of(tree.render(), hit);           // 轨道或滑块
interaction.pressed_on(owner);  /* refresh 受影响的节点 */
if (owner && widgets.scrollbar_owner_of(*owner)) {
  ScrollResult r = widgets.scrollbar_press(tree.render(), *owner, extent, pointer);
  // kList: r.slots 是被回收的槽位，像普通列表滚动一样重绘
}
// 移动（按住期间 Interaction::holding() 就是捕获对象）
if (auto held = interaction.holding()) {
  ScrollResult r = widgets.scrollbar_drag(tree.render(), *held, extent, pointer);
}
// 抬起
widgets.scrollbar_release();
interaction.released_on(target);
```

- **拖滑块**：按下时记住抓取点在滑块里的位置，之后滑块跟着指针走，偏移 =
  滑块位置 × max_offset / 行程，夹在两端。指针离开滚动条（甚至离开视口）继续拖也有效。
- **点轨道**：朝点击方向**翻一页**（一个 `viewport_extent`），不是跳到点击位置；按住
  不会连续翻页（没有定时器）。
- **hover/press 外观**：只有滑块变色（`thumb_hover`/`thumb_pressed`），轨道保持 `track` 色。
- **列表**：拖动经由 `list_scroll_by()`，所以回收池和普通滚动一样被正确重新分配；
  `ScrollResult::slots` 要传给重绘、`Focus::blur_if_any_of()`、`Interaction::forget()`。

**外观**来自 `WidgetSet` 上的 `dg::ScrollbarStyle`（`set_scrollbar_style()`），对应主题
token `color.scrollbar-track`/`-thumb`/`-thumb-hover`/`-thumb-pressed` 和
`size.scrollbar`，见 `doc/guide/theming.md`。

参考实现：`examples/10_scrolling`（底层）、`examples/15_list`（运行时；两者的 `--verify-*` 都检查滚动条）。

#### 图片

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

**支持的格式**：位图 PNG / JPEG / WebP（Skia 预编译包里注册并经测试的三种解码器），以及 SVG 矢量图标。

| 入口 | 行为 |
|---|---|
| `decode(bytes, size)` | **只解位图**（PNG/JPEG/WebP，按内容嗅探），**永不解析 SVG**——主题包把不可信资源字节送到这里，见 `theming.md` 的安全决定 |
| `decode_any(bytes, size, SvgOptions{})` | 格式无关：去掉 BOM/空白后以 `<` 开头的当 SVG，其余走 `decode()`。用于应用自带的可信资源 |
| `decode_svg(bytes, size, SvgOptions{})` | 明确按 SVG 解析 |
| `decode_file(path, SvgOptions{})` | 读文件后交给 `decode_any()` |

```cpp
// 单色图标 + 着色：保留图标自身的覆盖率/alpha，所有颜色替换为 tint
Expected<ImageId, ImageError> icon =
    images.decode_file("icons/plus.svg", SvgOptions{.tint = Color::rgba(0x4F, 0xC3, 0xF7)});
style.image.source = icon.value_or(ImageId{});
style.image.fit = ImageFit::kContain;
```

SVG 的处理方式：**绘制时按节点尺寸直接画矢量**（`SkSVGDOM::render` + 缩放），不做栅格缓存——任何节点尺寸、任何 DPI 下都清晰，节点改尺寸不需要重新栅格化，代价是每次绘制都走一遍矢量绘制（图标场景足够便宜）。`pixel_size()` 对 SVG 返回其固有尺寸（`width`/`height` 属性，没有则取 `viewBox`），fit 模式按它计算；没有正的固有尺寸的 SVG 会被拒绝。`tint` 按目录条目固定，同一个 SVG 要两种颜色就解码两次（只是一个解析好的 DOM，不是位图）。`ImageCatalog::is_vector(id)` 可区分两类条目。

**没有九宫格（nine-patch）拉伸**。所有解码都是同步的，没有异步/线程池。参考：`examples/13_image`（第六个面板是着色的 SVG 图标）、`tests/unit/test_image_formats.cpp`。

#### ★ dropdown / 右键菜单 / tooltip / 对话框 —— 库组件层（`include/drawgui/ui/`）

这四样**不是新的 `WidgetKind`**，而是建在 `PopupHost` + 现有控件之上的**组件层**：
`dg::Menu`、`dg::Dropdown`、`dg::Tooltip`、`dg::Dialog`。之所以需要单独一层：
`WidgetSet` 从不创建节点（它只给调用方建好的节点挂行为），`PopupHost` 从不知道
容器里放什么；组件层是第一处"既建节点、又注册控件、又负责开关浮层"的库代码。
没有新的绘制原语、没有回调——宿主循环调用 `handle_pointer()`/`handle_key()`/
`tick()`，读回一个带类型的结果（`MenuResult`/`DropdownResult`/`TooltipEvent`/
`DialogResult`），和其它控件 API 的驱动方式一致。

- `kDropdown` 本身仍然只持有锚点、`label`、`options`、`selected_index`
  （这些状态要比弹层活得久）。`dg::Dropdown` 把它和一个 `dg::Menu` 接起来：
  打开时从 `dropdown_options()` 建菜单、给当前选中项打勾并高亮；选中时调用
  `dropdown_select()` 并报告 `kChanged{selected, previous}`。
- `src/widget/tooltip.cpp` 仍然只是 hover 计时器；`dg::Tooltip` 用它决定何时
  弹出文字气泡、何时销毁。
- 菜单项、对话框按钮都是普通 `kButton`，注册进宿主自己的 `WidgetSet`；高亮就是
  `dg::Focus` 的当前焦点，模态陷阱就是 `Focus::enter_scope(panel, true)`。

完整 API、事件路由和示例见 [`popups.md`](popups.md) 的"库组件"一节；
`examples/25_showcase` 直接使用这些组件；`examples/22_dropdown_menu`、
`examples/23_menu_tooltip_dialog` 通过 `dg::App` 间接使用它们。

#### ★ 数据控件：ProgressBar / Tabs / TreeView / Table（组件层）

和弹层组件一样，这四个**不是新的 `WidgetKind`**，而是 `include/drawgui/ui/` 里的
构建器类：它们通过 `LayoutTree` 建节点、注册普通控件，宿主循环调用
`handle_pointer()`/`handle_key()` 读回带类型的结果，**没有回调**。共享的参数包是
`dg::ControlContext`（`control_context.h`：`LayoutTree&` + `NodeSideTables` +
字体 + `ControlStyle`），颜色/尺寸用 `control_style_from_theme()` 从已有 token
读取（surface/on-surface/border/primary/on-primary/on-disabled、radius.sm、
space.md），**没有新增 token**。

**调用方义务：每次 `layout()` 之后对每个控件调用 `sync(ctx)`**——和
`resync_sliders()`/`sync_scrollbar()` 同一类义务：布局会把按绘制期定位的子节点
放回原点。拆除用 `destroy(ctx)`（内部是 `remove_subtree()`），节点与控件计数回到基线。

| 组件 | 节点/控件 | 结果类型 |
|---|---|---|
| `dg::ProgressBar` | 轨道（kLeaf，裁剪）+ 填充；**不挂任何控件、不可聚焦** | `set_value()`/`tick()` 返回 bool |
| `dg::Tabs` | 标签条（每个标签一个 `kButton`）+ 面板栈 | `TabsResult{kSelected, index}` |
| `dg::TreeView` | 外框一个可聚焦 `kButton` + `kList` 回收池 + 滚动条 | `TreeResult{kSelected/kExpanded/kCollapsed, item}` |
| `dg::Table` | 外框 `kButton` + 表头行（可排序列是 `tab_index=-1` 的 `kButton`）+ `kList` 回收池 | `TableResult{kSelected, row}` / `{kSortRequested, column}` |

要点：

- **ProgressBar**：确定模式填充 `value∈[0,1]` 的轨道宽度；不确定模式是一段
  （`indeterminate_percent`，默认 30%）每 `indeterminate_period_ms` 从左扫到右，
  位置是 `tick(ctx, AnimTime now)` 的**纯函数**（时钟缝和 `set_edit_time()` 相同，
  测试给定时间即得定帧）。`AnimationEngine::reduced_motion()` 时停在中间，
  `animating()` 返回 false。
- **Tabs**：**漫游焦点**——只有选中的标签在 Tab 序列里（其他标签 `tab_index=-1`）；
  焦点在标签上时 Left/Right 移到上/下一个**启用**的标签（循环），Home/End 到首/末。
  **自动激活**：方向键移到哪个标签就选中哪个（"选择跟随焦点"——面板早已建好，
  切换很便宜）；Enter 被接受但不再做事（`Key` 没有 Space）。点击选中并聚焦。
  禁用标签（`TabItem::disabled` → `NodeStyle::disabled`）永远不会被选中/聚焦。
  **隐藏面板**：所有面板都留在布局里（切换零重排）；未选中的面板设
  `NodeStyle::disabled`（整棵子树退出 Tab 序列与指针归属，焦点在里面时
  `blur_if_disabled()` 将其清掉），渲染边界收成空的裁剪框（不绘制、不命中）。
  再次选中时恢复并对该面板跑一次 `refresh_subtree()`。内容加在 `tabs.panel(i)` 下。
- **TreeView**：模型是先序的扁平 `TreeItem{label, depth, expanded}` 列表。展开/折叠
  重新算**扁平化可见行**（O(n)），把行数交给下面的 `kList`
  （新增的 `WidgetSet::list_reset()`），回收池大小是
  `ceil(viewport_height/row_height)+1`，**1 万项和 10 项一样**。
  键盘（外框有焦点时）：Up/Down/Home/End/PageUp/PageDown 移动选择，Right 展开或进入
  第一个子项，Left 折叠或回到父项；点击披露列切换展开，点击行其他位置选中；滚轮、
  滚动条只滚动。折叠一个祖先时选择移到该祖先（事件仍报 `kCollapsed`）。
- **Table**：列宽固定，`width_px` 或 `width_percent`（`BoxPercents`，相对行宽）或
  都不给（占剩余）；表头右侧也留滚动条槽，所以对齐。表格**持有**单元格字符串副本；
  **排序归应用**：点击可排序列表头报告 `kSortRequested`，应用自己排序后
  `set_rows()`，可选 `set_sort_indicator()`（标题后加 ` ^`/` v`）。

`WidgetSet::list_reset(tree, list, item_count, viewport_extent)` 是为此新增的唯一
`WidgetSet` API：替换 `kList` 的行数、把滚动偏移夹到新范围、忘掉所有槽位分配并把
所有槽停到内容原点上方（裁剪之外），再 `list_sync()`——行数缩小时不会留下陈旧行；
它同时也是布局之后的重同步。视觉集中在 `src/ui/control_visuals.cpp`，最小可用，
等设计侧打磨。示例：`examples/27_data_widgets`（运行时版，`--verify`、`--dump-png`），`examples/25_showcase`（直接使用组件）。

#### 多行编辑框（`kTextArea`）

`kTextArea` 和 `kTextField` 共用同一个编辑内核（`include/drawgui/widget/text_edit.h`），
差别在于：内容子节点 `TextStyle::wrap = true` 自动换行，几何来自 `dg::Paragraph`
的行信息（`offset_at`/`caret_rect`/`range_rects`/`lines`）；选区和输入法下划线可能跨多行，
所以它们是**节点池**而不是单个节点；它自己就是垂直滚动容器（与 `kScrollView`
相同的 `RenderTree::set_scroll_offset()` 机制），可以挂滚动条。

```cpp
Widget area;
area.kind = WidgetKind::kTextArea;
area.text_inset = 10;                      // 文字与边框的内边距（四边）
area.selection_layer = layer;              // 画在 content 下面的普通子节点
for (int i = 0; i < 32; ++i) {             // 每个可见的选中行一个
  area.highlight_pool.push_back(tree.add_child(layer, placeholder, highlight_style));
}
for (int i = 0; i < 4; ++i) {              // 每个可见的组字行一个
  area.underline_pool.push_back(tree.add_child(area_node, placeholder, underline_style));
}
area.content = content;                    // text.wrap = true
area.caret = caret;
widgets.attach(area_node, area);
widgets.attach_scrollbar(tree.render(), area_node, track, thumb);  // 可选，最后添加
widgets.text_area_set_focus(tree.render(), fonts, area_node, true);

widgets.text_area_insert(tree.render(), fonts, area_node, "\n");      // Enter
widgets.text_area_move(tree.render(), fonts, area_node, TextAreaMove::kLineDown, shift);
widgets.text_area_click(tree.render(), fonts, area_node, pointer, /*extend=*/drag || shift);
```

- **按键**：Enter（裸键或 Shift）插入 `\n`，Ctrl/Alt+Enter 交给快捷键路由；
  Up/Down 保持 `TextEditState::preferred_x` 列位置（在首行按 Up 到文首，末行按 Down 到文末）；
  Home/End 是**视觉行**（软换行处 End 用上游亲和，光标留在本行末尾）；Ctrl+Home/End
  是全文首尾；PageUp/PageDown 按视口高度移动并滚动。Tab 仍然是焦点切换。
  任何非垂直移动、编辑、点击都会清掉 `preferred_x`。
- **滚动**：每次编辑/移动后都会把光标滚进视口；滚轮（`scroll_by()`，
  `scrollable_owner_of()` 也会找到 `kTextArea`）和拖动滚动条只动视口，不动光标。
  文字宽度 = 视口宽 − 2×`text_inset` − 滚动条槽（挂了滚动条且 `DG_PROP_SCROLLBAR` 不是 `never` 时）。
- **选区**：每行一个矩形；选中了空行的换行符时画一个固定 6px 宽的小块。
  池子大小就是能同时高亮的可见行数上限，超出的行不画。
- **布局之后要 `text_area_refresh()`**：和滑块一样，`kLeaf` 的子节点每次重排都会被放回
  内容原点，C ABI 的事件泵每帧都会调一次。
- **输入法候选框**：`text_area_caret_rect()` 给出光标的绝对矩形，交给
  `WindowManager::start_text_input()`（即 `SDL_SetTextInputArea`）。
- 与 kind 无关的访问器 `editor_state()`/`editor_insert()`/`editor_select_all()`/
  `editor_set_text()`/`editor_is_composing()` 同时适用于两种编辑框——剪贴板动作和快捷键路由用的就是它们。

#### 编辑框通用：撤销、按词操作、多击选择、长度上限

以下对 `kTextField` 和 `kTextArea` 都成立，规则都在编辑内核 `text_edit.h` 里：

- **撤销/重做**：`editor_undo()`/`editor_redo()`（Mod+Z、Mod+Shift+Z、Mod+Y）。
  每次改文字前记一份 `{text, cursor, selection_anchor}` 快照，深度上限 100，有新编辑就清空重做栈。
  连续输入（或连续 Backspace、连续 Delete）在 1 秒内、且从上次编辑留下的光标处继续时合并成一步；
  在非空白后输入空白会开新一步，所以 "hello world" 撤销两次。光标移动、选区变化、粘贴、剪切、
  按词删除、输入法提交都各自是独立的一步；组字预览从不记录，组字期间撤销被抑制。
  合并用的时间来自 `WidgetSet::set_edit_time(AnimTime)`（C ABI 每次按键/输入前写入
  `steady_anim_time()`，测试写固定值），内核不读真实时钟。`editor_set_text()` 会清空撤销历史。
- **按词**：Ctrl+Left/Right（`kWordLeft`/`kWordRight`，加 Shift 扩展）、Ctrl+Backspace/Delete
  （`editor_delete_word()`）。分词按字素簇的首码点分类：空白、换行、标点符号、CJK（汉字/假名，
  连续的一串算一个词）、emoji（每个 emoji 簇单独成词）、其余（字母、数字、`_`）。
- **多击**：`text_field_click()`/`text_area_click()` 的 `click_count`——2 选词
  （`text_edit::word_range()`，点在词右半边也选这个词），3 在 TextField 全选、在 TextArea 选所在逻辑行。
  连击计数用 `dg::ClickCounter`（`widget/click_counter.h`）。
- **长度上限**：`editor_set_max_length(id, n)`，单位是**字素簇**（用户看到的字符，不是字节），0 = 不限。
  输入、粘贴、输入法提交时把插入的串在字素边界截断到放得下的长度；`editor_set_text()` 也截断；
  已有文字不会因为调小上限而被截掉。

完整示例：`examples/26_text_area`（C ABI，`--verify-text-area`/`--dump-png`）。

#### kScrollView/kList 不能获得焦点

`focus.h` 的 `is_focusable()` 是个穷举 switch：`kButton`/`kCheckbox`/`kSlider`/`kTextField`/`kTextArea`/`kDropdown` 可以被 Tab 到，`kPanel`/`kLabel`/`kScrollView`/`kList`/`kScrollbar` 不行。后两者被排除是**故意的**而非疏漏——把一个 Tab 能到但没有键盘操作能力的东西做成焦点停靠点，体验比压根不能到达更差。键盘滚动（PageUp/PageDown/Home/End）现在由 `apply_keyboard_scroll()` 实现，但它是从当前焦点向上找最近的可滚动祖先，而不是让 ScrollView/List 自身可聚焦（见 `doc/guide/input.md`）。

## 边界与坑

- **没有 widget 树、没有 `createXxx()` 工厂函数**——上文已经讲透。
- **dropdown/菜单/tooltip/对话框是组件层（`include/drawgui/ui/`），不是 `WidgetKind`**——由宿主循环驱动、不支持嵌套，见上文★和 `doc/limitations.md` B 节。
- **`kScrollView`/`kList` 不能获得键盘焦点**，它们的滚动条也不能（`TreeView`/`Table` 的焦点在外框 `kButton` 上，绕开了这一点）。
- **数据控件（ProgressBar/Tabs/TreeView/Table）布局后要 `sync()`**，见上文★。
- **滚动条的节点由调用方创建**，布局之后要 `sync_scrollbar()`；视口要自己留出槽，见上文「滚动条」。
- **禁用是节点属性，写完要同步**：C++ 侧写 `DG_PROP_DISABLED` 后要调用
  `cancel_disabled()`/`blur_if_disabled()`/`refresh_subtree()`，见上文「禁用状态」。
- 完整清单见 `doc/limitations.md` 对应条目。

## 相关 example

- `examples/05_widgets` —— 按钮/checkbox/radio/panel 的基本搭法
- `examples/11_form_controls` —— 运行时的 `kCheckbox`/`kRadio`/`kSlider` 表单：两个单选组、
  一个随窗口变宽的滑块和一个带 `step` 的定宽滑块；底部一行演示禁用状态（开关通过
  `set_disabled()` 启用/禁用旁边的 checkbox 与 slider，`--verify-form-controls` 校验它）
- `examples/12_text_input` —— 文本框（另见 `doc/guide/text.md`）
- `examples/26_text_area` —— 多行编辑框（C ABI 驱动）
- `examples/13_image` —— 四种 `ImageFit` 对比
- `examples/15_list` —— 虚拟化列表的回收池
- `examples/21_focus` —— Tab 顺序、focus ring
- `examples/22_dropdown_menu` —— 运行时 `kDropdown` 的完整用法
- `examples/23_menu_tooltip_dialog` —— 运行时的右键菜单/tooltip/对话框
- `examples/28_c_components` —— 同一批组件从纯 C 使用：`DG_NODE_TYPE_PROGRESS`/`TABS`/
  `TREE`/`TABLE`/`DROPDOWN`（`doc/reference/c-abi.md` 第 5 节「组件」）
- `examples/27_data_widgets` —— 运行时的进度条/页签/树/表格（底层组件见 `examples/25_showcase`）
