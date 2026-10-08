# 能力边界（Limitations）

这份文档回答两个问题：这个库能用到什么程度、不能用到什么程度是可以确认的（不是猜测）。分五节：能用但有代价的坑、被误认为控件的配方、C ABI 的能力边界、测试真正证明了什么、尚未实现或明确不做的部分。

---

## A. 能用但有代价的坑

### A1. `TextStyle::wrap = true` 每次调用都重建整条排版管线

`src/render/paragraph_build.cpp:53-55` 每次 `build_paragraph()` 都新建一个 `sk_make_sp<FontCollection>()`——也就是说，每一帧、每一个开启 `wrap` 的文本节点，都要重新走一遍 SkParagraph 的整条构建管线，没有缓存。

**结论**：不要把 `wrap = true` 的文本节点放进高频重绘路径（跟手动画、逐帧更新的场景）。静态或低频更新的换行文本不受影响。

### A2. overlay 关闭不删节点，只是裁成空矩形——反复开关有累积成本

`PopupHost::close()` 把浮层容器裁剪为空矩形（`Overflow::kClip` 对空框），把它从绘制和命中测试里移除，但**不删除节点**。`include/drawgui/window/popup_host.h` 中 `close()` 声明前的注释说明这是一个选择，不是被迫的：`RenderTree::remove_child()` 从 8-5 起就存在且能用，之所以不用，是因为真删除会让 `content_root` 以及调用方持有的所有子节点 id 失效，还需要调用方自己跟进 `dg::on_node_removed()` 的六张侧表清理（见 `doc/contracts.md` 第 8 条）——这需要一个真实有动机的调用方去做这个决定，当前没有。

**结论**：反复开关同一个下拉菜单/右键菜单，会持续留下已死但未删除的节点，是稳定的内存/节点数增长，不是内存泄漏 bug，但长时间运行的应用需要知道这笔成本存在。

### A3. 隐式 transition 只覆盖 7 个属性

`AnimationEngine::read_current()`（`src/anim/animation_engine.cpp:104-144`）的 switch 只处理这些属性，其余属性设置隐式 transition 会静默跳变，不报错：

- `DG_PROP_BACKGROUND_COLOR`
- `DG_PROP_BORDER_COLOR`
- `DG_PROP_OPACITY`
- `DG_PROP_LEFT`
- `DG_PROP_TOP`
- `DG_PROP_WIDTH`
- `DG_PROP_HEIGHT`

显式调用 `animate(from, to)` 不受这个限制，可以对任意可插值属性显式指定起止值做动画。

### A4. 主题里的 int token 切换会触发重排，color token 不会

同一份主题里，颜色类 token 切换只需要重绘；数值类（间距、字号等）token 切换会触发布局重排。设计变体主题时如果只想换配色，用 color token；涉及尺寸/间距的变体要预期布局重算的开销。

### A5. Focus ring 在小于 `2*(gap+thickness)`（默认阈值 8px）的控件上会填满整个控件

`src/widget/focus.cpp:49-66` 与 :263-267 的 `fill_ring()`：当控件的宽或高小于 `2*(gap+thickness)` 时，不再画环形轮廓，而是把整个控件区域涂满焦点色。这是刻意的降级（“可见但不是环形”好过“完全不可见”），不是 bug。

**结论**：小于约 8×8 像素（默认 `gap`+`thickness` 参数下）的可聚焦控件，聚焦时会整体变色而不是显示细边框。控件设计时避免做得比这个阈值更小。

### A6. `kScrollView`/`kList` 不能用 Tab 聚焦

`include/drawgui/widget/focus.h:99-108`：焦点遍历不会落到滚动容器本身上。键盘滚动（PageUp/PageDown、方向键滚动列表）需要应用层自己实现为 action，不能指望 Tab 把焦点带到滚动区域再用方向键滚动。

### A7. `transform` 属性设置永远返回 `kUnsupported`

