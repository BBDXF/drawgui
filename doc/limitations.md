# 能力边界（Limitations）

这份文档回答两个问题：这个库能用到什么程度、不能用到什么程度是可以确认的（不是猜测）。分五节：能用但有代价的坑、弹层组件（菜单/下拉/tooltip/对话框）的边界、C ABI 的能力边界、测试真正证明了什么、尚未实现或明确不做的部分。

---

## A. 能用但有代价的坑

### A1. `TextStyle::wrap = true` 的文本在内容或宽度变化时才重新排版

`FontCollection` 现在每个 `FontCatalog` 只建一次（`FontAccess::paragraph_collection()`，`src/render/font_catalog.cpp`），所有段落共享它的字体查找与 SkParagraph 自带的整形缓存。绘制侧另有逐节点的段落缓存（`Node::paragraph_cache`，`src/render/tree_impl.h`）：同一个 wrap 节点在文本、样式、宽度都不变时，重复重绘直接复用上一次排好的 `skia::textlayout::Paragraph`，不再调用 `build_paragraph()`。`set_text()`/`set_style()` 改了文本样式、`set_local_bounds()` 改了宽度时丢弃缓存（只移动位置不丢）；节点经 `remove_subtree()` 删除时一并释放。绘制时还会再比对一次键（字体目录、`TextStyle`、宽度），所以任何其它路径改了样式也不会画出过期的排版。`detail::paragraph_build_count()` 与 `RenderTree::paragraph_cache_size()` 是给测试用的计数探针（`tests/unit/test_paragraph_geometry.cpp`）。

剩下的代价：

- 文本或宽度**每次真的变化**都要完整重排一次（例如逐帧改字的 wrap 文本、跟随窗口拖拽实时改宽的段落）——缓存只省掉"没变却重绘"的那部分。
- `dg::Paragraph::build()`（布局前的高度测量、TextField 的光标测量）不经过绘制侧缓存，每次调用都排一次版；只是不再额外新建 `FontCollection`。
- 每个缓存条目持有一份 `TextStyle` 副本和排好的段落，内存随 wrap 节点数线性增长。

**结论**：静态或低频更新的换行文本基本没有重复排版开销；逐帧改变内容或宽度的 wrap 文本仍然每帧排版一次，放进高频路径前先量一下。

### A2. `close()` 只裁空不删节点；要释放请用 `destroy()`

`PopupHost::close()` 把浮层容器裁剪为空矩形（`Overflow::kClip` 对空框），把它从绘制和命中测试里移除，但**不删除节点**——这是廉价的"隐藏"，`content_root` 和调用方持有的子节点 id 都保持有效。裁空现在是**按需选择**的：`PopupHost::destroy(handle, tables)` 会经 `dg::remove_subtree()`（RenderTree 重载）真正删除整棵浮层子树并清理六张侧表（见 `doc/contracts.md` 第 8 条），之后这些 id 全部失效，需要重新 `show()` 拿新的。

**结论**：只用 `close()` 反复开关弹层，会持续留下已死但未删除的节点（稳定的节点数增长）；每次重新构建内容的弹层应在关闭时调用 `destroy()`，释放的槽位会被后续 `add_child()` 复用，节点数不再增长。`examples/22_dropdown_menu`、`23_menu_tooltip_dialog`、`25_showcase` 已改用 `destroy()`。

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

### A6b. 滚动条：节点由调用方创建，布局后要 sync，没有连续翻页

`kScrollView`/`kList` 的滚动条（`WidgetSet::attach_scrollbar()`，见 `doc/guide/widgets.md`「滚动条」）有这些边界：

- **轨道和滑块是调用方建的普通节点**，和滑块控件一样；库不会替你建，也不会替你在视口上留槽——不留 padding 时滚动条覆盖在内容边缘上。
- **布局之后要 `sync_scrollbar()`**：布局会把轨道放回内容原点（和 `resync_sliders()` 同一种义务）；写完 `DG_PROP_SCROLLBAR`、或绕过 `scroll_by()` 直接 `RenderTree::set_scroll_offset()` 之后也要。
- **每个视图只有一根滚动条**：一个 `kScrollView` 只沿一个轴滚动（`scroll_axis`），所以没有双轴、也没有右下角的交汇块。
- **点轨道只翻一页**，按住不会连续翻页（没有定时器驱动的重复）；没有「点哪跳哪」模式，也没有滚动条上的箭头按钮。
- **滚动条不会自动隐藏/淡出**（没有 overlay 式滚动条）；外观只有纯色填充，最小滑块长度 `min_thumb_length` 不是主题 token。
- C ABI 的 `SCROLL_VIEW`/`LIST`/`TEXT_AREA` 由 ABI 自己建滚动条、留槽、在布局后 sync；其余边界同上。

