# 属性参考

权威来源：`props/drawgui.props.toml`（唯一真源，`tools/gen_props.py` 从它生成
`include/drawgui/render/prop_ids.generated.h` 与 `src/render/prop_dispatch.generated.inc`）。
本文档中的状态列由人工核对 `src/props/node_props.cpp` 得出，如与源码不符，以
源码为准。

## 1. 基本模型

一个属性通过 `dg::set_prop(tree, node, prop_id, PropValue)` 写入（C ABI 对应
`dg_node_set_prop`）。五种复杂类型（gradient/shadow/transform/image/text）不走这条
标量入口，见第 4 节。

`prop_id` 永远是裸 `uint16_t`，从不被转换成枚举——校验本身就是分发过程，未知
id 由生成的 `switch` 默认分支决定，而不是一次范围检查后的 cast。

**id 永不复用**：`props/drawgui.props.toml` 的 `meta.next_id`
只增不减；删除一个属性只留下空位和注释，重新编号是 MAJOR 版本破坏
（`props/prop_ids.lock` 机制性地钉住这条规则，而非靠自觉）。

## 2. `PropStatus`（六值）

| 值 | 何时出现 |
|---|---|
| `kApplied` | 写入成功并生效 |
| `kUnknownId` | id 是 0（`DG_PROP_INVALID`）或超出已定义范围 |
| `kTypeMismatch` | id 合法，但值的类型与该属性声明的类型不符（例如把颜色发给 `width`）；也用于**已实现但要求走专用 setter** 的属性（`background_gradient`/`shadow`/`image_source`/`text`）——通过标量入口写它们，得到的正是这个状态，而不是 `kUnsupported` |
| `kValueOutOfRange` | 类型对但值不可用：非有限浮点数、负长度、超出 `values` 范围的枚举序号、`grow`/`shrink` 收到非整数权重等 |
| `kNotApplicable` | 属性合法、值合法，但对**这个**节点不适用（叶子节点上的 `gap`，父节点不是 flex/wrap 容器时子节点上的 `grow`）。判定依据是 toml 的 `applies_to`/`consumed_by` |
| `kUnsupported` | 引擎目前未实现该能力（与调用方无关，是 drawgui 自身的缺口）；同一次写入在能力补齐后会变成 `kApplied` |

`PropWrite { status, message }`：失败时 `message`带节点路径；写入失败不会让节点
处于半配置状态——applier 先在 `BoxStyle`/`NodeStyle` 的副本上操作，成功才提交。

## 3. `PropValue`（五种标量载荷）

`PropValue` 是一个带类型标签的标量，只能通过以下工厂构造：

| 工厂函数 | 对应 `PropType` | 说明 |
|---|---|---|
| `PropValue::number(float)` | `k_float` | 普通浮点数 |
| `PropValue::length(float)` | `k_length` | 绝对长度（逻辑像素，四舍五入到整数设备像素） |
| `PropValue::percent(float)` | `k_length` | 百分比（50 = 50%，范围 0..1000），仅 `width`/`height`/`min_*`/`max_*` 接受。单位放在标量联合体的整数半边：`bits == 0` 像素、`bits == 1` 百分比，其他值返回 `kValueOutOfRange`。参照物是父节点该轴的可用内容盒，参照无界时按未设置处理（见 `doc/guide/layout.md`）。C ABI 对应形状：`DG_VALUE_LENGTH` + `bits` 作单位（ABI 侧映射由后续工作接入） |
| `PropValue::color(Color)` | `k_color` | 0xAARRGGBB，边界处非预乘 |
| `PropValue::option(uint32_t ordinal)` | `k_enum` | 属性 `values` 列表的下标；顺序本身是 ABI 契约（生成的 `DG_<PROP>_<VALUE>` 常量就是这个序号） |
| `PropValue::boolean(bool)` | `k_bool` | 0/1，放在整数半边；C ABI 为 `DG_VALUE_BOOL`，`bits` 不是 0/1 时返回 `kValueOutOfRange`（`PropValue::boolean_bits()` 用于原样转交 ABI 传入的位） |