setter 存在、参数校验正确，但功能完全没有实现（`include/drawgui/props/node_props.h` 中 `set_transform()` 的注释："EXISTS AS A FOURTH ENTRY POINT BUT ALWAYS REFUSES"；`src/render/prop_dispatch.generated.inc` 的 `DG_PROP_TRANSFORM` 分支只做赋值，绘制侧没有消费）。这不是即将修的小 bug，是被三个未决问题卡住的功能：非轴对齐的 damage 矩形怎么算、逆变换命中测试怎么做、是否要影响父节点的布局尺寸。三个问题都没有结论前，`transform` 不会真正生效。

### A8. `present()` 只接受 BGRA8888，格式不对是报错不是自动转换

`include/drawgui/window/window_manager.h:93` 附近：“quietly presenting the wrong channels is worse than saying so”——传入其他像素格式不会被静默转换成 BGRA8888，会直接失败。

### A9. `WindowManager` 上的 `warp_pointer`/`post_*`/`showing_fill` 是测试专用接口，混在生产 API 里

`warp_pointer`、`post_pointer_button`、`post_expose`、`post_wheel`、`post_key`、`post_logical_key`、`post_text_input`、`post_text_editing`、`showing_fill` 这些方法是给自动化测试注入合成事件用的（`include/drawgui/window/window_manager.h:474-647` 附近）。它们没有命名前缀做区分——对照 C ABI 侧至少给调试接口加了 `dg_debug_` 前缀，这批 C++ 接口没有。**不要在产品代码路径里调用它们**，它们会绕过真实的 SDL 事件源直接注入状态。

### A10. IME 组字（preedit）的偏移换算未经真机验证

`include/drawgui/window/window_manager.h:321-348`：`TextEditingEvent::start`/`length` 是 SDL3 自己文档里定义的“UTF-8 字符数”单位，转换成这个引擎内部使用的字节偏移的逻辑是按字面意思实现的，但注释自己承认——开发这个项目的机器上，真实 IME 从来不会发送 `SDL_EVENT_TEXT_EDITING` 事件（它们用自己的组字窗口），现有的 IME 相关测试全部靠合成事件覆盖。这条转换逻辑没有被一次真实的中文/日文输入法组字过程验证过。

### A11. ABI 的 `dg_node_insert_before` 只支持在末尾追加

C ABI 的 `dg_node_insert_before(parent, child, ref)` 目前只接受 `ref == NULL`（等价于 append），传非空 `ref` 返回 `DG_ERR_UNSUPPORTED`（`abi/drawgui.def.toml` 中 `error` 常量组的 `UNSUPPORTED` 条目）。C++ 层的插入能力比这个更完整，但没有对应导出。

### A12. 未覆盖字体族的字符画成 `.notdef` 方框（tofu），不是空白

字体 fallback 链（`src/render/font_fallback.cpp`）是这个项目自己实现的显式规则表，绕开了 `matchFamilyStyleCharacter()` 在这台开发机上七次测试七次返回 null 的事实（`src/render/font_fallback.cpp:3-7` 附近的注释）。这个自建链条覆盖了常见语言组合，但对完全没被任何规则覆盖的字符——`src/render/skia_paint.cpp:209-213`：每个未覆盖的 codepoint 会画出一个 `.notdef` 方框（tofu），而不是空白；这是有意的决定，为的是让"这台机器没有对应字体"和"这段文字本来就是空的"两种情况可以被肉眼区分。排查这类问题时应该找方块字，而不是空白。

---

## B.「配方」不是「控件」

`dropdown`、`context menu`、`tooltip`、`dialog` 这几个名字，**在 `src/` 里没有对应的控件实现**：

- `kDropdown` 这个节点种类只持有 anchor 位置、options 列表、当前选中项索引（`include/drawgui/widget/widget_set.h:114-128`）。点开之后那一整块弹出的选项列表，是**应用层自己拿 `PopupHost` + 一堆 `kButton` 现搭出来的**，不是库内建的渲染/交互逻辑，见 `examples/22_dropdown_menu/dropdown_options.cpp`。
- `src/widget/tooltip.cpp` 只提供一个 hover 计时器（多久悬停后触发“该显示了”的信号），气泡本身的形状、定位、内容全部在 `examples/23_menu_tooltip_dialog/tooltip_content.cpp` 里手写。
- 右键菜单、模态对话框同理，都是 `examples/23_menu_tooltip_dialog/` 里用 `PopupHost` + 焦点 API + 若干普通控件手工拼出来的应用层代码。