### A7. `transform` 属性设置永远返回 `kUnsupported`

setter 存在、参数校验正确，但功能完全没有实现（`include/drawgui/props/node_props.h` 中 `set_transform()` 的注释："EXISTS AS A FOURTH ENTRY POINT BUT ALWAYS REFUSES"；`src/render/prop_dispatch.generated.inc` 的 `DG_PROP_TRANSFORM` 分支只做赋值，绘制侧没有消费）。这不是即将修的小 bug，是被三个未决问题卡住的功能：非轴对齐的 damage 矩形怎么算、逆变换命中测试怎么做、是否要影响父节点的布局尺寸。三个问题都没有结论前，`transform` 不会真正生效。

### A8. `present()` 只接受 BGRA8888，格式不对是报错不是自动转换

`include/drawgui/window/window_manager.h:104` 附近：“quietly presenting the wrong channels is worse than saying so”——传入其他像素格式不会被静默转换成 BGRA8888，会直接失败——`PixelFormat::kRgba8888` 交给 `present()` 也是报错。唯一会做 RGBA→BGRA 转换的是 `set_icon()`（图标很小，每次调用拷贝一份）。

### A9. `WindowManager` 上的 `warp_pointer`/`post_*`/`showing_fill` 是测试专用接口，混在生产 API 里

`warp_pointer`、`post_pointer_button`、`post_expose`、`post_wheel`、`post_key`、`post_logical_key`、`post_text_input`、`post_text_editing`、`post_window_event`、`showing_fill` 这些方法是给自动化测试注入合成事件用的（`include/drawgui/window/window_manager.h:537-766` 附近）。它们没有命名前缀做区分——对照 C ABI 侧至少给调试接口加了 `dg_debug_` 前缀，这批 C++ 接口没有。**不要在产品代码路径里调用它们**，它们会绕过真实的 SDL 事件源直接注入状态。

### A10. IME 组字（preedit）的偏移换算未经真机验证

`include/drawgui/window/window_manager.h:337-364`：`TextEditingEvent::start`/`length` 是 SDL3 自己文档里定义的“UTF-8 字符数”单位，转换成这个引擎内部使用的字节偏移的逻辑是按字面意思实现的，但注释自己承认——开发这个项目的机器上，真实 IME 从来不会发送 `SDL_EVENT_TEXT_EDITING` 事件（它们用自己的组字窗口），现有的 IME 相关测试全部靠合成事件覆盖。这条转换逻辑没有被一次真实的中文/日文输入法组字过程验证过。

### A11. ABI 的节点移动不能跨窗口

C ABI 的 `dg_node_insert_before(parent, child, ref)` 已支持按 `ref` 定位插入、以及把同一窗口里已挂上的节点移动到新位置（保留身份、控件状态和句柄），`dg_node_remove` 会摘除整棵子树并让子树里所有句柄失效——足够让 JS 侧的 keyed reconciler 驱动这棵树。剩下的限制：一个节点不能从一个窗口移动到另一个窗口（返回 `DG_ERR_ALREADY_ATTACHED`，需要在目标窗口重建）；`ref` 只能是有句柄的子节点，checkbox 指示块、文本框内部子节点这类没有句柄的内部子节点不能当 `ref`，新子节点因此总是排在它们之后。

### A12. 未覆盖字体族的字符画成 `.notdef` 方框（tofu），不是空白

字体 fallback 链（`src/render/font_fallback.cpp`）是这个项目自己实现的显式规则表，绕开了 `matchFamilyStyleCharacter()` 在这台开发机上七次测试七次返回 null 的事实（`src/render/font_fallback.cpp:3-7` 附近的注释）。这个自建链条覆盖了常见语言组合，但对完全没被任何规则覆盖的字符——`src/render/skia_paint.cpp:209-213`：每个未覆盖的 codepoint 会画出一个 `.notdef` 方框（tofu），而不是空白；这是有意的决定，为的是让"这台机器没有对应字体"和"这段文字本来就是空的"两种情况可以被肉眼区分。排查这类问题时应该找方块字，而不是空白。