没有能构造 `k_gradient`/`k_shadow`/`k_transform`/`k_image`/`k_text` 的工厂——因此任何
针对这五个属性的标量写入天然走错入口。

## 4. 为什么复杂类型走专用 setter

`gradient`/`shadow`/`transform`/`image`/`text` 五种类型的数据没有共同大小
（渐变的 stop 列表、阴影的四个标量、变长 UTF-8 字符串……），塞进一个带堆分配 variant 的
`dg_value` 会破坏这个联合体从不堆分配的约定。因此它们各自有独立入口：

```
dg::set_gradient(tree, node, prop_id, const LinearGradientStyle&)
dg::set_shadow  (tree, node, prop_id, const ShadowStyle&)
dg::set_image   (tree, node, prop_id, const ImageStyle&)
dg::set_transform(tree, node, prop_id, const TransformDesc&)  // 见下
dg::set_text     (tree, node, prop_id, std::string_view)  // C ABI: dg_node_set_text
```

五个函数仍然使用与标量路径**相同**的 `dg_prop_id`、相同的
`props/drawgui.props.toml` 条目、相同的 `prop_type()` 校验——不是第二套编号
体系。发错 id（例如把 `DG_PROP_BACKGROUND_GRADIENT` 发给 `set_shadow`）会被
拒绝，机制与标量路径把 `DG_PROP_WIDTH` 当颜色发送时一样。

`set_transform` **始终返回 `kUnsupported`**：`TransformDesc`
（`translate_x/y`、`scale_x/y`、`rotate_deg`、`origin_x/y`）这个签名本身已
确定，但背后的能力没有——引擎的矩形都是轴对齐整数设备像素（damage、hit
test、clip 全部如此），通用 2D 变换会把每一个都变成非轴对齐四边形或矩阵乘法，
这是三个子系统需要同时改变的事，不是拆开做的事。这是**主动声明的搁置**，
不是半成品。

`background_gradient`（渐变）、`shadow`（阴影）、`image_source`（图片源）、`text`（文字）四个
属性其实**已实现**，只是不走标量入口——用标量 `set_prop` 写它们，会得到
`kTypeMismatch`，消息里明确说明"请改用 `set_gradient`/`set_shadow`/`set_image`"。

## 5. 属性表（59 项）

列说明：**外部名**是 `props/drawgui.props.toml` 的 `name`（即 `dg_prop_id`
常量与文档、setter 里出现的那个词汇）；**内部字段**是
`src/props/node_props.cpp`/`BoxStyle`/`NodeStyle` 实际写入的成员——两者**不总
是同名**，尤其 `justify`→`main_align`、`align`→`cross_align`（见 5.1 节说明）。

### 5.1 `justify`/`align` 的命名映射（易混淆点）

toml 里 `justify`（id 32）、`align`（id 33）两个属性的 `field` 元数据分别写
的是 `"justify"`/`"align"`，但这只是文档性字段——`apply_justify()`/
`apply_align()` 实际写入的是 `BoxStyle::main_align`/`BoxStyle::cross_align`
（`src/props/node_props.cpp:535-591`）。也就是说：

- **外部（属性 id、ABI、你调用 `set_prop` 时用的名字）**：`justify`、`align`
- **内部（C++ 结构体成员，仅在阅读引擎源码时会遇到）**：`main_align`、`cross_align`

使用者只需要认识 `justify`/`align`；只有在读布局源码、或看到诊断信息里出现
`main_align`/`cross_align` 时才需要知道这层映射。

### 5.2 box_model（id 1-15）

