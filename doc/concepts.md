# 核心概念

这份文档讲引擎里几个不符合常规 GUI 框架直觉的设计。按常规框架的假设去用，
会在这几个地方撞墙——提前知道比调试时才发现要省时间。

## 1. 两棵树，不是三棵

常规框架的心智模型往往是「widget 树 → 渲染树 → 屏幕」三层。drawgui 只有
两棵：`dg::LayoutTree` 内含一棵 `dg::RenderTree`——两者绑在一起，前者驱动
后者的布局。**没有 widget 树。**

`dg::WidgetSet` 不是一棵树，是一张按 `NodeId` 索引的侧表：
`attach(NodeId, const Widget&)`。使用者要自己在 `RenderTree` 里建节点、
建子节点，再给某个 `NodeId` 附加一个 `Widget` 描述符（按钮的文本、slider
的取值范围之类）。**没有 `createButton()` 这种东西**——按钮是「一个
`RenderTree` 节点 + 一条 `WidgetSet` 记录」的组合，不是一个独立的类。

细节和取舍见 `doc/guide/widgets.md`。

**后续更新：在这两棵树之上还有一层 C++ 运行时。** `dg::App`/`dg::Window`
（公开头文件 `include/drawgui/app/`，实现 `src/app/`）把「建节点 + 附加
`Widget` + 默认样式 + 组件组合 + 事件泵 + 绘制」打包成
`window.create(dg::NodeKind::kButton, parent)` 这样的调用，C ABI（`src/abi/`）
只是它上面的一层薄壳。所以分层是：`render`/`layout`/`widget`/`ui`/`window`
等底层模块 → `app` 运行时 → `abi`。运行时内部仍然是上面描述的「两棵树 +
侧表」，没有引入 widget 树；需要时可以经 `dg::Window` 的 escape hatch
（`layout()`、`widgets()` 等）直接拿到它们，约定见
[`doc/contracts.md`](contracts.md) 第 9 条。参考见 `doc/guide/app.md`。

## 2. 侧表模式

`WidgetSet` 不是特例，是这个引擎的通用模式。以下都是按 `NodeId` 索引的
侧表，而不是节点自身的字段：

- `dg::WidgetSet` —— 节点的 widget 描述符
- `dg::Focus` —— 节点是否可聚焦、Tab 顺序
- `dg::Interaction` —— 节点的 hover/press 状态
- `dg::ActionScopes` —— 节点绑定的快捷键作用域
- `dg::ThemeBindings` —— 节点的哪些属性跟随哪个主题 token
- `dg::AnimationEngine` —— 节点上正在跑的动画

好处是节点本身保持精简；代价是删除一个节点时，要记得同时清理每一张表。
`dg::remove_subtree()`（`include/drawgui/render/node_lifecycle.h`）就是干这件
事的——先收集整棵子树（因为 `remove_child()` 会立即断开父子链接，之后再
遍历就晚了），再挨个从六张表里 `forget()`/`cancel_all_for()`。它是删除节点的
**唯一**途径：`RenderTree::remove_child()`/`LayoutTree::remove_child()` 需要一个
只有 `remove_subtree()` 能构造的 `RemovalKey`，手写的裸删除编译不过；六张表以
`dg::NodeSideTables` 的引用成员传入，漏写一张同样编译不过。

具体实现见 `src/render/node_lifecycle.cpp`。

## 3. 属性是数字，不是字段

节点的视觉/布局属性不是 C++ 结构体字段，而是通过一个 `uint16_t`
（`dg_prop_id`）寻址的通道：`set_prop(node, id, PropValue)`。目前有 68 个
属性，从 `DG_PROP_WIDTH`(1) 到 `DG_PROP_IMAGE_TINT`(68)
（`include/drawgui/abi/drawgui.h` 里的 `DG_PROP_*` 宏）。

这么做是为了 ABI 稳定：id 从 `props/drawgui.props.toml` 生成，被
`props/prop_ids.lock` 钉死——**一个 id 一旦发布就不能挪作他用**，哪怕它
后来被废弃，也只能追加新 id，不能回收旧的（因为 C ABI 消费者已经把这个
数字编译进了自己的二进制）。