### A13. 禁用状态的外观只是最小实现

`DG_PROP_DISABLED` 的行为是完整的（见 `doc/guide/widgets.md`「禁用状态」），但外观只覆盖
`refresh()` 管理的填充：控件表面换成 `color.disabled`，已勾选指示器换成
`color.on-disabled`。**标签文字颜色、滑块轨道/滑块头、文本框边框/文字不会变灰**；禁用控件上
**不显示 tooltip**；C++ 调用方写完该属性要自己调用 `cancel_disabled()`/`blur_if_disabled()`/
`refresh_subtree()`（C ABI 已自动做）；ABI 在创建控件时读入禁用色，之后切换主题不会刷新。

---

## B. 弹层组件：库提供了什么、没提供什么

`dropdown`、`context menu`、`tooltip`、`dialog` 现在有**库组件**（`include/drawgui/ui/`，实现在 `src/ui/`）：`dg::Menu`、`dg::Dropdown`、`dg::Tooltip`、`dg::Dialog`。它们建在 `PopupHost` + 普通 `kButton` + `dg::Focus` 之上，**不是新的 `WidgetKind`，也没有新的绘制原语**；用法见 `doc/guide/popups.md` 的"库组件"一节。能确认的边界：

- **由宿主循环驱动，没有回调**：调用方自己把 `pump()` 出来的事件交给组件的 `handle_pointer()`/`handle_key()`/`handle_text()`/`tick()`，读回 `MenuResult`/`DropdownResult`/`TooltipEvent`/`DialogResult`。组件打开期间，事件该不该再分发给宿主场景由调用方决定（菜单/对话框打开时示例都是"组件吃掉全部事件"）。
- **每次关闭都 `destroy()`**：节点和控件计数回到基线（单测 `tests/unit/test_ui_components.cpp` 验证）；关闭后组件给出的 NodeId（`item_nodes()`/`buttons()`/`body()`）全部失效。
- **不支持嵌套**：菜单里开子菜单、对话框里再开下拉都不行——`Focus` 只有一个作用域，不是栈；后开的会替换前一个的作用域。
- **菜单高亮就是焦点**：高亮项是 `Focus::current()`，没有单独的"高亮游标"；菜单不画焦点环，用 `PopupStyle::item_hover` 填充高亮行。分隔线不挂控件、禁用项带 `NodeStyle::disabled`，所以键盘导航跳过它们是 `focus_order()` 现成的规则。
- **激活发生在按下（kDown）**，不是松开；Type-ahead 只看首字母、ASCII 大小写不敏感。
- **文字宽度**：给了 `FontCatalog` 时用 `Paragraph` 实测，没给时按每字节 0.6em 估算；没给 `FontCatalog` 时 `Dropdown` 无法提交选择（`dropdown_select()` 需要它）。
- **对话框覆盖整个窗口**，尺寸取 `RenderTree::viewport()`；窗口打开期间改尺寸不会重新居中（关掉重开即可）。面板/按钮布局是手算的固定几何，不走 `LayoutTree`。
- **主题**：颜色/尺寸来自已有 token（`popup_style_from_theme()`），遮罩色 `PopupStyle::scrim` 取自 `color.scrim`（内建主题：暗色 `#00000099`、亮色 `#0F141A66`）。
- **视觉是最小可用版**：所有绘制/节点形状集中在 `src/ui/ui_visuals.cpp`，等设计侧打磨。
- **C ABI 已导出这些组件**（0.2 phase B，见 C 节）：`dg_menu_open()`、`DG_NODE_TYPE_DROPDOWN`、`dg_node_set_tooltip()`、`dg_dialog_open()`，事件泵替调用方做路由和 tick。
- `examples/23_menu_tooltip_dialog` 的**原生（第二个 OS 窗口）对话框**分支、`examples/25_showcase` 的带输入框的资料对话框（依赖文本框的 IME/焦点钩子）仍是应用层手搭的，没有迁移到 `dg::Dialog`。

---

## C. C ABI 的能力边界

C ABI（`include/drawgui/abi/drawgui.h`，版本 0.2，85 个函数，完整列表见 `doc/reference/c-abi.md`）现在能做到：