| id | 名称 | 类型 | 状态 | 说明 |
|---|---|---|---|---|
| 1 | `width` | length | 完整 | 边框盒宽度；缺省即收缩到内容尺寸；可为百分比 |
| 2 | `height` | length | 完整 | 边框盒高度；缺省即收缩到内容尺寸；可为百分比 |
| 3 | `min_width` | length | 完整 | 尺寸源解出结果后应用的下限；可为百分比 |
| 4 | `max_width` | length | 完整 | 尺寸源解出结果后应用的上限；可为百分比 |
| 5 | `min_height` | length | 完整 | 同上，高度方向；可为百分比 |
| 6 | `max_height` | length | 完整 | 同上，高度方向；可为百分比 |
| 7 | `aspect_ratio` | float | 完整 | width/height；从一个维度推出另一个 |
| 8-11 | `padding_l/t/r/b` | float | 完整 | 缩小传给子节点的约束 |
| 12-15 | `margin_l/t/r/b` | float | **部分** | 见下 |

`margin_*`（parentData，`parent_data_scope="base"`）：仅支持非负值。负值会
"移动子节点但不归还空间"——`place_flex_children` 直接把 margin
加到位置上，deflate（缩小约束）却没有对称处理负值。这是记录在案的功能缺口，
不是隐藏 bug（`src/props/node_props.cpp:355-357` 注释原文）。

### 5.3 visual（id 16-30, 47-51, 55-56）

| id | 名称 | 类型 | 状态 | 说明 |
|---|---|---|---|---|
| 16 | `background_color` | color | 完整 | 边框盒填充，0xAARRGGBB 非预乘 |
| 17 | `background_gradient` | gradient | 完整 | 仅线性渐变，≥2 个 stop；**专用 setter**（`set_gradient`），标量入口返回 `kTypeMismatch` |
| 18-21 | `border_width_l/t/r/b` | float | 完整 | 边框盒内侧厚度 |
| 22 | `border_color` | color | 完整 | 四条边共用颜色 |
| 23-26 | `border_radius_tl/tr/br/bl` | float | 完整 | 裁剪所有装饰层 |
| 27 | `opacity` | float | 完整 | 0..1，非该范围内的值（含非有限数）被拒绝而非裁剪；是**子树整体透明度**，不是逐对象 alpha |
| 28 | `shadow` | shadow | 完整 | 外阴影；**专用 setter**（`set_shadow`）；`blur_radius` 有硬性上限（实测占单帧光栅时间 52%，该测量记录在被移除的开发切片文档中，见 git 历史），是预算控制而非单纯范围检查 |
| 29 | `overflow` | enum(`visible`/`clip`) | 完整 | 是否裁剪超出边框盒的内容 |
| 30 | `transform` | transform | **未实现** | setter（`set_transform`）存在、id 校验正确，**永远返回 `kUnsupported`**：引擎所有矩形都是轴对齐整数设备像素，通用 2D 变换需要 damage/hit-test/clip 三个子系统同时改变 |
| 47 | `image_source` | image | 完整 | 选择哪个已解码的 `ImageCatalog` 条目；**专用 setter**（`set_image`） |
| 48 | `image_fit` | enum(`fill`/`contain`/`cover`/`none`) | 完整 | 走普通标量路径（不是专用 setter） |
| 49 | `image_placeholder_color` | color | 完整 | 图片未解码完成前的占位色，绝不是"洞" |
| 50 | `text` | text | 完整 | label/button 等绘制的 UTF-8 字符串；**专用 setter**（`set_text`，C ABI 为 `dg_node_set_text`），标量入口返回 `kTypeMismatch`；字体族不是属性，取引擎默认值（字号/对齐见 55/56） |
| 51 | `text_color` | color | 完整 | `TextStyle::color`，走普通标量路径；默认全透明，只设 `text` 不设它则看不见文字 |
| 55 | `font_size` | float | 完整 | `TextStyle::size`，逻辑像素，须为正的有限数；不改变节点盒子大小 |
| 56 | `text_align` | enum(`left`/`center`/`right`) | 完整 | `TextStyle::align`，序号即 `DG_TEXT_ALIGN_*`。没有字重属性（`TextStyle` 没有字重） |

