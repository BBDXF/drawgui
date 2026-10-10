# 布局

## 用 `dg::App`

节点用 `Window::create(kind, parent)` 建，布局属性用 `set()` / `set_prop()` 写，
运行时在每次 `App::pump()` 里自动重新布局和重绘——不需要调用 `layout()`。

```cpp
const dg::NodeId root = window->create(dg::NodeKind::kBox).value();
window->set(root, {dg::prop::width_percent(100), dg::prop::height_percent(100),
                   dg::prop::column(), dg::prop::gap(12)});
const dg::NodeId row = window->create(dg::NodeKind::kBox, root).value();
window->set(row, {dg::prop::row(), dg::prop::gap(8), dg::prop::width_percent(100),
                  dg::prop::grow(1)});
window->set_prop(row, DG_PROP_PADDING_L, dg::PropValue::number(20));
```

- 普通 `kBox` 一开始是 240×120 的叶子；写 `DG_PROP_DIRECTION`（`prop::row()` /
  `prop::column()`）会把它变成行/列容器。做根节点或容器时记得给尺寸
  （像素、百分比）或 `grow`，否则内容会被挤在 240×120 里；想让它按内容收缩
  （或由父容器 stretch/grow 决定），写 `prop::width_auto()` / `prop::height_auto()`
  （`PropValue::auto_length()`，C ABI 为 `DG_LENGTH_UNIT_AUTO`）把默认尺寸去掉。
  默认值保持 240×120 是为了兼容已有调用方（ABI 测试与示例依赖它）。
- `kList`/`kTree`/`kTable` 跟其他节点一样参与 flex：`grow`、父容器的 stretch、
  百分比高度都会生效；树和表格在布局后发现高度变了，会按新高度重建内部视图
  （下一帧生效）。
- `gap`、`grow`、`padding_*` 是数值（`PropValue::number`），宽高是长度
  （`PropValue::length` / `percent`，即 `prop::width()` / `prop::width_percent()`）。
- 滚动：建 `NodeKind::kScrollView`，往它下面 `create()` 的子节点会自动放进它的
  内容节点（`Window::child_container()` 告诉你实际位置）；滚动条和滚轮由运行时处理，
  `Window::scroll_offset()` 读当前偏移。
- 裁剪、透明度：`DG_PROP_OVERFLOW`、`DG_PROP_OPACITY`。
- 尺寸的三种写法：像素（`prop::width(px)`）、百分比（`prop::width_percent(50)`，相对父节点的
  内容盒，规则见下文「百分比尺寸」）、`auto`（`prop::width_auto()`，没有自己的尺寸）。同一字段
  的像素和百分比互斥，后写的生效。
- 隐藏：`prop::visible(false)`，见下文「隐藏节点」。
- 属性清单：`doc/reference/`；`dg::prop::*` 只覆盖最常用的几个。

### 隐藏节点：`visible`（display:none）

```cpp
window->set(panel, dg::prop::visible(false));   // C ABI：DG_PROP_VISIBLE，DG_VALUE_BOOL 0
```

`DG_PROP_VISIBLE`（bool，默认 1）采用 **CSS `display:none` 语义**，而不是
`visibility:hidden`：隐藏的节点和它的整棵子树

- **不占布局空间**：所有容器（row/column/wrap/absolute/leaf）把它当作不存在——没有尺寸、
  没有 margin，也不计入 `gap`；它自身的矩形收成空矩形（子树内的矩形同样清空）；
- **不绘制、不参与命中测试**（`hit_test()` 直接跳过子树）；
- **不能获得焦点、不响应 hover/press、不弹 tooltip**：`dg::is_disabled()` 对隐藏节点及其后代
  返回 true；隐藏正获得焦点的控件（或其祖先）会把焦点清掉（同 `disabled` 的
  `blur_if_disabled()` 路径），`Window::focus()` 返回 `kUnsupported`。

切换会触发重排：写 `visible` 同时标记节点和父节点。隐藏子树里的属性变更不会触发布局；
重新显示时整棵子树从头布局，所以**增量布局与完整布局逐矩形一致**（单测
`tests/unit/test_runtime_layout_props.cpp`）。为什么不选 `visibility:hidden`：想保留空位时，
设 `opacity = 0` 加 `disabled` 即可得到同样效果；而「从布局里拿掉、但不销毁节点和它的状态」
只有 `visible` 能做到。`examples/29_enterprise` 用它收起/展开详情面板（展开时再配一个
`Window::animate()` 的淡入）。

下面的「直接使用底层」讲布局引擎本身：约束协议、`LayoutTree` 的增量布局。
用 `dg::App` 时这些都由运行时完成，读它是为了理解行为和排查问题。

