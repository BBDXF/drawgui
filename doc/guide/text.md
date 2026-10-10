# 文本

## 用 `dg::App`

```cpp
(void)app.add_font("/usr/share/fonts", "DejaVu Sans");  // 第一个加入的字体是默认字体
const dg::NodeId label = window->create(dg::NodeKind::kLabel, root).value();
window->set_text(label, DG_PROP_TEXT, "你好, drawgui");
window->set(label, dg::prop::font_size(16));
window->set_prop(label, DG_PROP_TEXT_ALIGN, dg::PropValue::option(DG_TEXT_ALIGN_LEFT));

const dg::NodeId field = window->create(dg::NodeKind::kTextField, root).value();
// 用户输入后：TextChangedEvent{node = field}，window->text(field) 读出内容
```

- `add_font()` 要在建文本节点之前调用；没有字体时文本节点不画字。
- `kTextField` / `kTextArea`：点击聚焦并放置光标、键入、Backspace/Delete、
  方向键与 Home/End（加 Shift 选择）、Mod+C/X/V/A 剪贴板、撤销/重做、
  输入法预编辑（preedit 只做预览，不改已提交文本），全部由运行时处理。
  未聚焦且放不下的单行框显示省略号。
- 鼠标选择：在聚焦的编辑框里按下并拖动，选区跟着指针扩展（拖出框外也继续，多行框会随之
  滚动）；双击选词，三击在单行框全选、在多行框选所在的逻辑行。Shift+方向键扩展选区（运行时
  的 Shift+点击目前不扩展选区，只放置光标）。
- 光标：2 像素宽的竖线，两种编辑框规则相同——高度取字体的行高（上升 + 下降），但不超过字号的
  1.2 倍（16px 字号最多约 19px，CJK/emoji 回退字体不会把它撑高），在所在文本行里垂直居中，
  单行框里不会撑满整个框高。同一时刻只有聚焦的那个编辑框显示光标和选区；焦点移走（点击、Tab、
  `Window::focus()`、打开或关闭对话框）时旧编辑框的光标和选区随之撤掉。
- 占位符：`window->set_text(field, DG_PROP_PLACEHOLDER, "输入你的名字…")`
  （`kTextField`/`kTextArea`）。文字为空且没有组字时以 `color.on-surface` 的半透明色
  显示，随主题切换；其它节点返回 `kNotApplicable`。
- 编辑框文字离边框 10px（单行框左右，多行框四边）；写 `padding_*` 改这个内边距
  （对称，取最后写入的值，不影响布局盒子）。
- `dg::prop::wrap_text()`（`DG_PROP_WRAP_TEXT`）让标签按宽度换行（注意
  `DG_PROP_WRAP` 是容器换行，不是文字）；换行不会撑高节点，高度仍由你给。
- 任何节点写 `padding_*` 都会让它自己的文字在内容盒里排版（左对齐的标签向右
  缩进、换行宽度变窄、`padding_t` 下移）。
- `DG_PROP_MAX_LENGTH` 限制编辑框长度。
- 表单对话框里的编辑框和窗口里的一样工作（见 [popups.md](popups.md)「表单对话框」）。
- 示例：`examples/12_text_input`（C++ 运行时）、`examples/26_text_area`（C ABI）、
  `examples/29_enterprise`（搜索框和表单对话框里的编辑框）。

下面的「直接使用底层」讲文本栈本身。

## 这个子系统解决什么问题

drawgui 内核没有把文本当作特殊节点类型，而是当作 `NodeStyle::text`（`TextStyle`）这一个字段——和 fill、border、image 平级。这样做的好处是绘制、命中测试、damage 只需要处理一种节点结构；代价是文本子系统和布局子系统几乎没有耦合：**LayoutTree 完全不知道字体、字符串这些概念**，`BoxStyle` 里没有任何字段涉及文本测量。这直接导致一个后果，也是这份文档要讲清楚的最容易踩的坑：**多行/自动换行文本的高度必须由调用方自己测量好，再拿去建节点**，内核不会帮你算。