- **窗口**：开窗并操作（标题、尺寸、位置、最小/最大尺寸、最小化/最大化/还原、全屏、可调整大小、图标、状态查询），接收 `DG_EVENT_WINDOW_*` 状态事件。没有关闭单个窗口的函数——窗口只会因用户关闭或 `dg_app_destroy()` 而消失。
- **节点**：十四种——`BOX`、`BUTTON`、`LABEL`、`CHECKBOX`、`TEXT_FIELD`、`TEXT_AREA`、`SCROLL_VIEW`、`LIST`、`IMAGE`，以及 phase B 的组件 `DROPDOWN`、`PROGRESS`、`TABS`、`TREE`、`TABLE`；按位置插入、同窗口内移动、整棵子树删除。
- **弹层**（phase B）：菜单 `dg_menu_open()`（锚定节点或坐标，`DG_EVENT_MENU_ACTIVATE`/`_DISMISS`）、模态对话框 `dg_dialog_open()`（`DG_EVENT_DIALOG_RESULT`）、tooltip `dg_node_set_tooltip()`（`dg_wait_events()` 自己为延迟醒来）。
- **布局**：`BOX` 写 `DG_PROP_DIRECTION` 变成行/列容器（`JUSTIFY`/`ALIGN`/`GAP`/`WRAP` 等随之可用）；百分比长度（`DG_LENGTH_UNIT_PERCENT`）与 `DG_LENGTH_UNIT_AUTO`（回到按内容/由父容器拉伸，去掉创建时的默认尺寸），都只用于 `WIDTH`/`HEIGHT`/`MIN_*`/`MAX_*`。
- **文字**：`DG_PROP_TEXT`/`TEXT_COLOR`/`FONT_SIZE`/`TEXT_ALIGN`/`WRAP_TEXT`（自动换行），`PADDING_*` 同时移动节点自己的文字；编辑框 `DG_PROP_MAX_LENGTH`、`DG_PROP_PLACEHOLDER`（`dg_node_set_text()`）、文字内边距（默认 10px，`PADDING_*` 修改）。
- **主题**：`BUTTON`/`CHECKBOX`/`RADIO` 的颜色来自主题 token 并随切换更新；调用方写入或绑定的 `BACKGROUND_COLOR`/`TEXT_COLOR`/`BORDER_COLOR` 优先（背景色的悬停/按下色由它派生）。
- **焦点**：`dg_node_focus()`/`dg_window_get_focus()`/`dg_window_clear_focus()`、Tab/Shift+Tab、`DG_PROP_TAB_INDEX`、`DG_EVENT_FOCUS_CHANGED`。
- **指针**：`DG_EVENT_CLICK` 带连击数（`dg_event::clicks`），右键按下是 `DG_EVENT_CONTEXT_CLICK`。
- **事件循环**：`dg_timer_add()`/`dg_timer_cancel()`（`DG_EVENT_TIMER`），线程安全的 `dg_wake()`/`dg_post_user_event()`（`DG_EVENT_USER`）。
- **其它**：读回 checkbox 状态、编辑框文字、列表选中行、滚动偏移；装配字体；加载主题包。

能用但有边界的地方：

- **容器不能变回叶子**：`BOX` 一旦写过 `DIRECTION` 就一直是容器；`row_reverse`/`column_reverse` 未实现。`WRAP` 必须在 `DIRECTION` 之后写。
- **文字没有字族/字重属性**：只有 `dg_app_add_font()` 注册的第一个字族，`TextStyle` 本身没有字重。改大字号不会撑高节点盒子，开了 `WRAP_TEXT` 的标签也不会按行数长高（高度仍由调用方给）；编辑框的 `TEXT_COLOR` 在下次切换主题时被主题色覆盖。编辑框的 `PADDING_*` 只设一个对称的文字内边距（最后写入的那一边的值；`TEXT_FIELD` 只用于左右，`TEXT_AREA` 四边），不进入布局盒子。
- **`SCROLL_VIEW`** 只沿一个轴滚动；它的内容节点没有句柄，`PADDING`/`BACKGROUND` 之类只能写在视口上（容器属性会转给内容节点）。写 `PADDING_R`/`PADDING_B` 会覆盖为滚动条预留的槽。键盘滚动只在焦点落在它里面的控件上时生效（滚动视图本身不可聚焦，见 A6）。
- **`LIST`** 只有单列文字行：同时可见最多 30 行（32 个复用行节点），数据整体推送（`dg_list_set_items()` 全量拷贝，没有增量更新、也没有按索引拉取的回调），行高统一、不可聚焦、没有键盘导航、没有悬停高亮，选中发生在按下而不是松开。
- **`IMAGE`**：同步解码；图片表只增不减，app 存活期间不释放（换图片会留下旧图）；SVG 视为可信资源（经 Skia SVG 解析器）；`svg_tint_argb` 是解码时的整图 SrcIn 着色；`DG_PROP_IMAGE_TINT`（可绑定主题 token）在绘制时着色、覆盖解码时的 tint，同一张图可在多个节点上着不同颜色。见 E 节的图片格式边界。
- **定时器 id 低于 2^31**，事件里放在 `int32_t x` 中；用户事件的 64 位负载拆在 `x`/`y` 两个字段里。
- **焦点事件只报新焦点**，不报失去焦点的是谁；焦点落在没有句柄的节点上时 `node` 为 NULL。