复杂类型——渐变、阴影、图片、变换——不走这条标量通道，而是有专用 setter
（比如设置渐变要用专门的复杂属性接口，而不是塞进 `PropValue`）。

细节见 `doc/reference/properties.md`。

## 4. damage 驱动的局部重绘

`RenderTree` 自己跟踪脏区（`DamageRegion`），`repaint()` 只重新光栅化脏
矩形覆盖的区域，不是整帧重画。

`DamageRegion` 对矩形数量有上限（`TreeSpec::max_damage_rects`，默认 8，
`include/drawgui/render/damage.h:39`、`render_tree.h:554`）。超过这个数量
时，多块脏矩形会被合并成更大的一块——这会导致「明明只改了一个像素，
观察到的却是半屏重绘」。**这是设计好的行为，不是 bug**：合并成一块矩形
比维护任意多块矩形要便宜，超过阈值时"过度重绘一点"换"damage 追踪本身
不失控"是这里做的取舍。

细节见 `doc/guide/layout.md`。

## 5. exactly-once layout（及其例外）

布局每帧最多跑一次。`LayoutStats::nodes_visited == 0` 在这些操作里成立
（有测试断言）：滚动、拖拽 slider、打字、列表虚拟化回收、颜色 token 切换、
焦点变化。也就是说，这些交互不会触发重新布局，只触发重绘。

**例外：整数型 token（`radius`/`space` 一类）的主题热重载必然触发重排。**
`examples/24_theme_package/theme_package_check.cpp` 对这种场景断言的是
`nodes_visited != 0`，跟上面那条正好相反——因为这类 token 直接影响盒子
尺寸，不重排就是错的。颜色 token 之所以能做到 `== 0`，是因为颜色只影响
绘制，不影响几何。

如果你的调用方观察到「改主题却触发了 layout」，先确认改的是不是数值型
token，如果是，这是预期行为。

## 6. 文本不参与布局

`LayoutTree` / `RenderTree` 本身不知道「文本」这个概念的高度会随内容变化。
`TextStyle::wrap`（多行换行）的实际高度，需要调用方**自己先用
`dg::Paragraph::build()` 测量出来**，再拿这个测量结果去构造节点——而不是
先建节点、让引擎去反算文本占多高。

依据：`include/drawgui/render/render_tree.h:115-123` 附近的注释——这是有意
的设计，跟 `ImageStyle` 的处理方式一致：一个盒子必须先知道自己的尺寸，
内容才能在这个尺寸内被解析，顺序不能反过来。

细节见 `doc/guide/text.md`。

## 7. 整数设备像素

布局系统使用整型设备像素，不是浮点逻辑像素。这是对常见 GUI 框架惯例
（浮点逻辑像素 + DPI 缩放）的有意偏离——整数坐标让 damage 矩形的边界是
精确的，不需要处理浮点误差导致的一像素缝隙或重叠。代价是不能直接表达
非整数缩放；缩放要在坐标进入布局系统之前就折算成设备像素整数。

细节见 `doc/guide/layout.md`。

## 8. 零 virtual

`src/` 和 `include/` 全部代码里没有一个 `virtual`/`override` 用作 C++
运行时多态（已用 `grep -rn` 核实：唯二命中的是标识符名字如
`dg_theme_override`、注释里提到"override"这个词，没有一处是虚函数声明）。
多态一律靠 `switch` 在少数几个已知类型（`WidgetKind`、`dg_node_type` 等）
上分发，不是靠抽象基类 + 虚函数。

`include/drawgui/widget/widget_set.h:50` 附近的注释记录了被拒绝的方案：
一个带虚 `paint()` 的抽象 `IWidget` interface——这类"提前抽象"在这个项目
里的一贯做法是拒绝，接口只从至少一个已经跑起来的实现里提炼，不会先写
接口再补实现。

## 下一步

- 想知道循环顺序为什么是那样、还有哪些"必须遵守但代码不强制"的约定：
  [`doc/contracts.md`](contracts.md)
- 具体某个主题的用法（layout/text/widgets/theming/input/popups/animation）：
  [`doc/guide/`](guide/)
- 属性 id、token id、action id、C ABI 函数的完整清单：
  [`doc/reference/`](reference/)
- 已知的能力边界（比如 `transform` 属性的限制）：
  [`doc/limitations.md`](limitations.md)