字体这边同样有一个容易踩空的默认行为：`FontCatalog` 里如果没有覆盖某个字族/某个语言的字体，绘制会**静默地什么都不画**，而不是报错或者退化成 tofu 方框。这是刻意的——「忘了设字体」和「设了字体但这台机器上没有」在视觉上是同一个失败：什么都没画出来,而不是悄悄换成排在最前面的某个字体。

参考实现：`include/drawgui/render/render_tree.h` 的 `TextStyle`、`include/drawgui/render/font_catalog.h`、`include/drawgui/render/paragraph.h`、`include/drawgui/render/grapheme.h`、`include/drawgui/widget/widget_set.h` 的 `kTextField` 一族。

## 直接使用底层

直接持有 `WidgetSet` / `RenderTree` 时，下面这些事要自己做：

- 把 `PumpResult::text_input` 交给聚焦编辑框（`text_field_insert()` /
  `editor_insert()`），把 `text_editing` 交给 `text_field_composition_update()` /
  `text_area_composition_update()`。
- 焦点变化时调用 `text_field_set_focus()` / `text_area_set_focus()`（省略号、光标显隐）。
- 每次重新布局后对多行编辑框调用 `text_area_refresh()`，对未聚焦的单行框重新推导显示。
- 剪贴板快捷键要自己走 `apply_clipboard_key()`（见 [input.md](input.md)）。
- 换行文本要先测量再建节点（见下文）。

改完要遵守 [`app.md` 的逃生舱约定](app.md#逃生舱约定)：只改运行时不组合的东西，改完调用 `Window::invalidate(node)`。

### 核心类型和它们的关系

- `dg::FontCatalog`：一张可共享（引用计数）的字体表。`scan(dir)` 扫描目录建表，`add(family, bold)` 注册一个具名字族并返回 `FontId`，`resolve(...)` 回答「这个字/这个码点该由哪个字族画」。
- `dg::FontId`：`{value}`，0 表示无效。
- `dg::TextStyle`：一次文本绘制的全部参数——`text`、`font`、`language`（BCP 47）、`size`、`color`、`align`、`wrap`、`max_lines`。
- `dg::Paragraph`：`wrap = true` 时用来**提前测量**高度的类型，不参与实际绘制路径（绘制仍由 `NodeStyle::text` 走）。它也提供多行编辑用的几何查询：`offset_at()`、`caret_rect()`、`range_rects()`、`lines()`、`line_of()`、`word_at()`，所有偏移都是 UTF-8 字节偏移（SkParagraph 这几个接口内部用 UTF-16，已在内部换算）。
- `dg::TextEditState` / `dg::text_edit::*`（`drawgui/widget/text_edit.h`）：与渲染无关的编辑核心——文本、光标、选区、IME 组字预览及插入/删除/移动/组字操作；`kTextField` 的编辑函数都是它的薄包装。
- `dg::WidgetSet` 的 `kTextField` 一族：单行可编辑文本框（状态在 `Widget::edit`）。
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

### 常见任务怎么做

#### 画一行不换行的文本

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

注意：**文本不会让节点自己收缩到内容大小**——节点的宽高仍然要按 [layout.md](layout.md) 的规则声明,文本只是被放进这个已经定好的盒子里绘制,超出部分按 `text.wrap`/`max_lines` 处理,或者直接溢出（不裁剪,除非父节点设了 `Overflow::kClip`)。

#### 让一段文本自动换行（wrap）——测量→建节点两步走

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

#### 让一段文本混合多种文字（拉丁字母、中文、emoji）

字体回退链是手工构建的，**不依赖 fontconfig**——同一个字族名可以覆盖 12 种脚本外加 emoji,不需要调用方为每种文字单独指定字体:

```cpp
std::vector<FontFallbackRule> rules = FontCatalog::default_fallback_rules();
fonts.set_fallback_rules(std::move(rules));  // 可选：替换成宿主自己的策略
```