以下几项不是遗漏，是 `abi/drawgui.def.toml` 头部注释里逐项具名拒绝的，各有各的理由：

- **无回调模式**（`dg_set_event_callback`）——没有真实嵌入 QuickJS 的调用方之前，接口形状无从设计
- **无自定义绘制**（`dg_custom_paint_set_cmds`）——不在当前验收路径上
- **无动画 API**（`dg_animate`/`dg_node_set_transition`）——C++ 侧 `AnimationEngine` 已存在，只是没有调用方需要它被导出

此外 `Widget::group`（radio）与 IME 组字接口也在同一处注释里具名不导出：前者因为分组切换不重绘兄弟节点、且无调用方需要；后者因为组字偏移换算从未经真机 IME 验证（见 A10）。

phase B 组件（菜单、下拉框、tooltip、对话框、ProgressBar、Tabs、Tree、Table）已导出，边界：

- **每个窗口同时最多一个 `dg_menu_open()` 菜单、一个对话框**；再开菜单会先关旧的（旧菜单报 `DG_EVENT_MENU_DISMISS`），对话框打开时 `dg_menu_open()` 返回 `DG_ERR_UNSUPPORTED`。不支持子菜单。菜单/对话框打开期间该窗口的全部指针/按键/文字事件都交给它们（菜单外的点击关掉菜单，且这次点击被吃掉）。
- **弹层不随主题切换重绘**：已打开的菜单/对话框/tooltip 保持打开时的样式，下次打开才用新主题；`DROPDOWN`/`PROGRESS`/`TABS`/`TREE`/`TABLE` 本身在主题切换时**重建内部节点**（状态、选中、展开、排序指示、调用方的标签页面板都保留；节点数不变），内部节点上的焦点移到重建后的对应节点。
- **数据整体推送**：`dg_tree_set_items()`/`dg_table_set_cells()`/`dg_dropdown_set_items()` 全量拷贝，没有增量更新；`dg_table_set_columns()` 清空行。表格**不自己排序**：`DG_EVENT_TABLE_SORT` 之后由调用方排好、重新推送、`dg_table_set_sort_indicator()`。
- **尺寸**：`TREE`/`TABLE` 的内部视图跟随布局后的节点高度（像素、百分比、`GROW`、父容器拉伸都算）：高度变化后在下一次布局同步时重建内部节点（池大小随之变化），所以变化后的第一帧仍是旧高度；`PROGRESS` 的轨道填满节点；宽度跟随节点（内部是 100%）。
- **`TABS`** 只能通过 `dg_tabs_add()` 加标签（没有删除/改名）；`dg_node_insert_before(tabs, …)` 返回 `DG_ERR_UNSUPPORTED`，内容放进返回的面板 `BOX`。隐藏面板被禁用（离开 Tab 序列）。
- **`dg_tree_set_selected(-1)`/`dg_table_set_selected(-1)`** 通过重建/重推清空选中，滚动位置会回到顶部（树）或被夹紧（表）。
- **焦点事件**：组件自己移动焦点（点标签、点树）时，事件泵比较前后焦点补发 `DG_EVENT_FOCUS_CHANGED`，`node` 是最近的有句柄祖先（标签页报 `TABS` 节点）；弹层打开期间不报。
- **测试钩子**：`dg_debug_post_text_field_key()` 追加 `Escape`；`dg_debug_node_count()` 返回窗口树的节点总数（含弹层），用于验证关闭/删除后回到基线。

