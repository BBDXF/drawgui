# 文本

## 这个子系统解决什么问题

drawgui 内核没有把文本当作特殊节点类型，而是当作 `NodeStyle::text`（`TextStyle`）这一个字段——和 fill、border、image 平级。这样做的好处是绘制、命中测试、damage 只需要处理一种节点结构；代价是文本子系统和布局子系统几乎没有耦合：**LayoutTree 完全不知道字体、字符串这些概念**，`BoxStyle` 里没有任何字段涉及文本测量。这直接导致一个后果，也是这份文档要讲清楚的最容易踩的坑：**多行/自动换行文本的高度必须由调用方自己测量好，再拿去建节点**，内核不会帮你算。

字体这边同样有一个容易踩空的默认行为：`FontCatalog` 里如果没有覆盖某个字族/某个语言的字体，绘制会**静默地什么都不画**，而不是报错或者退化成 tofu 方框。这是刻意的——「忘了设字体」和「设了字体但这台机器上没有」在视觉上是同一个失败：什么都没画出来,而不是悄悄换成排在最前面的某个字体。

参考实现：`include/drawgui/render/render_tree.h` 的 `TextStyle`、`include/drawgui/render/font_catalog.h`、`include/drawgui/render/paragraph.h`、`include/drawgui/render/grapheme.h`、`include/drawgui/widget/widget_set.h` 的 `kTextField` 一族。

## 核心类型和它们的关系

- `dg::FontCatalog`：一张可共享（引用计数）的字体表。`scan(dir)` 扫描目录建表，`add(family, bold)` 注册一个具名字族并返回 `FontId`，`resolve(...)` 回答「这个字/这个码点该由哪个字族画」。
- `dg::FontId`：`{value}`，0 表示无效。
- `dg::TextStyle`：一次文本绘制的全部参数——`text`、`font`、`language`（BCP 47）、`size`、`color`、`align`、`wrap`、`max_lines`。
- `dg::Paragraph`：`wrap = true` 时用来**提前测量**高度的类型，不参与实际绘制路径（绘制仍由 `NodeStyle::text` 走）。
- `dg::WidgetSet` 的 `kTextField` 一族：单行可编辑文本框的状态和操作。
- `dg::grapheme_boundaries()`：按 UAX #29 扩展字位簇（extended grapheme cluster）切分字符串，是光标移动/删除的最小单位。

**建立 FontCatalog 必须在建任何文本节点之前完成**，且必须先 `add()` 一个具体字族才能用它。`add()` 对不存在的字族会直接返回失败（`Expected<FontId, FontError>`），绝不会静默换成别的字体：

```cpp
dg::Expected<dg::FontCatalog, dg::FontError> scanned = dg::FontCatalog::scan(font_dir);
if (!scanned.has_value()) { /* 处理扫描失败 */ }
dg::FontCatalog fonts = std::move(scanned).value();

dg::Expected<dg::FontId, dg::FontError> primary = fonts.add("Inter", /*bold=*/false);
if (!primary.has_value()) { /* 这台机器没有 Inter，失败要处理，不能假装成功 */ }
dg::FontId ui_font = primary.value();

dg::TreeSpec spec;
spec.viewport = {800, 600};
spec.fonts = fonts;          // 必须在 LayoutTree/RenderTree 构造前设好
dg::LayoutTree tree{spec};
```

## 常见任务怎么做

### 画一行不换行的文本

```cpp
dg::BoxStyle box;
box.width = 200;
box.height = 24;

dg::NodeStyle style;
style.text.text = "Hello";
style.text.font = ui_font;
style.text.size = 14.0F;
style.text.color = dg::Color::from_argb(0xFFEAEAEA);
style.text.align = dg::TextAlign::kCenter;   // 默认就是 kCenter

tree.add_child(parent, box, style);
```

`TextAlign` 只有 `kLeft`/`kCenter`/`kRight` 三个值，没有 `justify`（两端对齐）。`inset` 字段给左右对齐的文本留出前后边距，`kCenter` 时被忽略（本身已经居中）。

注意：**文本不使数节点自己收缩到内容大小**——节点的宽高仍然要按上面 layout.md 的规则声明,文本只是被放进这个已经定好的盒子里绘制,超出部分按 `text.wrap`/`max_lines` 处理,或者直接溢出（不裁剪,除非父节点设了 `Overflow::kClip`)。

### 让一段文本自动换行（wrap）——测量→建节点两步走

这是文本子系统里最容易出错的地方，务必按顺序做:

```cpp
using namespace dg;

TextStyle wrapped;
wrapped.text = long_paragraph;
wrapped.font = ui_font;
wrapped.language = "zh-Hans";     // 影响 CJK 汉字统一的取字，BCP 47
wrapped.size = 14.0F;
wrapped.color = Color::from_argb(0xFFEAEAEA);
wrapped.align = TextAlign::kLeft;
wrapped.wrap = true;
wrapped.max_lines = 0;            // 0 = 不限行数；正数会截断并加省略号

const float inner_width = 360.0F;  // 节点将来会有的内容宽度

// 第一步：在建节点之前，先测量出这个宽度下的高度
Expected<Paragraph, FontError> built = Paragraph::build(fonts, wrapped, inner_width);
int height = built.has_value() ? built.value().metrics().height : 0;

// 第二步：拿测出来的高度去声明节点尺寸，这时候才建节点
BoxStyle box;
box.width = static_cast<int>(inner_width);
box.height = height;

NodeStyle style;
style.text = wrapped;
tree.add_child(parent, box, style);
```