### 5.4 container（id 31-37, 46, 54, 59）

| id | 名称 | 类型 | 状态 | 说明 |
|---|---|---|---|---|
| 31 | `direction` | enum(`row`/`column`/`row_reverse`/`column_reverse`) | **部分** | `row_reverse`/`column_reverse` 返回 `kUnsupported`：`size_flex_children` 按固定顺序遍历子节点，反转顺序会改变谁吸收整数除法的余数 |
| 32 | `justify` | enum(6 值) | 完整 | 6 个值全部实现；`space_around`/`space_evenly` 用累计份额整数分配。内部字段是 `main_align`，见 5.1 |
| 33 | `align` | enum(5 值) | **部分** | `baseline` 返回 `kUnsupported`：需要子节点排布前先知道其基线，属于故意缺失的内在尺寸测量。`wrap` 容器下 `stretch` 会退化。内部字段是 `cross_align`，见 5.1 |
| 34 | `gap` | float | 完整 | 主轴上相邻子节点间距，叠加在 margin 之上 |
| 35 | `main_size` | enum(`min`/`max`) | 完整 | 容器是收缩贴合内容还是撑满主轴 |
| 36 | `run_gap` | float | 完整 | 仅 `RenderWrap` 有意义，用于换行后行间距 |
| 37 | `align_content` | enum(6 值) | 完整 | 仅 `RenderWrap`；行堆叠方向的交叉轴对齐 |
| 46 | `scroll_axis` | enum(`none`/`vertical`/`horizontal`) | 完整 | 仅限叶子节点（`kLeaf`）；决定该节点给子节点哪个轴的无界约束 |
| 54 | `scrollbar` | enum(`auto`/`always`/`never`) | 完整 | `NodeStyle::scrollbar`，写在挂了滚动条的 `kScrollView`/`kList` 节点上。序号即 `DG_SCROLLBAR_*`：`auto`(0，默认，内容溢出才显示)、`always`(1)、`never`(2，隐藏但照常滚动)。不是绘制输入，写入不重排；在下一次 `WidgetSet::sync_scrollbar()` 时生效（C++ 调用方写完要自己调）。见 `doc/guide/widgets.md`「滚动条」 |
| 59 | `wrap` | bool | 完整 | 行/列容器是否换行：`true` 变成 `kWrapRow`/`kWrapColumn`，`false` 变回，保持轴向；只对行/列/换行容器（叶子返回 `kNotApplicable`） |

### 5.5 flex（parentData，id 38-41）

| id | 名称 | 类型 | 状态 | 说明 |
|---|---|---|---|---|
| 38 | `grow` | float(整数权重) | **部分** | 只接受非负整数（分配自由空间是精确整数除法，小数权重在重新布局后没有稳定含义）；`wrap` 容器下返回 `kNotApplicable`；父节点非 flex/wrap 时也是 `kNotApplicable` |
| 39 | `shrink` | float(整数权重) | **部分** | 同样要求非负整数。**故意允许写入即使当前无法生效**：`shrink` 需要一个已声明的基准（`basis` 或容器主轴上的确定尺寸），而 `shrink`/`basis` 写入顺序不固定，若在此处拒绝会造成"先写 `shrink` 再写 `basis` 失败、反过来却成功"的不对称；真正的拒绝发生在布局阶段（`size_flex_children`），带节点路径 |
| 40 | `basis` | float | 完整 | 主轴基准尺寸；缺省即子节点自然尺寸。**无法被清除**：toml 没有为它定义 `auto` 序号 |
| 41 | `align_self` | enum(6 值，含 `auto`) | **部分** | `baseline` 返回 `kUnsupported`（同 `align`）；`auto` 是"清除覆盖"，不是第五种对齐值；`stretch` 在写入时被接受，但若父容器是 `wrap` 会在布局时退化为 `start`（因为答案依赖父节点种类，而子节点的 `align_self` 可能先于父节点类型确定而写入） |