## 这个子系统解决什么问题

drawgui 没有 HTML/CSS 那样的多遍回流（reflow）。它用的是 Flutter 风格的单遍协议：父节点向下传递约束（constraints），子节点向上报告尺寸（size）。每个节点在一次布局中只被测量一次。

这个协议本身不是这个项目发明的，但这里对它做了一处刻意的偏离：**约束和尺寸都是整型设备像素，不是浮点逻辑像素**。原因是 damage（脏矩形）系统要求「一个节点声明的矩形，必须和它实际画的矩形完全相同」。浮点布局总要在某处取整，而节点左边缘从 10.4 滑动到 10.6 时，取整结果会从 10 跳到 11——这一帧里 damage 矩形和实际绘制矩形就会错开一个像素。整型布局从根上排除了这类问题，代价是 DPI 变化会触发一次重新布局而不仅仅是重绘（但 DPI 变化本来就会改变帧缓冲尺寸从而强制全量重绘，所以这个代价基本是免费的）。

第二个偏离是：**没有 intrinsic sizing**（内在尺寸测量，比如「这段文字/这组子节点自己需要多宽」）。intrinsic sizing 需要投机性地测量子树,可能导致同一节点被布局不止一次，且最坏情况下是 O(n²)。这个内核选择完全不提供它——一个 Row 通过给子节点一个无界（unbounded）主轴约束来获取子节点的自然尺寸，这仍然是普通的一遍布局，不是第二遍。这是个硬边界：容器能按子节点**已声明或已测出**的尺寸收缩（`width_auto()` 就是这样），但不会为了「求内容需要多宽」去投机测量子树；文本的 wrap 高度是按给定宽度测出来的，见 [text.md](text.md)。

参考实现：`include/drawgui/layout/box.h`（约束、样式的定义）与 `include/drawgui/layout/layout_tree.h`（布局树、增量布局）。

## 直接使用底层

直接持有 `dg::LayoutTree` 时，下面这些事要自己做（`dg::App` 都替你做了）：

- 改完盒样式后调用 `layout()`（首次用 `layout_full()`），再 `repaint()`。
- 布局移动了节点之后，滚动视图/列表/多行编辑框的组合子节点要重新同步
  （`sync_scrollbar()`、`list_reset()`、`text_area_refresh()`，见 [widgets.md](widgets.md)）。