渐变/阴影/变换三个复杂属性 setter 和 `kSlider` 控件仍未导出。

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
- 鼠标指针形状：dummy 驱动没有光标，测试只能通过 `WindowManager::current_cursor()`/`cursor_switch_count()`（ABI 是 `dg_debug_get_cursor()`）验证解析结果和“只在变化时切换”，屏幕上是否真的换成了对应的系统光标、多窗口之间切换是否正确（依赖真实的 mouse-enter 事件）没有自动化覆盖
- 窗口的最小化/最大化/还原和图标：测试跑在 SDL 的 dummy 视频驱动下，它直接拒绝这几个请求（`DG_ERR_UNSUPPORTED`），所以测试只能验证"拒绝会被如实报告"和参数校验，以及用 `WindowManager::post_window_event()` 注入的事件能正确分发；真实窗口管理器上是否真的最小化、图标是否显示、`set_resizable()` 是否生效（dummy 驱动接受但从不报告可调整）都没有自动化覆盖。标题、尺寸、位置、最小/最大尺寸、全屏在 dummy 驱动下是真实生效、可测的

以上这些，测试套件只能覆盖到“合成事件驱动下代码路径没有崩溃/结果符合预期结构”，不能覆盖到“真实设备上确实是这个效果”。

### 不要相信任何“连续 N 个 slice 零新增绘制原语”的说法

不同历史记录里对某个数字的说法互不相同（例如"连续多少个 slice 零新增绘制原语"），且都没有工具链自动验证支撑，是手工记账留下的不一致遗留（这些数字记录在被移除的开发切片文档中，见 git 历史）。这份文档不复述这类数字。

### A14. 点击计数、指针捕获与定时器的边界

- `PointerEvent::clicks`：平台计数会一直增长（4、5…），自行推导的计数按 1、2、3 循环；
  判断双击用 `clicks == 2`。C ABI 的编辑框和 `dg_event::clicks` 都用这个字段（ABI 另外
  规定：两次按下之间按过键，连击数从 1 重新数起）。定时器、`wake()`/`post_user_event()`
  已暴露为 `dg_timer_*`/`dg_wake()`/`dg_post_user_event()`；指针捕获没有暴露。
- 指针捕获只在 Linux/SDL3 上实现，dummy 驱动下平台调用无效（只有逻辑状态可观察）；
  捕获期间不会把其他窗口的事件改派给被捕获窗口，这件事交给 SDL 自己。
- 定时器精度受 `SDL_WaitEventTimeout` 的毫秒粒度和调度延迟限制，只保证"不早于截止时间"；
  重复定时器不补发错过的周期。定时器到期只在 `pump()` 里检查，不 `pump()` 就不会触发。
- 除 `wake()`/`post_user_event()` 外，`WindowManager` 不是线程安全的。

---

### A15. 数据控件（ProgressBar/Tabs/TreeView/Table）的边界

- **布局后要 `sync()`**；忘了会看到隐藏面板重新出现、树/表的行叠在原点、进度填充回到 0。
- **只读文本行**：树/表的行只有文字，没有图标、行内控件、单元格编辑或多选；行高固定（`ControlStyle::row_height`），不支持可变行高。
- **Table 列宽固定**：不能拖拽调整列宽、没有横向滚动；排序由应用完成（只报告 `kSortRequested`）。表格持有一份单元格字符串副本，`set_rows()` 会清掉选择。
- **TreeView 模型是先序扁平列表**，展开/折叠是 O(n) 重算可见行；没有懒加载子节点。
- **Tabs 的隐藏面板借用 `NodeStyle::disabled` 实现"惰性"**：隐藏期间面板内节点 `is_disabled()` 为真；整树 `refresh_subtree()` 会给它们涂禁用色，重新选中时由 Tabs 恢复。标签条不会溢出滚动，标签过多会超出宽度。Enter/方向键之外没有 Space 激活（`Key` 无 Space）。
- **指针事件在按下（kDown）时生效**；控件不接 `Interaction`，悬停反馈只来自调用方自己的 `Interaction` 循环（标签与表头是普通 `kButton`）。
- **C ABI 没有导出这些组件**；视觉是最小版（`src/ui/control_visuals.cpp`），选中行的文字用 `on-primary`。

## E. 未实现 / 路线图缺口