### 5.6 stack（parentData，id 42-45）

| id | 名称 | 类型 | 状态 | 说明 |
|---|---|---|---|---|
| 42-45 | `left`/`top`/`right`/`bottom` | float | 完整 | 相对 stack 内容盒的偏移；写入任一个即视为定位该子节点 |

### 5.7 interaction（id 52-53, 57-58, 60-61）

| id | 名称 | 类型 | 状态 | 说明 |
|---|---|---|---|---|
| 52 | `disabled` | bool | 完整 | `NodeStyle::disabled`。禁用本节点**及其整棵子树**的 hover/press/click/拖动/文本编辑与焦点（`dg::is_disabled()` 沿父链判断）；写入只产生重绘，不重排。C++ 调用方写完需调用 `cancel_disabled()`/`blur_if_disabled()`/`refresh_subtree()`，C ABI 的 `dg_node_set_prop()` 已自动完成。行为细节见 `doc/guide/widgets.md`「禁用状态」 |
| 53 | `cursor` | enum | 完整 | `NodeStyle::cursor`，鼠标指针形状。取值（序号即 `DG_CURSOR_*`）：`auto`(0，默认)、`default`(1)、`pointer`(2，手形)、`text`(3，I 形)、`crosshair`(4)、`move`(5)、`not_allowed`(6)、`wait`(7)、`progress`(8)、`resize_ew`(9)、`resize_ns`(10)、`resize_nwse`(11)、`resize_nesw`(12)，一一对应 SDL 的系统光标。`auto` 表示继承：从指针下的节点沿父链找第一个非 `auto` 的值，途中遇到有自己默认值的控件（目前只有文本框 → `text`）就用它。不是绘制输入，写入不重排。解析规则见 `doc/guide/input.md`「鼠标指针形状」 |
| 57 | `tab_index` | float(整数) | **仅 C ABI** | `Widget::tab_index`：正数按升序先走，0 按树序，负数退出 Tab 序列。是控件状态不是节点样式，`dg::set_prop()` 返回 `kUnsupported`（C++ 直接写 `Widget::tab_index`），C ABI 的 `dg_node_set_prop()` 自己处理 |
| 58 | `max_length` | float(整数) | **仅 C ABI** | 编辑框最多多少个字素簇，0 = 不限；同上，C++ 用 `WidgetSet::editor_set_max_length()` |
| 60 | `value` | float | **仅 C ABI** | `DG_NODE_TYPE_PROGRESS` 的进度 0..1（0.2 phase B）；组件状态，C++ 用 `dg::ProgressBar::set_value()` |
| 61 | `indeterminate` | bool | **仅 C ABI** | `DG_NODE_TYPE_PROGRESS` 显示不确定动画；C++ 用 `dg::ProgressBar::set_indeterminate()` |

## 6. 状态汇总（自查结果）

- 46 项完整、10 项部分实现、4 项只在 C ABI 生效（`tab_index`/`max_length`/
  `value`/`indeterminate`）、1 项未实现（`transform`），共 61 项（含 P6 a1 新增的 `text`/`text_color`，
  以及 `disabled`、`cursor`、`scrollbar`、`font_size`、`text_align`、`wrap`）。
- 与最初给出的表格核对下来内容基本吻合，本文档在此基础上补充了：
  `justify`/`align` 与内部字段名 `main_align`/`cross_align` 的映射说明
  （5.1 节），以及 `grow`/`wrap` 场景的 `kNotApplicable`（5.5 节）。

## 7. 相关文档

- ABI 侧如何暴露属性写入：`doc/reference/c-abi.md`
- 复杂属性（渐变/阴影/图片/变换）为何走专用 setter 而非标量通道：`include/drawgui/props/node_props.h` 中 `set_gradient()`…`set_text()` 声明附近的注释
- 阴影模糊半径预算的实测数据：该测量记录在被移除的开发切片文档中，见 git 历史
