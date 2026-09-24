# 布局

## 这个子系统解决什么问题

drawgui 没有 HTML/CSS 那样的多遍回流（reflow）。它用的是 Flutter 风格的单遍协议：父节点向下传递约束（constraints），子节点向上报告尺寸（size）。每个节点在一次布局中只被测量一次。

这个协议本身不是这个项目发明的，但这里对它做了一处刻意的偏离：**约束和尺寸都是整型设备像素，不是浮点逻辑像素**。原因是 damage（脏矩形）系统要求「一个节点声明的矩形，必须和它实际画的矩形完全相同」。浮点布局总要在某处取整，而节点左边缘从 10.4 滑动到 10.6 时，取整结果会从 10 跳到 11——这一帧里 damage 矩形和实际绘制矩形就会错开一个像素。整型布局从根上排除了这类问题，代价是 DPI 变化会触发一次重新布局而不仅仅是重绘（但 DPI 变化本来就会改变帧缓冲尺寸从而强制全量重绘，所以这个代价基本是免费的）。

第二个偏离是：**没有 intrinsic sizing**（内在尺寸测量，比如「这段文字/这组子节点自己需要多宽」）。intrinsic sizing 需要投机性地测量子树,可能导致同一节点被布局不止一次，且最坏情况下是 O(n²)。这个内核选择完全不提供它——一个 Row 通过给子节点一个无界（unbounded）主轴约束来获取子节点的自然尺寸，这仍然是普通的一遍布局，不是第二遍。这是个硬边界：**没有「自动收缩到内容大小」这种事**，除非你自己测量好再声明尺寸（文本的 wrap 高度就是这样处理的，见 `doc/guide/text.md`）。

参考实现：`include/drawgui/layout/box.h`（约束、样式的定义）与 `include/drawgui/layout/layout_tree.h`（布局树、增量布局）。

## 核心类型和它们的关系

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

## 常见任务怎么做

### 建立一棵树并做首次布局

用 `LayoutTree` 而不是直接用 `RenderTree`——`LayoutTree` 内部会创建并拥有一个 `RenderTree`，`render()` 拿到的就是它。首次布局用 `layout_full()`（忽略所有缓存，全量布局），之后每次改动只调用 `layout()`（只重排脏的部分）。参见 `examples/04_layout/layout_scene.cpp`。

### 用 flex（grow / shrink / basis）分配剩余空间

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

### 让容器在放不下时自动换行

```cpp
BoxStyle wrap;
wrap.kind = LayoutKind::kWrapRow;
wrap.run_gap = 8;                          // 相邻「行」之间的间距（跨轴）
wrap.align_content = AlignContent::kSpaceBetween;  // 多行整体在跨轴上的分布
```

`run_gap` 只对多行/多列生效——单行容器没有第二行可分隔。`align_content` 决定「行」这个整体怎么摆放，和决定单个子节点在自己所在行里怎么对齐的 `cross_align`是两码事。

### 让内容滚动

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

**滚动偏移是运行时状态，不是属性**——这是刻意设计。`set_scroll_offset()` 只重新计算子节点的绝对坐标和裁剪边界，从不调用 `measure()`，所以一次滚轮滚动的代价是一次重绘，永远不是一次重排。真正可用的示例见 `examples/10_scrolling/`；实际的封装（clamp 到内容范围）在 `WidgetSet::scroll_by()` 里（见 `doc/guide/widgets.md` 的 `kScrollView`）。

### 裁剪溢出内容

`NodeStyle::overflow = Overflow::kClip` 同时控制三件事：绘制（子节点超出部分不画）、命中测试（`hit_test()` 在裁剪边界外直接判定为未命中，被裁掉的像素不可点击）、以及 damage（脏矩形不会跑到裁剪区域外）。这三者共用同一条规则，不是三份独立实现。裁剪矩形是节点自身的**边框盒**（border box），不是 CSS 的 padding box——这是对 CSS 的刻意偏离，因为渲染树本身不知道 padding 是什么。

### 让一整组内容一起变透明

```cpp
NodeStyle group_style;
group_style.opacity = 0.5F;   // 整个子树作为一张图片淡出，不是逐个子节点单独设 alpha
```

`opacity` 是**组透明度**，走 `SkCanvas::saveLayer`：整个子树先合成到一张离屏图片上解决内部重叠，再把这张图片整体按透明度画出来。这和给每个子节点的 fill/border/text 颜色分别设 alpha（逐对象透明度）效果不同——后者会让重叠的子节点互相透过彼此，颜色会变暗。`opacity == 1` 不创建离屏层（免费），`opacity == 0` 直接跳过整个子树的绘制（和合成后 alpha=0 效果相同,但不必真的创建层）。**命中测试完全忽略 `opacity`,包括 0**——一个淡出到看不见的按钮仍然可以被点击,这是有意的,因为透明度是连续动画量,没有天然的截断点。

## 边界与坑

- **没有 intrinsic sizing**：容器不会「收缩到内容大小」，除非你自己测量。详见 `doc/limitations.md`。
- **`grow`/`shrink` 只是整数权重/仅对声明的 basis 生效**：见上文，没有隐式的「按测量尺寸自动收缩」。
- **`main_align`/`cross_align` 覆盖不全**：`MainAlign` 有 `kStart`/`kCenter`/`kEnd`/`kSpaceBetween`，**缺 `space_around`/`space_evenly`**；`CrossAlign`/`align_self` 有 `kStart`/`kCenter`/`kEnd`/`kStretch`，**缺 baseline 对齐**；容器方向通过 `LayoutKind`（`kRow`/`kColumn` 等）固定，**没有 reverse 变体**（`row-reverse` 之类不存在）。
- **滚动偏移不触发重排**：这是设计，不是 bug；但也意味着「滚动到某处后容器尺寸变了」不会自动重新计算——你需要在下一次真正的属性变更时才会重排。

以上边界完整版见 `doc/limitations.md` 对应条目。

## 相关 example

- `examples/04_layout` —— row/column/wrap/absolute 的基本用法，含 `layout_check.cpp` 的一致性校验
- `examples/09_sizing` —— grow/shrink/basis/aspect_ratio
- `examples/10_scrolling` —— `scroll_axis` + `set_scroll_offset()`
- `examples/07_clipping` —— `Overflow::kClip` 对绘制/命中测试/damage 的联合影响
- `examples/08_opacity` —— 组透明度 vs 逐对象 alpha 的对比