**如果你的项目需要下拉菜单、右键菜单、tooltip、对话框，正确的起点是把 `examples/22_dropdown_menu/` 和 `examples/23_menu_tooltip_dialog/` 当模板抄一份**，而不是去 `src/` 里找一个现成控件类。这些 example 是"这样搭是正确的"的参考代码，不是库承诺提供的能力。

---

## C. C ABI 的能力边界

C ABI（`include/drawgui/abi/drawgui.h`）目前能做到：开窗、建 `BOX`/`BUTTON`/`LABEL`/`CHECKBOX`/`TEXT_FIELD` 节点、设置 51 个属性（含专用的 `dg_node_set_text()` 通道）、读回 checkbox 勾选状态（`dg_node_get_checked()`）与文本框文字（`dg_node_get_text()`）、装配字体（`dg_app_add_font()`）、接收 `close`/`click`/`action`/`text_changed` 四类事件、加载主题包。

**P6 sub-step a1 起，能画文字了**——但只是最小可见的一行：`DG_NODE_TYPE_LABEL` 有引擎内置的默认字号（16px）和默认黑色，`font_size`/`text_align` 这两个属性**故意没做**（够用就不加，是这个仓库的纪律），所以想要更大字号、居左/居右对齐，ABI 现阶段做不到，只能直接用 C++ 层的 `TextStyle`。

以下四项不是遗漏，是 `abi/drawgui.def.toml` 头部注释里逐项具名拒绝的，各有各的理由：

- **无回调模式**（`dg_set_event_callback`）——没有真实嵌入 QuickJS 的调用方之前，接口形状无从设计
- **无自定义绘制**（`dg_custom_paint_set_cmds`）——不在当前验收路径上
- **无动画 API**（`dg_animate`/`dg_node_set_transition`）——C++ 侧 `AnimationEngine` 已存在，只是没有调用方需要它被导出
- 无焦点 API、无弹层 API——同样：ABI 现有的验收标准不需要它们

此外 `Widget::group`（radio）与 IME 组字接口也在同一处注释里具名不导出：前者因为分组切换不重绘兄弟节点、且无调用方需要；后者因为组字偏移换算从未经真机 IME 验证（见 A10）。

想要动画、焦点管理、弹层、可配置字号/对齐这些能力，目前只能直接用 C++ 层，ABI 现阶段不是这些能力的入口。

---

## D. 测试覆盖的真实形状

### golden 测试有 8 张 baseline，后 4 张走 ABI 路径

`tests/golden/golden_test.cpp` 的前 4 个场景（原有，未改动）：纯色清屏、一个圆角矩形填充、两个描边形状（矩形+圆角矩形，含非对称圆角）、两个半透明填充的叠加混合——全是裸 `Canvas` 原语，**没有一张涉及文字、节点树、布局、主题、widget、图片、阴影、渐变**。

第 5 个场景 `abi_label_text`（P6 sub-step a1 新增，`tests/golden/abi_label_scene.cpp`）**通过 `include/drawgui/abi/drawgui.h` 建一个真实 `dg_window_t`**：`dg_app_add_font()` 装配 `tests/fonts/gen_test_fonts.py` 生成的确定性测试字体、`dg_node_create(DG_NODE_TYPE_LABEL)`、`dg_node_set_text()`、一次真实 paint pass，再用 TEST-ONLY 的 `dg_debug_capture_png()` 取出这一帧的 PNG 字节——这是第一张证明"ABI 建的节点树真的画出了像素"的 golden。之后又加了三张 ABI 场景：`abi_checkbox_off`/`abi_checkbox_on`（`tests/golden/abi_checkbox_scene.cpp`，checkbox 未勾选/点击勾选后的外观）与 `abi_text_field`（`tests/golden/abi_text_field_scene.cpp`，点击聚焦后注入文字的文本框）。