命名的主字族覆盖不到的码点会走回退链；`language`（BCP 47 前缀匹配）决定选哪一条规则，这是解决中日韩汉字统一（同一码点在 zh-Hans/zh-Hant/ja/ko 里是不同字形）的唯一手段。`FontFallbackRule` 是纯数据表，没有 BCP 47 规范化算法——「zh-CN」不会自动变成「zh-Hans」，除非表里显式列出。

这条回退链**不需要 icudtl.dat**：字位簇切分用的是 libgrapheme 后端（`SkUnicodes::Libgrapheme::Make()`），不是 ICU。

#### 单行文本编辑（text field）

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
widgets.text_field_click(tree.render(), fonts, field_node, pointer_x, /*extend_selection=*/false,
                         /*click_count=*/1);  // 2 选词，3 全选
widgets.text_field_set_focus(tree.render(), fonts, field_node, /*focused=*/true);
```

`text_field_insert()` 会对输入做 UTF-8 净化（`sanitize_utf8()`，坏字节替换为 U+FFFD）并丢弃 ASCII 控制字符（换行/Tab 之类，单行框没有意义）。`cursor`/`selection_anchor` 永远落在字位簇边界上（`grapheme_boundaries()`），因为所有修改都通过这些边界查询来定位，从不做原始字节算术——这就是「grapheme cluster 正确」的含义：退格键删掉一个 emoji 家族组合表情，而不是它内部的某一个码点。完整调用序列见 `tests/unit/test_text_input.cpp`；运行时里的同一路径见 `examples/12_text_input`。

#### 多行文本编辑（text area）

`WidgetKind::kTextArea` 复用同一个编辑内核，只是把投影换成了换行段落：光标、选区、
点击定位都通过 `dg::Paragraph` 的行几何（全部是 UTF-8 字节偏移）算出来；软换行处同一个
偏移有两个光标位置，`Widget::caret_upstream` 记录当前用哪一个。搭建方法、按键表、
滚动与节点池见 [widgets.md](widgets.md)「多行编辑框」。

`text_area_insert()` 与 `text_field_insert()` 的唯一区别是保留 `\n`（其它控制字符，
包括 `\r`，照样丢弃）。粘贴走同一条路：粘进 TextArea 保留换行；粘进 TextField 的换行
被丢弃（不会改成空格）。

#### 接入系统输入法（IME）

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

- **多行编辑是 MVP**：`kTextArea` 每次按键都会把整段文字重新排版一次（O(n)）；超长文档放进去
  之前先量一下。撤销/重做、双击选词、三击选行两种编辑框都支持（见 [input.md](input.md)）。
- **无公开的富文本 API**：一个 `TextStyle` 只有一种字号/颜色/字重，不能在一段文字里局部加粗或换色。
- **无 RTL 界面镜像、无竖排**：这些都不存在，不是被裁剪掉的功能。
- **换行文字不撑高节点**：`wrap_text()` 只决定按宽度折行，节点高度仍要自己给（或先用 `Paragraph::build()` 测量）。
- **IME 偏移换算未经真机验证**：`TextEditingEvent` 的单位转换只是按 SDL 文档实现，没有在真实输入法上跑过。
- 详细清单见 `doc/limitations.md` 对应条目。

## 性能坑

`FontCollection` 每个 `FontCatalog` 只建一次；绘制 wrap 文本节点时，排好的段落按节点缓存，文本、样式、宽度都不变的重绘不会重新排版（只移动位置也不会）。真正的代价只剩"变化时重排"：逐帧改内容或改宽度的 wrap 文本仍然每帧完整排版一次，`Paragraph::build()` 本身也不走这份缓存、每次调用都排一次。**结论：静态段落随便用；逐帧变化的 wrap 文本放进高频路径前先量一下。**细节见 `doc/limitations.md` A1。

## 相关 example

- `examples/06_font_fallback` —— 回退链、多脚本混排
- `examples/20_multiline_text` —— wrap + `Paragraph::build()` 的完整测量流程
- `examples/12_text_input` —— 单行编辑、选区、光标移动
- `examples/26_text_area` —— 多行编辑、软换行 Home/End、滚动
- `examples/12_text_input`（IME 部分）—— `TextEditingEvent`/composition 预览