以下按三类区分：**真实缺口**（该有但还没做）、**有意推迟**（有明确的技术理由，未来可能补）、**明确不做**（`doc/design.md` 已经写清楚不在这个项目的范围内）。这个区分很重要——第三类不要当 bug 报告。

### 真实缺口 / 有意推迟

- **布局语义空洞**：`row_reverse`/`column_reverse`（推迟理由：反转 remainder 分配会破坏字节级一致性测试的基线）、baseline 对齐（需要先有一次测量再定位，规划过但没有落地）、wrap 容器内 `align: stretch`、负 margin（当前实现遇负值会在 0 处截断，需要 `deflate` 语义配合才能对称让出空间）、百分比长度只覆盖 `width`/`height`/`min_*`/`max_*`（padding/margin/gap 不支持；参照为父节点可用内容盒，参照轴无界时退化为 auto 而不是报错，见 `doc/guide/layout.md`）、分数 grow 权重（当前只支持整数前缀和）
- **手势竞技场（gesture arena）未建**，惯性滚动/回弹（fling/inertia）缺失——都需要一个还不存在的动画时钟驱动的速度来源
- **Windows/macOS 从未验证**——当前只有 Linux/SDL3 一条实测路径
- **JS 绑定层（P6）完全未开始**
- **移动端（P8）未开始**——Android/iOS 宿主、生命周期、触摸手势都还没做
- **无公开的富文本编辑 API**：一个编辑框只有一种字号/颜色
- **TextArea 只是 MVP**：每次按键都对整段文字重新排版（O(n)，长文档会慢）；选区高亮受节点池大小限制（C ABI 为 64 个可见行，超出的行不画）；C ABI 不能注入 Shift+点击/Ctrl+Home/Ctrl+Left 之类带修饰键的指针或编辑键（`dg_debug_post_key` 能注入带修饰键的快捷键，如 `Mod+Z`）
- **编辑框的撤销与选词是简化版**（TextField/TextArea 共用）：撤销快照是整段文字的拷贝（深度上限 100，适合表单级文本，不适合大文档）；分词是按字素簇首码点分类的简单规则，连续的汉字/假名算一个词，没有词典分词；双击选词后拖动仍按字素扩展，不按词扩展；`max_length`（`editor_set_max_length()`）只有 C++ API，没有 ABI 属性；TextArea 里 Tab 始终切焦点，不能插入 `\t`
- **无 RTL 界面镜像**：BiDi 文本渲染本身是支持的，但整体界面镜像（flex 主轴反转、padding 左右互换、图标翻转）没有做
- **无竖排文本**
- **图片格式与 SVG 的边界**：位图只注册并测试了 PNG/JPEG/WebP（GIF/BMP/ICO/AVIF 等解码器未注册，送进来会被拒绝）；动图只取第一帧；无九宫格；解码全部同步。SVG 走 Skia 的 `SkSVGDOM`：没有设置字体管理器和资源提供者，所以 SVG 里的 `<text>` 不显示、`<image>`/外部引用不加载；每次绘制都重新画一遍矢量（无栅格缓存，复杂插画类 SVG 会贵）；`tint` 是整图 SrcIn 着色，只适合单色图标，多色图标会被压成一种颜色；C ABI 通过 `dg_node_set_image_data()`/`_file()` 的 `svg_tint_argb` 参数暴露解码时的 tint；绘制时的 tint 是节点属性 `image_tint`（`DG_PROP_IMAGE_TINT`，位图同样适用）。`ImageCatalog::decode()` 刻意不解析 SVG（主题包安全决定），SVG 只能经 `decode_any()`/`decode_svg()`/`decode_file()` 进入
- **字体 fallback 链对完全未覆盖的字符只画 `.notdef` 方框（tofu）**，见 A12——这不是"处理不好"，是链条设计本身：只覆盖显式列出的语言/字符集组合

### 明确不做（scope boundary，不是遗漏）

- **无障碍（accessibility）实现**：`doc/design.md` §11 明确写了"不做"，只留了架构挂点（一棵与渲染树平行但更稀疏的 a11y 节点树），不对接 UIA/NSAccessibility/AT-SPI 等平台无障碍框架。这是这个项目当前定位下的范围边界，不是"还没写"的缺口——如果需要无障碍支持，需要在这个挂点上自己接平台层，库本身不提供。