前 4 张图证明的是："Skia 的圆角矩形四角顺序转换、alpha 混合数学没有退化"；`abi_label_text` 多证明了一件事："ABI 装配的字体+LABEL 节点+`dg_node_set_text()` 这条完整路径能画出可见的字形，且在这台机器和任何一台机器上逐字节一致"。**它们仍然不证明** 其余控件的外观、主题应用、复杂布局结果没有退化——这些路径依然没有像素级回归测试。

### 结构上不可自动测试的部分

- 真实显示器上的观感（色彩管理、DPI 缩放的视觉效果）
- 真实 IME 的组字互动（见 A10）
- 真实系统剪贴板与其他应用的互操作
- `present()` 之后像素真正落到屏幕上这一步
- 真实窗口 resize 的用户手感（大小、时序）

以上这些，测试套件只能覆盖到“合成事件驱动下代码路径没有崩溃/结果符合预期结构”，不能覆盖到“真实设备上确实是这个效果”。

### 不要相信任何“连续 N 个 slice 零新增绘制原语”的说法

不同历史记录里对某个数字的说法互不相同（例如"连续多少个 slice 零新增绘制原语"），且都没有工具链自动验证支撑，是手工记账留下的不一致遗留（这些数字记录在被移除的开发切片文档中，见 git 历史）。这份文档不复述这类数字。

---

## E. 未实现 / 路线图缺口

以下按三类区分：**真实缺口**（该有但还没做）、**有意推迟**（有明确的技术理由，未来可能补）、**明确不做**（`doc/design.md` 已经写清楚不在这个项目的范围内）。这个区分很重要——第三类不要当 bug 报告。

### 真实缺口 / 有意推迟

- **布局语义空洞**：`row_reverse`/`column_reverse`（推迟理由：反转 remainder 分配会破坏字节级一致性测试的基线）、`space_around`/`space_evenly`（算术已经存在于 `align_content`，缺一个把 `MainAlign` 扩出去的决定）、baseline 对齐（需要先有一次测量再定位，规划过但没有落地）、wrap 容器内 `align: stretch`、负 margin（当前实现遇负值会在 0 处截断，需要 `deflate` 语义配合才能对称让出空间）、百分比长度（刻意未写，理由记录在被移除的开发切片文档中，见 git 历史）、分数 grow 权重（当前只支持整数前缀和）
- **手势竞技场（gesture arena）未建**，惯性滚动/回弹（fling/inertia）缺失——都需要一个还不存在的动画时钟驱动的速度来源
- **Windows/macOS 从未验证**——当前只有 Linux/SDL3 一条实测路径
- **JS 绑定层（P6）完全未开始**
- **移动端（P8）未开始**——Android/iOS 宿主、生命周期、触摸手势都还没做
- **无公开的富文本编辑 API，无 TextArea**：现有的多行文本能力是"能显示多行"，不是"能编辑多行"（无跨行选择、无双击选词、无三击选段）
- **无 RTL 界面镜像**：BiDi 文本渲染本身是支持的，但整体界面镜像（flex 主轴反转、padding 左右互换、图标翻转）没有做
- **无竖排文本**
- **字体 fallback 链对完全未覆盖的字符只画 `.notdef` 方框（tofu）**，见 A12——这不是"处理不好"，是链条设计本身：只覆盖显式列出的语言/字符集组合

### 明确不做（scope boundary，不是遗漏）

- **无障碍（accessibility）实现**：`doc/design.md` §11 明确写了"不做"，只留了架构挂点（一棵与渲染树平行但更稀疏的 a11y 节点树），不对接 UIA/NSAccessibility/AT-SPI 等平台无障碍框架。这是这个项目当前定位下的范围边界，不是"还没写"的缺口——如果需要无障碍支持，需要在这个挂点上自己接平台层，库本身不提供。