- 在 `dg::App` 的窗口里通过 `Window::layout()` 自己加节点也可以：只在 `kBox`
  下面加。改完要遵守 [`app.md` 的逃生舱约定](app.md#逃生舱约定)：只改运行时不组合的东西，改完调用 `Window::invalidate(node)`。

### 核心类型和它们的关系

- `dg::BoxConstraints`：一对 `(min, max)` × 两个轴。`kUnbounded` 是 `INT_MAX`，表示「你想多大都行」。
- `dg::EdgeInsets`：margin / border / padding 共用的四边间距结构。
- `dg::BoxStyle`：一个节点在布局层面的全部属性——`kind`、尺寸、flex 参数、gap、对齐方式等。
- `dg::LayoutKind`：六个值，闭集合，不是虚函数体系：
  - `kLeaf` —— 自己决定尺寸，不排列子节点（子节点若存在，会以内容原点为起点、松约束地布局，这就是「一个带内容的装饰盒子不需要单独的容器类型」的原因）
  - `kRow` / `kColumn` —— 子节点沿主轴依次排列
  - `kWrapRow` / `kWrapColumn` —— 同上，但放不下时另起一行/列
  - `kAbsolute` —— 子节点按自己的 `left/top/right/bottom` 相对本节点内容盒定位
- `dg::LayoutTree`：拥有一棵 `dg::RenderTree`（不是并列关系，是包含关系——两棵存相同矩形的树会互相打架）。`layout_node` 编号 i 就是 `NodeId{i}`，没有映射表。

最小示例（一个有内边距的行容器，里面两个子节点）：

```cpp
using namespace dg;

TreeSpec spec;
spec.viewport = PixelSize{800, 600};
LayoutTree tree{spec};

BoxStyle row;
row.kind = LayoutKind::kRow;
row.gap = 8;
row.padding = EdgeInsets::all(12);
row.cross_align = CrossAlign::kCenter;
NodeId container = tree.add_child(LayoutTree::root(), row, NodeStyle{});

BoxStyle child_box;
child_box.width = 100;
child_box.height = 40;
NodeId a = tree.add_child(container, child_box, NodeStyle{});
NodeId b = tree.add_child(container, child_box, NodeStyle{});

LayoutStats stats = tree.layout();
PixelRect bounds_a = tree.bounds(a);
```

### 常见任务怎么做

#### 建立一棵树并做首次布局

用 `LayoutTree` 而不是直接用 `RenderTree`——`LayoutTree` 内部会创建并拥有一个 `RenderTree`，`render()` 拿到的就是它。首次布局用 `layout_full()`（忽略所有缓存，全量布局），之后每次改动只调用 `layout()`（只重排脏的部分）。参见 `examples/04_layout/layout_scene.cpp`。

#### 用 flex（grow / shrink / basis）分配剩余空间

```cpp
BoxStyle flexible;
flexible.grow = 2;              // 整数权重，占「剩余空间」的份额
BoxStyle fixed;
fixed.basis = 120;               // 声明的基准主轴尺寸（不是测量出来的）
fixed.shrink = 1;                // 只有 basis 是「声明的」时才会生效
```

要点：
- `grow` 是**整数权重**，不是浮点比例。剩余空间按整数除法精确分配，余数给最早的子节点——这保证增量布局和全量布局产生逐字节相同的结果，浮点权重在四舍五入的边界上做不到这一点。
- `shrink` 只在子节点的 `basis` 被**显式声明**时才生效。`basis` 缺省表示「使用测量出的自然尺寸」，而没有声明基准就无法计算收缩份额——测量两次会把单遍布局变成指数级复杂度。
- `grow`/`shrink` 在 `kWrapRow`/`kWrapColumn` 下被直接拒绝（写进 `diagnostics()`），因为一个 run（行/列）要等所有子节点都布局完才能确定尺寸，届时再拉伸就要求二次布局，这违反「每个节点只布局一次」的不变量。

#### 让容器在放不下时自动换行

```cpp
BoxStyle wrap;
wrap.kind = LayoutKind::kWrapRow;
wrap.run_gap = 8;                          // 相邻「行」之间的间距（跨轴）
wrap.align_content = AlignContent::kSpaceBetween;  // 多行整体在跨轴上的分布
```

`run_gap` 只对多行/多列生效——单行容器没有第二行可分隔。`align_content` 决定「行」这个整体怎么摆放，和决定单个子节点在自己所在行里怎么对齐的 `cross_align`是两码事。

#### 主轴留白：space_around / space_evenly

```cpp
row.main_align = MainAlign::kSpaceAround;   // 两端各半份、子节点之间整份
row.main_align = MainAlign::kSpaceEvenly;   // n 个子节点切成 n+1 份等距
```

属性层对应 `justify = space_around / space_evenly`。两者和 `align_content` 的 `space_around` 用同一种「累计份额」整数算术：第 i 个子节点前面的留白是 `floor(leftover * k / d)`，相邻两个子节点按差值推进，所以总和恰好等于 leftover，截断余数稳定地落在靠后的间隙里（101 宽、3 个 20 宽子节点、`kSpaceAround` → x = 6, 40, 74）。只有一个子节点时两者都退化为居中。wrap 容器里按每一 run 分别计算。

#### 百分比尺寸

```cpp
BoxStyle half;
half.percent.width = 50.0F;       // 50 表示 50%
half.percent.max_height = 25.0F;
// 属性层：set_prop(tree, node, DG_PROP_WIDTH, PropValue::percent(50));
```

- 只有 `width`/`height`/`min_*`/`max_*` 六项支持百分比；`padding`/`margin`/`gap` 不支持（它们在 toml 里是 `float` 而不是 `length`，并且 padding 的百分比在 CSS 里相对的是宽度这种跨轴语义，不值得引入）。
- **参照物是父节点在该轴上「可用的内容盒」**：父节点给子节点的最大范围（扣掉父的 border+padding，但还没扣子节点自己的 margin）。父节点尺寸被定死时，这正好就是它的内容盒；父节点是「收缩到内容」时，参照的是它允许的最大值。
- 解析是 `floor(参照 * 百分比 / 100)`，整数设备像素，同样的两个输入永远得到同一个像素，增量布局和全量布局逐字节一致。
- **参照轴无界时（比如滚动视口的滚动轴）百分比按「未设置」处理**：width/height 变 auto，min 变 0，max 变无上限，不报诊断。design.md 5.4.5 原本把这种情况定为错误；这里改为 auto，因为「滚动列表里宽 100% 的行」是最常见的用法，而它竖直方向的百分比没有别的合理含义。
- 同一个字段的像素值和百分比互斥：写百分比会把像素字段恢复为默认值，写像素会清掉百分比；直接改 `BoxStyle` 时如果两者都填了，以百分比为准。
- 父节点在解析子节点百分比之后才读子节点的尺寸（flex 的声明 basis 就读 `width`），所以百分比从不导致二次测量；父节点 resize 或改 padding 时，子节点会因为参照变化而重新布局（参照值是缓存键的一部分，但只对带百分比的节点生效，不带百分比的树访问的节点集合和以前完全相同）。

#### 让内容滚动

```cpp
BoxStyle viewport_box;
viewport_box.kind = LayoutKind::kLeaf;
viewport_box.scroll_axis = ScrollAxis::kVertical;   // 单个子节点在这一轴上得到无界约束
NodeStyle viewport_style;
viewport_style.overflow = Overflow::kClip;          // 溢出内容被裁掉

NodeId viewport = tree.add_child(parent, viewport_box, viewport_style);
NodeId content = tree.add_child(viewport, content_box, NodeStyle{});  // 可以比 viewport 更高

// 之后改变滚动位置，不触发重排:
tree.render().set_scroll_offset(viewport, PixelPoint{0, scroll_y});
```

**滚动偏移是运行时状态，不是属性**——这是刻意设计。`set_scroll_offset()` 只重新计算子节点的绝对坐标和裁剪边界，从不调用 `measure()`，所以一次滚轮滚动的代价是一次重绘，永远不是一次重排。真正可用的示例见 `examples/10_scrolling/`；实际的封装（clamp 到内容范围）在 `WidgetSet::scroll_by()` 里（见 [widgets.md](widgets.md) 的 `kScrollView`）。

#### 裁剪溢出内容

`NodeStyle::overflow = Overflow::kClip` 同时控制三件事：绘制（子节点超出部分不画）、命中测试（`hit_test()` 在裁剪边界外直接判定为未命中，被裁掉的像素不可点击）、以及 damage（脏矩形不会跑到裁剪区域外）。这三者共用同一条规则，不是三份独立实现。裁剪矩形是节点自身的**边框盒**（border box），不是 CSS 的 padding box——这是对 CSS 的刻意偏离，因为渲染树本身不知道 padding 是什么。

#### 让一整组内容一起变透明

```cpp
NodeStyle group_style;
group_style.opacity = 0.5F;   // 整个子树作为一张图片淡出，不是逐个子节点单独设 alpha
```

`opacity` 是**组透明度**，走 `SkCanvas::saveLayer`：整个子树先合成到一张离屏图片上解决内部重叠，再把这张图片整体按透明度画出来。这和给每个子节点的 fill/border/text 颜色分别设 alpha（逐对象透明度）效果不同——后者会让重叠的子节点互相透过彼此，颜色会变暗。`opacity == 1` 不创建离屏层（免费），`opacity == 0` 直接跳过整个子树的绘制（和合成后 alpha=0 效果相同,但不必真的创建层）。**命中测试完全忽略 `opacity`,包括 0**——一个淡出到看不见的按钮仍然可以被点击,这是有意的,因为透明度是连续动画量,没有天然的截断点。

## 边界与坑

- **没有 intrinsic sizing**：auto 尺寸的容器取子节点的自然尺寸之和，但不会投机地二次测量子树。详见 `doc/limitations.md`。
- **`grow`/`shrink` 只是整数权重/仅对声明的 basis 生效**：见上文，没有隐式的「按测量尺寸自动收缩」。
- **`main_align`/`cross_align` 覆盖不全**：`MainAlign` 有 `kStart`/`kCenter`/`kEnd`/`kSpaceBetween`/`kSpaceAround`/`kSpaceEvenly`；`CrossAlign`/`align_self` 有 `kStart`/`kCenter`/`kEnd`/`kStretch`，**缺 baseline 对齐**；容器方向通过 `LayoutKind`（`kRow`/`kColumn` 等）固定，**没有 reverse 变体**（`row-reverse` 之类不存在）。
- **百分比只有六个尺寸字段**，参照轴无界时退化为 auto，见上文。
- **滚动偏移不触发重排**：这是设计，不是 bug；但也意味着「滚动到某处后容器尺寸变了」不会自动重新计算——你需要在下一次真正的属性变更时才会重排。

以上边界完整版见 `doc/limitations.md` 对应条目。

## 相关 example

- `examples/04_layout` —— row/column/wrap/absolute 的基本用法，含 `layout_check.cpp` 的一致性校验
- `examples/09_sizing` —— grow/shrink/basis/aspect_ratio
- `examples/10_scrolling` —— 手搭 `kScrollView`：`scroll_axis` + `set_scroll_offset()`（运行时里直接用 `NodeKind::kScrollView`）
- `examples/07_clipping` —— `Overflow::kClip` 对绘制/命中测试/damage 的联合影响
- `examples/08_opacity` —— 组透明度 vs 逐对象 alpha 的对比
- `examples/29_enterprise` —— 运行时：百分比/`grow` 撑满的主从布局、`visible` 收起详情面板