`Paragraph::build()` 是**不可变**的一次性测量：想用不同宽度，就再 `build()` 一次，不能原地重新排版。`ParagraphMetrics` 除了 `height` 还报告 `line_count` 和 `exceeded_max_lines`（是否被 `max_lines` 截断）。完整流程见 `examples/20_multiline_text/multiline_scene.cpp` 里的 `wrapped_height()` 函数。

### 让一段文本混合多种文字（拉丁字母、中文、emoji）

字体回退链是手工构建的，**不依赖 fontconfig**——同一个字族名可以覆盖 12 种脚本外加 emoji,不需要调用方为每种文字单独指定字体:

```cpp
std::vector<FontFallbackRule> rules = FontCatalog::default_fallback_rules();
fonts.set_fallback_rules(std::move(rules));  // 可选：替换成宿主自己的策略
```

命名的主字族覆盖不到的码点会走回退链；`language`（BCP 47 前缀匹配）决定选哪一条规则，这是解决中日韩汉字统一（同一码点在 zh-Hans/zh-Hant/ja/ko 里是不同字形）的唯一手段。`FontFallbackRule` 是纯数据表，没有 BCP 47 规范化算法——「zh-CN」不会自动变成「zh-Hans」，除非表里显式列出。

这条回退链**不需要 icudtl.dat**：字位簇切分用的是 libgrapheme 后端（`SkUnicodes::Libgrapheme::Make()`），不是 ICU。

### 单行文本编辑（text field）

```cpp
Widget field;
field.kind = WidgetKind::kTextField;
field.content = content_node;    // 展示文字的子节点
field.caret = caret_node;
field.selection_highlight = selection_node;
field.composition_underline = ime_underline_node;
widgets.attach(field_node, field);

// 插入/删除都要传 FontCatalog——因为要重新计算光标像素位置
widgets.text_field_insert(tree.render(), fonts, field_node, "hello");
widgets.text_field_backspace(tree.render(), fonts, field_node);
widgets.text_field_move(tree.render(), fonts, field_node, TextFieldMove::kLineEnd, /*extend_selection=*/false);
widgets.text_field_click(tree.render(), fonts, field_node, pointer_x, /*extend_selection=*/false);
widgets.text_field_set_focus(tree.render(), fonts, field_node, /*focused=*/true);
```

`text_field_insert()` 会对输入做 UTF-8 净化（`sanitize_utf8()`，坏字节替换为 U+FFFD）并丢弃 ASCII 控制字符（换行/Tab 之类，单行框没有意义）。`cursor`/`selection_anchor` 永远落在字位簇边界上（`grapheme_boundaries()`），因为所有修改都通过这些边界查询来定位，从不做原始字节算术——这就是「grapheme cluster 正确」的含义：退格键删掉一个 emoji 家族组合表情，而不是它内部的某一个码点。完整调用序列见 `examples/12_text_input/text_field_check.cpp` 和 `text_field_scene.cpp`。

### 接入系统输入法（IME）

```cpp
manager->start_text_input(window_id, caret_rect);  // 开始接收 SDL 的 IME 事件
// ... 每次收到 TextEditingEvent（preedit 预览）:
widgets.text_field_composition_update(tree.render(), fonts, field_node,
                                      event.text, event.start, event.length);
// 真正提交时走普通的 text_field_insert()，不是这条路径
manager->stop_text_input(window_id);
```

`composition_underline` 是绘制预览下划线用的子节点，preedit 阶段不会碰 `text`/`cursor`/`selection_anchor`——按 Escape 或换焦点直接丢弃 `composition_text` 即可，不需要撤销逻辑。`TextEditingEvent` 的 `start`/`length` 是 SDL 自己的「UTF-8 字符」单位，代码里转换成字节偏移，但**这个转换在真实输入法上未经验证**（见下方边界）。

## 边界与坑

- **只有单行编辑**：没有 `TextArea`。多行文本（`examples/20_multiline_text`）只是「显示」能力——wrap+build 出来的段落是只读的，不支持在里面点击定位光标或编辑。
- **无公开的富文本 API**：一个 `TextStyle` 只有一种字号/颜色/字重，不能在一段文字里局部加粗或换色。
- **无 RTL 界面镜像、无竖排、无双击选词**：这些都不存在，不是被裁剪掉的功能。
- **IME 偏移换算未经真机验证**：`TextEditingEvent` 的单位转换只是按 SDL 文档实现，没有在真实输入法上跑过。
- 详细清单见 `doc/limitations.md` 对应条目。

## 性能坑

`src/render/paragraph_build.cpp:54` 附近，每次调用 `Paragraph::build()` 都会 `sk_make_sp<FontCollection>()` 新建一个 `FontCollection`——**没有任何缓存**。这意味着每一帧、每一个 wrap 文本节点都会重建一整条 SkParagraph 排版管线。如果你的场景里有需要每帧重绘的自动换行文本（比如跟随窗口大小实时换行的说明文字），这条路径会成为明显的性能瓶颈。**结论：包裹（wrap）文本不要放在高频重绘路径上**——静态段落、一次性弹窗说明这类场景没问题，逐帧变化的 wrap 文本要自己做缓存或者避免频繁 `build()`。

## 相关 example

- `examples/06_font_fallback` —— 回退链、多脚本混排
- `examples/20_multiline_text` —— wrap + `Paragraph::build()` 的完整测量流程
- `examples/12_text_input` —— 单行编辑、选区、光标移动
- `examples/12_text_input`（IME 部分）—— `TextEditingEvent`/composition 预览
