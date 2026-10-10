# C ABI 参考

权威来源：`abi/drawgui.def.toml`（唯一真源）。`include/drawgui/abi/drawgui.h`
（纯 C 头，无 C++/Skia 类型）、`src/abi/drawgui_abi.generated.cpp`（try/catch
边界）、`abi/drawgui.d.ts`（TypeScript 环境声明）三者都由
`tools/gen_abi.py` 从它生成，三者中任何一个手改都会被
`tools/abi_lock.py --check` 拦下。

## 1. 能力边界（先看这个）

在细看每个函数之前，必须先知道这个 ABI **不能**做什么——这些不是遗漏，是
`abi/drawgui.def.toml` 文件头注释里点名拒绝的：

- **只能创建十六种节点**：`DG_NODE_TYPE_BOX`（=1，`WidgetKind::kPanel`）、
  `BUTTON`（=2，`kButton`）、`LABEL`（=3，`kLabel`）、`CHECKBOX`（=4，
  `kCheckbox`）、`TEXT_FIELD`（=5，`kTextField`）、`TEXT_AREA`（=6，
  `kTextArea`，0.2）、`SCROLL_VIEW`（=7，`kScrollView`，0.2）、`LIST`
  （=8，`kList`，0.2）、`IMAGE`（=9，0.2），以及 0.2 phase B 的组件
  `DROPDOWN`（=10）、`PROGRESS`（=11）、`TABS`（=12）、`TREE`（=13）、`TABLE`
  （=14），见第 5 节「组件」；0.2 的表单控件 `SLIDER`（=15，`kSlider`）和
  `RADIO`（=16，分组的 `kCheckbox`），见第 5 节「滑块与单选」。菜单、对话框、
  tooltip 不是节点，是窗口/节点上的函数。普通 `CHECKBOX` 仍没有分组（要互斥选择用
  `RADIO`）；text field 不导出任何 IME 组字（composition）接口——在
  `abi/drawgui.def.toml` 文件头 "WHAT IS DELIBERATELY NOT HERE"里具名拒绝。
- **容器**：`BOX` 默认是叶子（子节点叠放在左上角）；对它写
  `DG_PROP_DIRECTION`（`DG_DIRECTION_ROW`/`COLUMN`）就变成行/列容器，
  之后 `JUSTIFY`/`ALIGN`/`GAP`/`MAIN_SIZE` 生效，`DG_PROP_WRAP`
  （`DG_VALUE_BOOL`）让它换行（再加 `RUN_GAP`/`ALIGN_CONTENT`）。一旦成为
  容器就不能再变回叶子。`row_reverse`/`column_reverse` 未实现
  （`DG_ERR_UNSUPPORTED`）。
- **文字**：`dg_node_set_text(DG_PROP_TEXT)` 设字符串、`DG_PROP_TEXT_COLOR`
  设颜色、0.2 起 `DG_PROP_FONT_SIZE`（逻辑像素）和 `DG_PROP_TEXT_ALIGN`
  （`DG_TEXT_ALIGN_LEFT/CENTER/RIGHT`）；对 `TEXT_FIELD`/`TEXT_AREA` 这三个
  属性作用在编辑框的文字上（`TEXT_COLOR` 会在下一次主题切换时被主题色
  覆盖）。字体族只有 `dg_app_add_font()` 注册的首个字族，**没有字重/字族属性**
  （`TextStyle` 本身没有字重）。节点盒子不会随字号自动变高——大字号要自己
  加高 `DG_PROP_HEIGHT`。0.2：`DG_PROP_WRAP_TEXT`（`DG_VALUE_BOOL`）让节点文字
  按宽度折行；`DG_PROP_PADDING_*` 同时移动节点自己的文字；编辑框的
  `DG_PROP_PLACEHOLDER`（`dg_node_set_text()`）和文字内边距（默认 10px，写
  `PADDING_*` 修改，对称）。
- **尺寸**：节点创建时带默认尺寸（`BOX` 240×120 等，保持兼容）；0.2 的
  `DG_LENGTH_UNIT_AUTO` 把 `WIDTH`/`HEIGHT`（及 `MIN_*`/`MAX_*`）恢复为未设置——
  按内容尺寸，或由父容器 stretch/`GROW` 决定。`LIST`/`TREE`/`TABLE` 跟随布局后的
  尺寸（`GROW`、stretch、百分比都生效）；`TABS` 的标签栏背景铺满节点宽度。
- **主题**：0.2 起 `BUTTON`/`CHECKBOX`/`RADIO` 的颜色取自主题 token 并随切换
  重涂；写入或绑定在它们上的 `BACKGROUND_COLOR`（悬停/按下色由它派生）、
  `TEXT_COLOR`、`BORDER_COLOR` 优先。
- **节点移动只能在同一窗口内**：`dg_node_insert_before` 可以按 `ref`
  定位插入、也可以移动已挂上的节点，但跨窗口移动返回
  `DG_ERR_ALREADY_ATTACHED`。
- **图片**：0.2 起 `dg_node_set_image_data()`/`dg_node_set_image_file()`
  把 PNG/JPEG/WebP/SVG 解码进 app 级的图片表并显示在 `IMAGE`（或 `BOX`）
  节点上，SVG 可在解码时整体着色（`svg_tint_argb`），0.2 的
  `DG_PROP_IMAGE_TINT` 则在绘制时给任意图片着色（可绑定主题 token）；同步解码、图片在 app 存活期间一直占内存（没有
  卸载接口）。渐变/阴影/变换三个复杂属性 setter 仍未导出。
- **没有动画 API、没有自定义绘制、没有回调模式**——弹层（菜单/tooltip/
  对话框）在 phase B 已导出，同样是轮询形状。其余几项在 `abi/drawgui.def.toml`
  文件头注释里被具名拒绝（`dg_set_event_callback`、
  `dg_custom_paint_set_cmds`、`dg_animate`/`dg_node_set_transition`），
  理由是各自"没有真实调用方可供提炼接口"，而不是技术上做不到。
- **`abi/drawgui.d.ts` 生成了，但本仓库里没有任何消费者**——没有 JS/TS
  框架层读它。这是"一个已敲定的目标形状，而非已被证明的形状"（"a settled
  TARGET shape, not a proven one"，见 `abi/drawgui.d.ts` 文件头的生成器注释）。

## 2. 四个不透明句柄

| 类型 | 含义 |
|---|---|
| `dg_app_t` | 一个 drawgui 应用：窗口管理器及其拥有的全部窗口/节点 arena |
| `dg_window_t` | 一个 OS 窗口及其布局/渲染/控件状态 |
| `dg_node_t` | 一个布局/渲染节点——pending（尚未挂到树上）或 live（已挂在某窗口下） |
| `dg_theme_t` | 一个已加载的主题（token→值表，可能还带着它来源的、不可信的包目录） |

**永不释放**：每个句柄都是一个仅分配一次、从不释放的小包装
（`{index, generation}`，8 字节），从不是资源本体。这意味着：在
`dg_node_remove()`/`dg_app_destroy()` 之后再用这个句柄，读到的是两个仍然
分配着的普通整数，而不是已释放内存——一个失效句柄 bug 表现为普通的、ASan
也测不出异常的 `DG_ERR_INVALID_HANDLE` 返回值，而不是真正的 use-after-free。

## 3. 五个结构体（首字段均为 `size`）

调用方必须先 `memset` 清零结构体、再设置 `size = sizeof(struct)`，这是
版本兼容机制：未来某个 MINOR 版本在结构体末尾追加字段时，能通过 `size`
辨别调用方的旧结构体是否比当前定义短，而不会读越界。

| 结构体 | 用途 |
|---|---|
| `dg_value` | 一个带类型标签的属性值——`type`（`DG_VALUE_*`）、`number`（浮点/长度）、`bits`（颜色 ARGB、枚举序号或布尔 0/1） |
| `dg_app_opts` | 应用级选项，目前为空，只留 `size` 供未来扩展 |
| `dg_window_opts` | 窗口初始状态：`title`（创建时拷贝，不持有）、`width`、`height`、`background_argb`；0.2 起追加 `resizable`、`min_width`/`min_height`、`max_width`/`max_height`（0 = 该轴不限制）——只有 `size` 覆盖到这些字段时才读取，0.1 编出来的短结构体照旧得到不可调整大小、无限制的窗口 |
| `dg_event` | `dg_poll_events()` 的一条事件：`kind`、`window`、`node`、`x`/`y`、`action_id`，0.2 追加 `clicks`（`uint16_t`，放在原来的尾部填充里，`sizeof(dg_event)` 不变，64 位上仍是 40 字节）。各字段按 `kind` 解释：`CLICK`/`CONTEXT_CLICK` 的 `x`/`y` 是指针位置、`clicks` 是连击数（2 = 双击）；`LIST_SELECT` 的 `x` 是行号（键盘选中时 `clicks` 为 0）；`TIMER` 的 `x` 是定时器 id；`USER` 的 64 位负载拆在 `x`（低 32 位）和 `y`（高 32 位）；`WINDOW_MOVED` 是新的左上角（逻辑屏幕坐标），`WINDOW_RESIZED` 是新的可绘制宽/高（设备像素）；`FOCUS_CHANGED` 的 `node` 是新的焦点（NULL = 无焦点）。phase B 只追加 kind、复用字段（大小仍是 40 字节）：`MENU_ACTIVATE` 的 `x` = 项序号、`y` = 菜单 id、`node` = 锚点；`MENU_DISMISS` 的 `x` = -1、`y` = 菜单 id；`VALUE_CHANGED` 的 `node` = 下拉框、`x` = 新选项、`y` = 旧选项（-1 = 无）；`DIALOG_RESULT` 的 `x` = 按钮（-1 = 没有取消按钮时的 Escape）、`y` = 对话框 id、`node` 为 NULL；`TAB_SELECT` 的 `x` = 标签；`TREE_SELECT`/`_EXPAND`/`_COLLAPSE` 的 `x` = 条目；`TABLE_SELECT` 的 `x` = 行、指针选中时 `clicks` 为连击数；`TABLE_SORT` 的 `x` = 列；`SLIDER_CHANGED` 的 `node` = 滑块、`x`/`y` 为 0（新值用 `dg_slider_get_value()` 查询）；方向键选中 `RADIO` 产生的 `CLICK` 的 `clicks` 和 `x`/`y` 为 0 |
| `dg_theme_err` | `dg_theme_load_dir`/`_memory` 失败时的机读原因（`DG_THEME_ERR_*`） |

## 4. 常量组

| 组 | 前缀 | 数量 | 说明 |
|---|---|---|---|
| `node_type` | `DG_NODE_TYPE_` | 16 | `BOX`(1)、`BUTTON`(2)、`LABEL`(3)、`CHECKBOX`(4)、`TEXT_FIELD`(5)；0.2 追加 `TEXT_AREA`(6)、`SCROLL_VIEW`(7)、`LIST`(8)、`IMAGE`(9)（见第 1 节与第 5 节「滚动、列表与图片」），phase B 追加组件 `DROPDOWN`(10)、`PROGRESS`(11)、`TABS`(12)、`TREE`(13)、`TABLE`(14)（见第 5 节「组件」），再追加 `SLIDER`(15)、`RADIO`(16)（见第 5 节「滑块与单选」）。`TEXT_AREA` 是多行编辑框，默认 320×160，文字距边框 10px，与 `TEXT_FIELD` 共用一套取自当前主题的颜色和焦点边框（底色 `color.surface`、文字 `color.on-surface`、边框 `color.border`、焦点边框和光标 `color.primary`、选中底色为半透明 `color.primary`，切主题立即生效）；Enter 换行、Up/Down 保持列、Home/End 按视觉行、Ctrl+Home/End 到全文首尾、PageUp/PageDown 翻页、按住拖动选择；滚轮和自带滚动条（`color.scrollbar-*`/`size.scrollbar`）只滚动视口；Tab 仍切焦点 |
| `value_type` | `DG_VALUE_` | 6 | `FLOAT`/`LENGTH`/`COLOR`/`ENUM`/`TOKEN`/`BOOL`（`TOKEN` 是 7-6 加入的主题绑定值类型；`BOOL` 的 `bits` 只能是 0/1，否则 `DG_ERR_VALUE_OUT_OF_RANGE`，首个使用者是 `DG_PROP_DISABLED`） |
| `event_kind` | `DG_EVENT_` | 32 | `WINDOW_CLOSED`(1)、`CLICK`(2)、`ACTION`(3)、`TEXT_CHANGED`(4，`TEXT_FIELD`/`TEXT_AREA` 文字被用户编辑，只带 `window`/`node`，新文字用 `dg_node_get_text()` 取)；0.2 追加窗口状态事件 `WINDOW_RESIZED`(5)、`WINDOW_MOVED`(6)、`WINDOW_MINIMIZED`(7)、`WINDOW_MAXIMIZED`(8)、`WINDOW_RESTORED`(9)、`WINDOW_FOCUS_GAINED`(10)、`WINDOW_FOCUS_LOST`(11)、`WINDOW_FULLSCREEN_ENTERED`(12)、`WINDOW_FULLSCREEN_LEFT`(13)，以及 `FOCUS_CHANGED`(14)、`TIMER`(15)、`USER`(16)、`CONTEXT_CLICK`(17，右键按下)、`LIST_SELECT`(18)；phase B 追加 `MENU_ACTIVATE`(19)、`MENU_DISMISS`(20)、`VALUE_CHANGED`(21)、`DIALOG_RESULT`(22)、`TAB_SELECT`(23)、`TREE_SELECT`(24)、`TREE_EXPAND`(25)、`TREE_COLLAPSE`(26)、`TABLE_SELECT`(27)、`TABLE_SORT`(28)；再追加 `SLIDER_CHANGED`(29)、`ROW_PART`(30)；最后是 `CLOSE_REQUESTED`(31，见第 5 节「关闭确认与动画」)、`ANIMATION_FINISHED`(32，`node` = 动画节点，`x` = 动画 id)。字段含义见第 3 节 `dg_event`。`RESIZED` 只在尺寸真的变了时才报；多数平台（包括 dummy 驱动）在窗口创建后会先报一次 `FOCUS_GAINED` |
| `length_unit` | `DG_LENGTH_UNIT_` | 3 | 0.2：`DG_VALUE_LENGTH` 的 `bits` 指明单位：`PX`(0，以前 `bits` 总是 0，含义不变)、`PERCENT`(1，父节点可用内容盒的百分比，50 = 一半)、`AUTO`(2，忽略 `number`，字段回到未设置：按内容尺寸/由父容器决定，`MIN_*` 为 0、`MAX_*` 无上限)；后两者只有 `WIDTH`/`HEIGHT`/`MIN_*`/`MAX_*` 接受；其它值返回 `DG_ERR_VALUE_OUT_OF_RANGE` |
| `curve` | `DG_CURVE_` | 4 | 0.2：`dg_node_animate()`/`dg_node_set_transition()` 的缓动：`LINEAR`(1)、`EASE_IN`(2)、`EASE_OUT`(3)、`EASE_IN_OUT`(4) |
| `pointer_button` | `DG_POINTER_BUTTON_` | 3 | 0.2：`PRIMARY`(1)、`SECONDARY`(2)、`MIDDLE`(3)，`dg_debug_post_pointer_button_ex()` 用 |
| `pixel_format` | `DG_PIXEL_FORMAT_` | 2 | 0.2：`BGRA8888`(1)、`RGBA8888`(2)，`dg_window_set_icon()` 的像素字节序 |
| `window_state` | `DG_WINDOW_STATE_` | 5 | 0.2：`dg_window_get_state()` 返回值的位：`MINIMIZED`(1)、`MAXIMIZED`(2)、`FULLSCREEN`(4)、`FOCUSED`(8)、`RESIZABLE`(16) |
| `menu_item_flag` | `DG_MENU_ITEM_` | 4 | phase B：`dg_menu_open()` 每项的位：`DISABLED`(1)、`CHECKED`(2，隐含可勾选)、`SEPARATOR`(4)、`CHECKABLE`(8，预留勾选列、未勾选) |
| `tree_item_flag` | `DG_TREE_ITEM_` | 1 | phase B：`EXPANDED`(1)，`dg_tree_set_items()` 用 |
| `table_column_flag` | `DG_TABLE_COLUMN_` | 1 | phase B：`SORTABLE`(1)，`dg_table_set_columns()` 用 |
| `error` | `DG_ERR_` | 13 | 见下 |
| （枚举属性值） | `DG_<PROP>_` | — | 0.2：每个枚举属性的取值序号，例如 `DG_CURSOR_TEXT`、`DG_IMAGE_FIT_COVER`、`DG_DIRECTION_ROW`，与 `prop_ids.generated.h` 同名同值（同样只在 C 下以 `#define` 出现，C++ 用生成头里的 `inline constexpr`）；d.ts 里是 `prop_enum` |
| `theme_err` | `DG_THEME_ERR_` | 9 | 主题包加载失败原因，一一对应 `dg::ThemeLoadStatus` |

`error` 常量组（13 个值，对应 `dg::PropStatus`/ABI 自身的失败原因）：
`OK`(0)、`OOM`(-1)、`INTERNAL`(-2)、`INVALID_ARGUMENT`(-3)、
`INVALID_HANDLE`(-4)、`UNKNOWN_ID`(-5)、`TYPE_MISMATCH`(-6)、
`VALUE_OUT_OF_RANGE`(-7)、`NOT_APPLICABLE`(-8)、`UNSUPPORTED`(-9)、
`ALREADY_ATTACHED`(-10)、`NO_WINDOW`(-11)、`NO_ACTIVE_THEME`(-12)。

## 5. 导出函数（共 116 个）

按用途分组；每个函数一句话说明。

### 生命周期

| 函数 | 用途 |
|---|---|
| `dg_abi_version()` | 返回 `MAJOR<<16 \| MINOR` |
| `dg_app_create(opts)` | 启动窗口管理器；`opts` 可为 NULL（取默认值） |
| `dg_app_add_font(app, directory, family)` | 必须在这个 app 的第一次 `dg_window_create()` 之前调用；扫描一次 `directory`、注册 `family`（该机器没有这个字族则失败，从不静默替换）；第一次成功调用的字族成为 `DG_NODE_TYPE_LABEL` 的默认字体 |
| `dg_app_destroy(app)` | 关闭全部窗口并关掉平台层；之后 `app` 失效 |

### 窗口

| 函数 | 用途 |
|---|---|
| `dg_window_create(app, opts)` | 立即开一个窗口，首帧绘制前用 `background_argb` 填充 |
| `dg_window_set_root(window, node)` | 把（必须是 pending 的）`node` 挂到窗口树，作为唯一内容根 |
| `dg_window_set_title(window, utf8)` / `dg_window_get_title(window, buf, cap, out_len)` | 0.2：改/读标题；读取是和 `dg_node_get_text()` 一样的两趟调用 |
| `dg_window_set_size(window, w, h)` / `dg_window_get_size(window, &w, &h)` | 0.2：逻辑单位（与 `dg_window_opts::width/height` 相同）；受最小/最大尺寸约束；结果以 `DG_EVENT_WINDOW_RESIZED` 确认（事件里是设备像素） |
| `dg_window_set_position(window, x, y)` / `dg_window_get_position(window, &x, &y)` | 0.2：左上角，逻辑屏幕坐标；Wayland 之类的窗口系统可能忽略设置、读回 0,0 |
| `dg_window_minimize` / `_maximize` / `_restore(window)` | 0.2：请求最小化/最大化/还原，对应事件确认。**dummy 驱动不支持，返回 `DG_ERR_UNSUPPORTED`** |
| `dg_window_set_fullscreen(window, on)` | 0.2：进入/退出全屏，`WINDOW_FULLSCREEN_ENTERED/LEFT`（通常还有 `RESIZED`）确认 |
| `dg_window_set_resizable(window, on)` | 0.2：是否允许用户拖拽改变大小（dummy 驱动接受但从不报告为可调整） |
| `dg_window_set_min_size` / `_max_size(window, w, h)` | 0.2：逻辑单位，0 = 该轴不限制；负数或最小值大于最大值（反之亦然）返回 `DG_ERR_UNSUPPORTED` |
| `dg_window_set_icon(window, pixels, w, h, row_bytes, format)` | 0.2：32 位直通 alpha 像素，调用返回前已拷贝；`format` 为 `DG_PIXEL_FORMAT_*`。空指针、非正尺寸、`row_bytes < w*4`、未知格式返回 `DG_ERR_INVALID_ARGUMENT`。**dummy 驱动没有图标，合法图像也返回 `DG_ERR_UNSUPPORTED`** |
| `dg_window_get_state(window)` | 0.2：非负返回值是 `DG_WINDOW_STATE_*` 位掩码，负数为 `DG_ERR_*` |

窗口操作的错误约定：句柄为 NULL 或窗口已不存在返回 `DG_ERR_INVALID_HANDLE`；参数在调用平台前校验，不合法返回 `DG_ERR_INVALID_ARGUMENT`；平台拒绝请求返回 `DG_ERR_UNSUPPORTED`，`dg_last_error()` 里是平台给出的原因。真实显示服务器上最小化/最大化/全屏等是异步生效的：返回 `DG_ERR_OK` 只表示请求被接受，对应的 `DG_EVENT_WINDOW_*` 才表示真的发生了。

### 节点

| 函数 | 用途 |
|---|---|
| `dg_node_create(app, type_id)` | 分配一个 pending 节点；`type_id` 只能是 `DG_NODE_TYPE_*` 的十六个值之一 |
| `dg_node_set_prop(node, prop_id, value)` | 标量属性写入，`dg::set_prop()` 的 ABI 映射；对 pending 或 live 节点都有效。写 `DG_PROP_DISABLED`（`DG_VALUE_BOOL`）成功后会自动取消该子树上的按压/hover（不产生 `DG_EVENT_CLICK`）、让落在其中的焦点失焦并重绘子树；禁用的 button/checkbox/text field 用主题 token `color.disabled`/`color.on-disabled` 着色。0.2：`DG_PROP_CURSOR`（`DG_VALUE_ENUM`，值为 `DG_CURSOR_*`）设置指针形状，事件泵在每次指针移动/按下/抬起后自动解析并切换光标（只在形状变化时才调用平台）；改了属性但指针没动时，要等下一次指针事件才生效。0.2：`DG_PROP_SCROLLBAR`（`DG_VALUE_ENUM`，`DG_SCROLLBAR_AUTO/ALWAYS/NEVER`）可以写入并会被校验；`TEXT_AREA`/`SCROLL_VIEW`/`LIST` 自带滚动条，写在它们上面决定滚动条何时显示（C++ 场景见 `doc/guide/widgets.md`「滚动条」）。0.2 phase A：`DG_PROP_DIRECTION` 让 `BOX` 变成容器（见第 1 节）；`DG_PROP_TAB_INDEX`（`DG_VALUE_FLOAT` 整数：正数按升序先走，0 按树序，负数退出 Tab 序列但仍可点击；只对可聚焦节点）和 `DG_PROP_MAX_LENGTH`（`DG_VALUE_FLOAT` 非负整数，按字素簇计，0 = 不限；只对编辑框，已有文字不截断）是控件状态，底层 C++ 的 `dg::set_prop()` 对它们返回 `kUnsupported`，由 C ABI 与 `dg::Window::set_prop()` 处理 |
| `dg_node_set_text(node, prop_id, utf8, len)` | 专用 setter 通道（`prop_id` 是 `DG_PROP_TEXT`，或 0.2 的 `DG_PROP_PLACEHOLDER`——只对 `TEXT_FIELD`/`TEXT_AREA`，编辑框为空且未组字时显示的半透明提示，其它节点 `DG_ERR_NOT_APPLICABLE`）：UTF-8 字符串装不进 `dg_value` 的定长联合，形状与 `dg::set_gradient/_shadow/_image` 一致；只对 live 节点有效（pending 返回 `DG_ERR_NO_WINDOW`）。0.2：对 `TEXT_FIELD`/`TEXT_AREA` 写的是**编辑框的文字模型**（光标移到末尾、清掉选区、组字和撤销历史，TextField 丢弃换行），不会产生 `DG_EVENT_TEXT_CHANGED` |
| `dg_node_get_checked(node)` | 查询 live `CHECKBOX`（0.2 起也包括 `RADIO`）的勾选状态：返回 1/0，负数为 `DG_ERR_*`（其它类型为 `DG_ERR_UNSUPPORTED`）；"收到 CLICK 再查询"的形状 |
| `dg_node_get_text(node, buf, cap, out_len)` | 读取 live `TEXT_FIELD`/`TEXT_AREA` 的当前文字：两趟调用（先 `cap=0` 取 `out_len` 全长，再分配缓冲读取）；按字节截断 |
| `dg_node_insert_before(parent, child, ref)` | 0.2：`parent` 是 `SCROLL_VIEW` 时插进它的内容节点；`parent` 是 `LIST` 返回 `DG_ERR_UNSUPPORTED`。把 `child` 插到 live 的 `parent` 下、紧挨在 `ref` 之前（`ref` 为 NULL 时追加到末尾）。`ref` 必须是 `parent` 的直接子节点且不是 `child` 本身，否则 `DG_ERR_INVALID_ARGUMENT`。pending 的 `child` 变成 live；已在**同一窗口** live 的 `child` 被**移动**（身份、控件状态、句柄都保留）——keyed reconciler 靠它重排。把节点移到它自己或它的后代下面是环，返回 `DG_ERR_INVALID_ARGUMENT`；`child` 在另一个窗口里 live 时返回 `DG_ERR_ALREADY_ATTACHED` |
| `dg_node_remove(node)` | 使句柄失效（此后任何调用返回 `DG_ERR_INVALID_HANDLE`）。对 LIVE 节点，还会把整棵子树从树上摘除：子树里每个节点的控件/主题/焦点/动画/action/交互状态都被清理（`dg::remove_subtree`），腾出的区域重绘、原父节点重新布局，子树里**所有**有句柄的后代句柄也一并失效。删除窗口根节点会清空该窗口的根，之后可以再 `dg_window_set_root()`。pending 节点只失效句柄 |

### 焦点（0.2）

| 函数 | 用途 |
|---|---|
| `dg_node_focus(node)` | 把键盘焦点给一个 live 的可聚焦节点（`BUTTON`/`CHECKBOX`/`RADIO`/`SLIDER`/`TEXT_FIELD`/`TEXT_AREA`/`DROPDOWN`，以及 `TABS`（聚焦选中的标签）/`TREE`/`TABLE`/`LIST`），效果等同点击；不可聚焦或已禁用返回 `DG_ERR_UNSUPPORTED` |
| `dg_window_get_focus(window)` | 当前焦点节点，没有焦点时为 NULL |
| `dg_window_clear_focus(window)` | 让窗口失去焦点 |

焦点每次变化（点击、Tab/Shift+Tab、上面两个函数、焦点节点被禁用或删除）都产生一条 `DG_EVENT_FOCUS_CHANGED`，`node` 是新焦点（NULL = 无）。Tab/Shift+Tab 由事件泵按树序（`DG_PROP_TAB_INDEX` 可调）遍历，编辑框里也一样。

### 滚动、列表与图片（0.2）

| 函数 | 用途 |
|---|---|
| `dg_node_get_scroll_offset(node, &x, &y)` | `SCROLL_VIEW`/`LIST`/`TEXT_AREA` 当前滚动偏移（设备像素） |
| `dg_list_set_items(node, items, count, item_height)` | 用 `count` 个 UTF-8 字符串替换 `LIST` 的行（调用返回前拷贝）；`item_height` 为行高，0 = 保持（默认 28）；清空选中、把滚动偏移夹到新内容内 |
| `dg_list_get_selected(node)` / `dg_list_set_selected(node, index)` | 读/写选中行，-1 = 无；设置不滚动、不产生事件 |
| `dg_list_set_rows(node, texts, secondaries, icons, flags, trailing_actions, count, item_height)` | 富行版 `dg_list_set_items()`：每行主文字、右对齐的暗色副文字、图标（`dg_app_load_image()` 的 id，0 = 无）、`flags`（`DG_ROW_CHECKABLE`/`DG_ROW_CHECKED`：行首复选框）、`trailing_actions[i]`（显示 `dg_list_set_row_actions()` 的前 N 个按钮，>8 返回 `DG_ERR_VALUE_OUT_OF_RANGE`）；除 `texts` 外数组都可为 null；行节点池大小不变 |
| `dg_list_set_row_actions(node, icons, tooltips, count)` | 整个列表共享的行尾操作按钮（最多 8 个），悬停显示 tooltip；按下产生 `DG_EVENT_ROW_PART`（`x = DG_ROW_PART_ACTION`，`y` = 按钮序号），**不改变选中** |
| `dg_list_row_checked(node, row)` | 行复选框状态 1/0；没有复选框返回 `DG_ERR_NOT_APPLICABLE` |
| `dg_list_set_row_keys(node, keys, count)` | 每行一个应用自定义 `uint64_t` 键（`count` 必须等于行数），事件里以 `dg_event::item_key` 回传；drawgui 从不解引用。下一次 `dg_list_set_items()`/`_set_rows()` 替换它 |
| `dg_tree_set_items_ex(node, labels, depths, flags, icons, secondaries, count)` | 带图标、复选框（`DG_TREE_ITEM_CHECKABLE`/`CHECKED`）、标签后暗色副文字的 `dg_tree_set_items()` |
| `dg_tree_row_checked(node, index)` / `dg_tree_set_item_keys(node, keys, count)` | 条目（先序下标）的复选框状态；每个条目一个键 |
| `dg_table_set_cells_ex(node, cells, icons, flags, rows)` | 带每格图标、复选框（`DG_ROW_*`）的 `dg_table_set_cells()`，按行主序 |
| `dg_table_cell_checked(node, row, column)` / `dg_table_set_row_keys(node, keys, count)` | 单元格复选框状态；每行一个键（下次设置单元格时清空） |
| `dg_node_set_image_data(node, bytes, len, svg_tint_argb)` | 解码 PNG/JPEG/WebP/SVG（以 `<` 开头的当 SVG）并显示在 `IMAGE`/`BOX` 上；`svg_tint_argb` alpha 非 0 时把 SVG 整体着成该色，0 = 不着色；数据视为可信的应用资源 |
| `dg_node_set_image_file(node, path, svg_tint_argb)` | 同上，从文件读 |

富行的点击规则：复选框和行尾按钮优先于行本身——按下它们**不改变选中**，只产生 `DG_EVENT_ROW_PART`（复选框先切换状态再入队，`x = DG_ROW_PART_CHECK`）；`TREE`/`TABLE` 获得焦点时按空格切换选中行（表格：该行第一个可勾选的单元格）的复选框。按在行上时除选择事件外还有 `DG_EVENT_CLICK`（`item` = 行/先序条目，表格另有 `column`，`item_key` = 行键）；右键先选中该行（照常产生选择事件），再产生带 `item`/`column`/`item_key` 的 `DG_EVENT_CONTEXT_CLICK`。获得焦点的 `LIST` 也一样（空格切换选中行的复选框）。

- `SCROLL_VIEW`：240×160 的裁剪视口，内部有一个看不见的内容节点（垂直滚动时是列，`DG_PROP_SCROLL_AXIS = DG_SCROLL_AXIS_HORIZONTAL` 时是行），`dg_node_insert_before(scroll_view, …)` 的子节点都进内容节点。写在它上面的容器属性（`DIRECTION`/`JUSTIFY`/`ALIGN`/`GAP`/`MAIN_SIZE`/`WRAP`/`RUN_GAP`/`ALIGN_CONTENT`）转给内容节点，其余属性作用于视口。滚轮、拖动/点击滚动条可滚动；焦点在它里面时 PageUp/PageDown/Home/End 也滚动它（仍会报 `DG_EVENT_ACTION`）。滚动条槽位通过视口的右（横向时为下）内边距预留，自己写 `PADDING_R/B` 会覆盖它。
- `LIST`：240×200 的虚拟化单列文本列表，32 个行节点循环复用，所以条目数不受限，但同时可见的行最多 30 行。数据是推送式的（`dg_list_set_items()`），不是回调拉取——C 调用方不需要实现回调，代价是全部字符串都拷贝一份放在 drawgui 里。主键按下某行即选中（`color.primary` 半透明底色）并产生 `DG_EVENT_LIST_SELECT`（`x` = 行号，`clicks` = 连击数，双击通常当作"激活"）。可聚焦（点击、Tab、`dg_node_focus()`）：获得焦点时 Up/Down/Home/End/PageUp/PageDown 移动选中并滚动到该行可见，产生 `clicks = 0` 的 `DG_EVENT_LIST_SELECT`，空格切换选中行的复选框，Shift+F10/Menu 键的 `DG_EVENT_CONTEXT_CLICK` 带选中行。只能垂直滚动（写 `SCROLL_AXIS` 返回 `DG_ERR_UNSUPPORTED`）；`DG_PROP_FONT_SIZE` 写在列表上作用于每一行。
- `IMAGE`：64×64、无底色，默认 `DG_IMAGE_FIT_CONTAIN`；`DG_PROP_IMAGE_FIT`/`IMAGE_PLACEHOLDER_COLOR` 适用。

### 组件（0.2 phase B）

`src/ui` 的组件层，轮询形状、没有回调：事件泵按「对话框 → 菜单 → 下拉框 →
标签页/树/表格 → 普通控件」的顺序把每个指针/按键/文字事件先交给组件，组件的
类型化结果变成一条 `dg_event`（字段含义见第 3 节）；tooltip 的延迟和不确定进度条的
动画会缩短 `dg_wait_events()` 的等待，所以永久等待（`-1`）也能按时醒来；布局之后
泵会调组件的 `sync()`。组件节点被 `dg_node_remove()`（或其祖先被删除）时组件状态一并
释放，弹层节点也被回收——`dg_debug_node_count()` 回到基线。主题切换时
`DROPDOWN` 重新着色，其余四种重建内部节点（保留状态；节点数不变）。

| 函数 | 用途 |
|---|---|
| `dg_menu_open(window, anchor, x, y, labels, flags, count)` | 打开菜单：`anchor` 非 NULL 时出现在该节点下方（最小宽度同节点），否则在 `x`/`y`（例如 `CONTEXT_CLICK` 的位置）；`flags` 为 `DG_MENU_ITEM_*`（可为 NULL）。返回菜单 id（正数）或负的 `DG_ERR_*`。上/下/Home/End 移动、Enter 或点击激活、Escape 或点外面关闭、按字母跳转。每窗口一个：再开会先关旧的（报 `MENU_DISMISS`）；对话框打开时也可以开，菜单叠在对话框之上，Escape/点外面只关菜单、对话框及其焦点陷阱保留，对话框内容里右键照常产生 `CONTEXT_CLICK`；没有可选项返回 `DG_ERR_INVALID_ARGUMENT` |
| `dg_menu_close(window)` | 静默关闭（不产生事件），焦点回到原处 |
| `dg_dropdown_set_items(node, items, count)` | `DROPDOWN` 的选项（拷贝），清空选中；`dg_node_set_text(DG_PROP_TEXT)` 设置未选中时的占位文字 |
| `dg_dropdown_get_selected(node)` / `dg_dropdown_set_selected(node, index)` | 读/写选中项，-1 = 无；设置不产生事件。用户在菜单里选了不同项时产生 `VALUE_CHANGED`；主键按下或聚焦时 Down/Enter 打开菜单 |
| `dg_node_set_user_data(node, value)` / `dg_node_get_user_data(node, &value)` | 节点上的一个不透明 `uint64_t`（下标、转成整数的指针……），供调用方自己记账：drawgui 从不读取、从不解引用，事件也不带它。pending 和 live 节点都可用（pending 时设的值在挂上树后保留）；没设过为 0；随节点删除而消失（之后返回 `DG_ERR_INVALID_HANDLE`）。`value` 指针为 NULL 返回 `DG_ERR_INVALID_ARGUMENT` |
| `dg_node_set_tooltip(node, utf8)` | 给 live 节点挂 tooltip（拷贝）；悬停该节点或其没有自己 tooltip 的后代满延迟后弹出在下方，移开/按下/滚轮/离开窗口隐藏；NULL 或空串移除 |
| `dg_app_set_tooltip_delay(app, delay_ms)` | 0..60000 ms（默认 500），对已打开的窗口也生效 |
| `dg_dialog_open(window, title, body, buttons, count, default_button, cancel_button)` | 全窗口遮罩上的模态对话框；`count` 为 0 时一个「OK」按钮；`default_button` 获得初始焦点并响应 Enter，`cancel_button` 响应 Escape（-1 = 无，Escape 报 -1）。返回对话框 id。打开期间窗口的全部事件都给它（Tab 在按钮间循环），已开的菜单先被关闭。每窗口一个 |
| `dg_dialog_close(window)` | 静默关闭 |
| `dg_menu_open_ex(window, anchor, x, y, labels, flags, shortcut_labels, commands, count)` | `dg_menu_open()` 加每项右对齐的快捷键文字（只显示，常用 `dg_chord_label()` 生成）和命令号；选中时 `MENU_ACTIVATE` 的 `command` 带回该项命令号（0 = 无）。两个数组都可为 NULL |
| `dg_menubar_set_menus(node, titles, entry_counts, menu_count, labels, shortcut_labels, flags, commands, entry_total)` | 替换 `MENUBAR` 的全部菜单（会关闭已展开的）：第 i 个菜单拥有扁平数组里接下来的 `entry_counts[i]` 项；`entry_counts` 之和必须等于 `entry_total`，否则 `DG_ERR_INVALID_ARGUMENT`。点标题在其下方展开菜单（再点同一标题收起），展开时指针移到别的标题即切换；F10 或单独按下并松开 Alt 进入键盘模式（焦点不动，高亮标题），Left/Right 在标题间移动（菜单展开时切换到相邻菜单），Down/Enter 展开，Escape 先关菜单再退出键盘模式。选中报 `MENU_ACTIVATE`：`node` = 菜单栏、`column` = 第几个菜单、`x`/`item` = 项、`command`；关闭报 `MENU_DISMISS`（`column` 同上）。没有子菜单、没有助记符 |
| `dg_menubar_set_entry(node, menu, item, label, shortcut_label, flags, command)` | 替换一项（例如切换勾选/禁用）；不存在的菜单/项为 `DG_ERR_VALUE_OUT_OF_RANGE`；该菜单正展开时先收起 |
| `dg_dialog_open_ex(window, title, body, buttons, count, default_button, cancel_button, content)` | 表单对话框：`content`（可为 NULL）是本窗口一个 live 节点（不能是窗口根），打开期间被挪进对话框正文（正文文字下方、按钮上方，宽度同正文），关闭后**脱离原父节点、保持存活**（停放在不可见处），由调用方用 `dg_node_insert_before()` 放回、复用或删除。焦点陷阱包含内容里的控件（Tab 在内容与按钮间循环）；内容里的键盘/文字/指针输入照常交给控件；Enter 触发默认按钮（焦点在 `TEXT_AREA` 里时是换行），Escape 取消 |
| `dg_dialog_set_button_disabled(window, dialog_id, button, disabled)` | 禁用/启用打开中的对话框的某个按钮：变暗、Tab 跳过、点击和 Enter（作为默认按钮时）都不触发。对话框没开（或 id 不对）为 `DG_ERR_UNKNOWN_ID`，按钮越界为 `DG_ERR_VALUE_OUT_OF_RANGE` |
| `dg_tabs_add(node, label, disabled)` | 给 `TABS` 追加一个标签，返回它的面板：一个填满面板区域的列式 `BOX`，用 `dg_node_insert_before()` 往里放内容；最多 64 个；失败返回 NULL |
| `dg_tabs_get_selected(node)` / `dg_tabs_set_selected(node, index)` | 读/写选中标签；禁用的标签返回 `DG_ERR_VALUE_OUT_OF_RANGE`；设置不产生事件 |
| `dg_tree_set_items(node, labels, depths, flags, count)` | 先序扁平列表（拷贝）：首项深度 0，每项最多比前一项深 1（下一项更深即有子项）；`flags` 为 `DG_TREE_ITEM_*`。清空选中 |
| `dg_tree_get_selected(node)` / `dg_tree_set_selected(node, index)` | 设置会展开祖先并滚动到可见；-1 清空选中（重建，滚动回顶部） |
| `dg_tree_set_expanded(node, index, expanded)` | 展开/折叠；没有子项的条目被接受并忽略 |
| `dg_table_set_columns(node, titles, widths, units, flags, count)` | 1..64 列：宽度按 `DG_LENGTH_UNIT_PX`/`PERCENT`，≤0 平分剩余宽度；`flags` 为 `DG_TABLE_COLUMN_*`。清空行、选中和排序指示 |
| `dg_table_set_cells(node, cells, rows)` | 按行主序推送 `rows × 列数` 个字符串（拷贝），清空选中 |
| `dg_table_get_selected(node)` / `dg_table_set_selected(node, row)` | 读/写选中行，-1 清空 |
| `dg_table_set_sort_indicator(node, column, ascending)` | 在表头显示升/降序箭头，-1 去掉；drawgui 不排序 |

- `DROPDOWN`：200×32，可聚焦，边框按钮显示选中项文字。
- `PROGRESS`：200×8，`DG_PROP_VALUE`（`DG_VALUE_FLOAT`，0..1）和 `DG_PROP_INDETERMINATE`（`DG_VALUE_BOOL`）；这两个属性只对 `PROGRESS` 有效，C++ 的 `dg::set_prop()` 返回 `kUnsupported`。不可聚焦。
- `TABS`：320×240；点标签或焦点在标签上时 Left/Right/Home/End/Enter 选中并产生 `TAB_SELECT`，禁用的标签被跳过；`dg_node_focus(tabs)` 聚焦选中的标签；不能直接 `dg_node_insert_before(tabs, …)`（`DG_ERR_UNSUPPORTED`）。
- `TREE`：240×240，虚拟化（固定行节点池）；点击、Up/Down/Left/Right/Home/End/PageUp/PageDown/Enter 选中/展开/折叠，滚轮和滚动条滚动。
- `TABLE`：360×240，虚拟化，带表头；点击行或 Up/Down/Home/End/PageUp/PageDown 选中（`TABLE_SELECT`），按可排序表头产生 `TABLE_SORT`——调用方排好数据、重新 `dg_table_set_cells()`、再 `dg_table_set_sort_indicator()`。
- `TREE`/`TABLE` 的视口在每次布局后跟随节点尺寸（像素或百分比 `DG_PROP_HEIGHT`、`DG_PROP_GROW`、父容器 stretch 都生效）；`PROGRESS` 的轨道填满节点。除 `TABS` 的面板外，组件节点不接受 `dg_node_insert_before()` 子节点。
- 纯 C 示例：`examples/28_c_components`（`--verify` 在 CTest 里跑）。

### 滑块与单选（0.2）

| 函数 | 用途 |
|---|---|
| `dg_slider_get_value(node, &value)` | 读 live `SLIDER` 的当前值；`value` 为 NULL 返回 `DG_ERR_INVALID_ARGUMENT`，其它类型 `DG_ERR_UNSUPPORTED` |
| `dg_radio_select(node)` | 选中 live `RADIO`、清掉同组其它项并重绘；不产生事件 |

- `SLIDER`：200×20，圆角轨道（`color.border`）+ 与轨道等高的圆点（`color.primary`），切主题重涂。`DG_PROP_VALUE`（`DG_VALUE_FLOAT`）写值，夹进 `[DG_PROP_MIN_VALUE, DG_PROP_MAX_VALUE]`（默认 0..100）并对齐到 `min + k*DG_PROP_STEP`（默认 0 = 连续）；`min ≥ max`、`max ≤ min`、负 `step`、非有限数返回 `DG_ERR_VALUE_OUT_OF_RANGE`，改范围后当前值重新夹取。主键按下：圆点跳到指针处、获得焦点、开始拖动直到松开；聚焦时 Left/Down、Right/Up 一个 step（连续时范围的 1/100），PageDown/PageUp 范围的 1/10，Home/End 到两端。每次用户改变值产生 `DG_EVENT_SLIDER_CHANGED`（写属性不产生）。`DG_PROP_DISABLED` 后不响应且用禁用色。`DG_PROP_HEIGHT` 让圆点同步变大。不接受子节点。
- `RADIO`：32×32 圆形、20×20 圆点，配色同 `CHECKBOX`；`DG_PROP_RADIO_GROUP`（`DG_VALUE_FLOAT` 整数，±2^24，默认 0）决定它在本窗口里的组。点击先选中（已选中时不取消）再排队 `DG_EVENT_CLICK`，之后 `dg_node_get_checked()` 查询——和 `CHECKBOX` 同一个形状。聚焦时 Left/Up、Right/Down 把焦点和选中移到同组上一个/下一个未禁用的 `RADIO`（创建顺序、循环），并在新节点上产生 `clicks = 0` 的 `DG_EVENT_CLICK`。每个单选是独立的 Tab 停靠点。
- `DG_PROP_MIN_VALUE`/`MAX_VALUE`/`STEP`/`RADIO_GROUP` 是控件状态：写到别的节点类型返回 `DG_ERR_UNSUPPORTED`，C++ 的 `dg::set_prop()` 同样返回 `kUnsupported`（运行时 `dg::Window::set_prop()` 会处理）。

### 定时器与跨线程唤醒（0.2）

| 函数 | 用途 |
|---|---|
| `dg_timer_add(app, delay_ms, repeat)` | 定时器 id（非 0，< 2^31），失败为 0；到期时产生 `DG_EVENT_TIMER`（`x` = id）。`repeat` 非 0 时每 `delay_ms` 重复，迟到也每次泵最多报一次。`dg_wait_events(app, -1)` 会为最近的定时器醒来 |
| `dg_timer_cancel(app, id)` | 待触发返回 `DG_ERR_OK`；已触发的一次性定时器、已取消或从未发放的 id 返回 `DG_ERR_INVALID_ARGUMENT`（重复取消无害） |
| `dg_wake(app)` | **线程安全**：让阻塞中的 `dg_wait_events()` 立即返回，不产生事件 |
| `dg_post_user_event(app, payload)` | **线程安全**：排入一条 `DG_EVENT_USER`（64 位负载拆在 `x`/`y`）并唤醒 |

线程规则：除 `dg_wake()`/`dg_post_user_event()` 外所有函数都只能在创建 app 的线程上调用；这两个可以从任意线程调用，但不能和 `dg_app_create()`/`dg_app_destroy()` 并发。

### 关闭确认与动画（0.2）

| 函数 | 用途 |
|---|---|
| `dg_window_set_close_confirmation(window, on)` | 非 0：用户点标题栏关闭时只产生 `DG_EVENT_CLOSE_REQUESTED`，窗口保持打开，由应用决定；0（默认）：照旧立即关闭并产生 `DG_EVENT_WINDOW_CLOSED` |
| `dg_window_close(window)` | 立即关闭窗口并排入 `DG_EVENT_WINDOW_CLOSED`（接受关闭请求就调它）；之后句柄失效（`DG_ERR_INVALID_HANDLE`） |
| `dg_node_animate(node, prop_id, from, to, duration_ms, curve)` | 把 live 节点的浮点/长度/颜色属性从 `from`（NULL = 当前值；只有背景色、边框色、不透明度、left/top/width/height 读得回，其它返回 `DG_ERR_UNSUPPORTED`）在 `duration_ms` 内按 `DG_CURVE_*` 插值到 `to`，由 `dg_wait_events()`/`dg_poll_events()` 推进（有动画时等待最多约 16 ms，没有时空闲不占 CPU）。返回动画 id（> 0，`DG_EVENT_ANIMATION_FINISHED` 的 `x` 回显它）；其它类型的属性直接跳到 `to` 并返回 0；负数为 `DG_ERR_*` |
| `dg_node_set_transition(node, prop_id, duration_ms, curve)` | 隐式过渡：之后对该属性的 `dg_node_set_prop()` 都从当前值（或飞行中的值）插值到新值；`duration_ms` 为 0 删除过渡。隐式过渡不产生结束事件 |
| `dg_animation_cancel(window, id)` | 停在当前值，不产生结束事件；已结束、已取消或节点已删除返回 `DG_ERR_UNKNOWN_ID` |
| `dg_app_set_reduced_motion(app, on)` | 减少动态效果：该 app 所有窗口（含以后创建的）的动画和过渡时长都视为 0，下一次泵即到终值 |

### 指针（0.2）

只有主键（左键）会按下/点击/聚焦控件。`DG_EVENT_CLICK` 的 `clicks` 是按下那次的连击数（平台给出，或由 drawgui 按同样阈值计数；两次点击之间按过键会重新从 1 数起）。右键按下产生 `DG_EVENT_CONTEXT_CLICK`（`node` 是指针下最近的有句柄节点，`x`/`y` 是位置），不按下也不聚焦任何东西；中键被忽略。

### 快捷键 / Action

| 函数 | 用途 |
|---|---|
| `dg_node_scope_action(node, action_id)` | 声明 `node` 消费某个 `action_id`；`node` 必须是 live 的（否则 `DG_ERR_NO_WINDOW`），重复声明是空操作而非错误 |
| `dg_shortcut_label(action_id)` | 返回该 action 的平台快捷键文字（如 `"Ctrl+A"`）；没有绑定时返回 NULL；返回值只在下次调用 `dg_shortcut_label` 前有效 |
| `dg_app_add_shortcut(app, chord, action_id)` | 应用自定义快捷键（路由第 4 级，所有窗口）：`chord` 如 `"Ctrl+Shift+N"`（修饰键 `Ctrl`/`Mod`/`Cmd`/`Shift`/`Alt` 任意顺序、不分大小写，键名为字母、数字、`F1`–`F12`、`Delete`、`Insert`、`Enter`、`Space`、方向键等），`action_id` ≥ `DG_USER_ACTION_BASE`；触发时报 `DG_EVENT_ACTION`（`node` 为 NULL）。同一组合键再绑定即替换。解析失败或 id 太小为 `DG_ERR_INVALID_ARGUMENT` |
| `dg_window_add_shortcut(scope, chord, action_id)` | 作用域快捷键（第 2 级）：焦点在 live 节点 `scope` 之内时触发，`DG_EVENT_ACTION` 的 `node` = `scope`；同一 `scope` 上同一组合键重复绑定是冲突（`DG_ERR_ALREADY_ATTACHED`）。每一级先查应用自定义的、再查内置表，所以自定义键可以遮蔽内置键（如 `PageDown`）；但编辑框自己的编辑键（方向键、Home/End、Backspace/Delete、撤销/重做、剪贴板组合键）总是优先，焦点在编辑框里时不带 Ctrl/Alt 的可打字键（字母、数字、空格、标点）也不当快捷键 |
| `dg_app_remove_shortcut(app, chord)` | 删除该组合键的全部自定义绑定（应用级和所有窗口的作用域级）；没有则 `DG_ERR_UNKNOWN_ID` |
| `dg_chord_label(app, chord)` | 组合键的显示文字（`"ctrl+shift+n"` → `"Ctrl+Shift+N"`），用于菜单的快捷键列；解析失败返回 NULL；返回值只在本线程下次调用前有效 |

### 事件泵

| 函数 | 用途 |
|---|---|
| `dg_wait_events(app, timeout_ms)` | 阻塞至多 `timeout_ms`（负数=永久），泵所有窗口，返回现在可轮询的事件数 |
| `dg_poll_events(app, out, max)` | 把至多 `max` 个事件出队到 `out[]`；从不阻塞，返回实际拷贝数 |

### 诊断

| 函数 | 用途 |
|---|---|
| `dg_last_error()` | 本线程最近一次失败调用的消息；有效期到本线程下一次 drawgui 调用为止 |
| `dg_dump_layout_tree(node)` | 把该节点子树导出为 JSON（type/bounds/parentData），见 `include/drawgui/abi/drawgui.h` 中该函数声明处的注释 |

### 主题

| 函数 | 用途 |
|---|---|
| `dg_theme_load_dir(app, dir, err)` | 加载一个外部、不可信的主题包目录（路径穿越/大小/资源数都有上限校验） |
| `dg_theme_load_memory(app, json, len, base_dir, err)` | 从内存中的 JSON blob 加载主题；`base_dir` 非空时仍按主题包规则解析资源路径 |
| `dg_theme_set_variant(theme, variant)` | 切换 `"light"`/`"dark"`；若这是当前激活主题，所有绑定节点立即重新解析 |
| `dg_theme_override(theme, token_id, value)` | 就地覆盖某个 token 的值（作用于当前激活的 variant） |
| `dg_app_set_theme(app, theme)` | 把 `theme` 设为该 app 的激活主题，立即对所有窗口重新应用绑定——这是热重载的第二步（第一步是重新 `load_dir`/`load_memory`） |

### 仅测试用（`dg_debug_*`，十二个，**非稳定面**）

这些函数只是为了让 `examples/19_c_client`、`26_text_area`、`28_c_components` 的 CTest 用例、`tests/unit/` 的 ABI 单元测试与 `tests/golden/`
的 ABI 场景能在无显示环境下通过真实的 SDL 事件队列驱动点击/按键/文字输入、
并截取帧像素；**不是**给真实宿主使用的
生产接口，`abi/drawgui.def.toml` 文件头注释本身也这样声明。

| 函数 | 用途 |
|---|---|
| `dg_debug_warp_pointer(window, x, y)` | 移动指针到指定坐标 |
| `dg_debug_post_pointer_button(window, down, x, y)` | 注入一次指针按下/抬起 |
| `dg_debug_post_key(window, down, logical_key_name)` | 注入一次按键；`logical_key_name` 是 `parse_chord()` 语法的组合键：裸键名（如 `"PageUp"`），0.2 起也可以带修饰键（如 `"Mod+Z"`、`"Mod+Shift+Z"`，注入的事件带对应的 Ctrl/Shift/Alt）；格式错误或未知键名返回 `DG_ERR_INVALID_ARGUMENT` |
| `dg_debug_trigger_exception(kind)` | 无条件抛异常（1=bad_alloc，2=runtime_error，其它=int），用于测试异常边界本身 |
| `dg_debug_capture_png(window)` | 把窗口当前帧编码为 PNG 放进线程局部缓冲并返回首字节指针（二进制，可能含 NUL）；golden 测试的取像入口 |
| `dg_debug_capture_png_len()` | 上一次 `dg_debug_capture_png()` 返回缓冲的字节数 |
| `dg_debug_post_text_input(window, utf8)` | 注入一次提交文字（`SDL_EVENT_TEXT_INPUT`）；须先点击聚焦 `TEXT_FIELD` |
| `dg_debug_post_text_field_key(window, down, key_name, shift)` | 注入文本框编辑键，`key_name` 只能是 `Left`/`Right`/`Home`/`End`/`Backspace`/`Delete`，0.2 起追加 `Up`/`Down`/`Enter`/`PageUp`/`PageDown`（对 `TEXT_AREA` 有意义）和 `Tab`（焦点遍历，`shift` 非 0 即 Shift+Tab），phase B 再追加 `Escape`（关闭菜单、取消对话框）；只能带 Shift，注入不了 Ctrl+Home 这类组合 |
| `dg_debug_post_wheel(window, dx, dy)` | 0.2：注入一次滚轮（格数，`dy` 为负是向下），位置是最后一次 `dg_debug_warp_pointer()` 的位置 |
| `dg_debug_post_pointer_button_ex(window, down, x, y, button, clicks)` | 0.2：带按键（`DG_POINTER_BUTTON_*`）和平台连击数（0 = 由 drawgui 计数）的 `dg_debug_post_pointer_button()` |
| `dg_debug_node_count(window)` | 0.2 phase B：窗口树当前的节点总数（含组件内部节点和打开的弹层），验证关闭/删除后回到基线 |
| `dg_debug_get_cursor(window)` | 0.2：窗口当前的光标（`DG_CURSOR_*`，不会是 `AUTO`），负数为 `DG_ERR_*`。无显示环境看不到真实光标，测试靠它观察 |

## 6. try/catch trampoline

`abi/drawgui.def.toml` 里每个 `[[function]]` 都有一个 `impl`，指向
`src/abi/abi_impl.h`/`.cpp` 里一个手写的、签名相同、可以自由抛异常的 C++
函数。`tools/gen_abi.py` 生成的 `src/abi/drawgui_abi.generated.cpp`
把每个 `impl` 包在**完全相同形状**的 try/catch 里：

```cpp
try {
  // 调用 impl
} catch (const std::bad_alloc&) {
  // 返回 DG_ERR_OOM（或该返回类型对应的失败值）
} catch (const std::exception& e) {
  // dg_last_error() 记下 e.what()，返回 DG_ERR_INTERNAL
} catch (...) {
  // 同上，未知异常
}
```

失败时返回什么值由 `ret` 类型单独决定（`void`→无返回；指针类型→
`nullptr`；`int32_t`→`DG_ERR_OOM`/`DG_ERR_INTERNAL`；`uint32_t`→`0`），
不是每个函数单独声明——这保证了没有一个导出函数能"忘记"包边界。

## 7. 版本兼容机制小结

- **函数**：按 `(name, 归一化签名)` 锁定，只能新增，不能删除或改签名。
- **结构体**：按 `(name, 有序字段列表)` 锁定，已锁定字段必须是当前字段列表
  的一个不变前缀——新字段只能追加在末尾。这就是为什么每个结构体第一个字段
  永远是 `size`。
- **常量**（`node_type`/`value_type`/`event_kind`/`error`/`theme_err`/`pixel_format`/`window_state`/`length_unit`/`pointer_button`/`menu_item_flag`/`tree_item_flag`/`table_column_flag`）：
  按 `(name, value)` 锁定，同 `props/prop_ids.lock`/`themes/token_ids.lock`
  的机制——只能新增，不能重新编号或复用。
- **不透明类型**：仅按名字锁定——只能新增，不能删除或改名（改名在 ABI 层是
  可观察的，因为它改变了 mangled struct tag）。

以上全部由 `tools/abi_lock.py` 针对 `abi/drawgui_abi.lock` 机制性校验，
而非依赖人工自觉。

### 版本历史

`DG_ABI_VERSION_MAJOR`/`DG_ABI_VERSION_MINOR` 来自 `abi/drawgui.def.toml`
的 `[meta]`，MINOR 每攒一批只追加的改动就加一次（同一份列表也写在 def.toml
的 `[meta]` 注释里）：

| 版本 | 内容 |
|---|---|
| 0.1 | 初始接口（94d53af），到 `dg_node_get_text()` 为止 |
| 0.2 | `dg_node_insert_before()` 按位置插入/移动、`dg_node_remove()` 删除整棵子树（5f3917b）；`DG_PROP_DISABLED` + `DG_VALUE_BOOL`（e8bdea9）；禁用控件跟随主题切换（c488360）；窗口操作 `dg_window_set_title()` … `dg_window_get_state()`、`DG_EVENT_WINDOW_*` 状态事件、`DG_PIXEL_FORMAT_*`、`DG_WINDOW_STATE_*`、`dg_window_opts` 追加的 `resizable`/最小/最大尺寸字段；`DG_PROP_CURSOR`（事件泵自动切换光标）、C 侧的枚举属性值常量（`DG_CURSOR_*` 及其它 `DG_<PROP>_<VALUE>`）、`dg_debug_get_cursor()`；`DG_PROP_SCROLLBAR` + `DG_SCROLLBAR_*`，以及滚动条主题 token `DG_TOKEN_COLOR_SCROLLBAR_*`/`DG_TOKEN_SIZE_SCROLLBAR`；`DG_NODE_TYPE_TEXT_AREA`（复用 `dg_node_set_text()`/`dg_node_get_text()`/`DG_EVENT_TEXT_CHANGED`），`dg_debug_post_text_field_key()` 追加 `Up`/`Down`/`Enter`/`PageUp`/`PageDown`；编辑框撤销/重做（`DG_ACTION_UNDO`/`REDO`/`REDO_ALT`，Mod+Z / Mod+Shift+Z / Mod+Y，事件泵直接作用于获得焦点的 `TEXT_FIELD`/`TEXT_AREA`）、获得焦点的编辑框自行处理复制/剪切/粘贴/全选（Mod+C/X/V/A，剪切/粘贴发出 `DG_EVENT_TEXT_CHANGED`，不再产生 `DG_EVENT_ACTION`）、Ctrl+Left/Right 按词移动、Ctrl+Backspace/Delete 删词、双击选词/三击选行（或全选），`dg_debug_post_key()` 可注入带修饰键的组合键；**phase A**：`BOX` 写 `DG_PROP_DIRECTION` 变成行/列容器、`DG_PROP_WRAP`；`DG_PROP_FONT_SIZE`/`DG_PROP_TEXT_ALIGN`；百分比长度（`DG_LENGTH_UNIT_*`）；焦点 API（`dg_node_focus()`/`dg_window_get_focus()`/`dg_window_clear_focus()`、Tab/Shift+Tab、`DG_PROP_TAB_INDEX`、`DG_EVENT_FOCUS_CHANGED`）；`DG_NODE_TYPE_SCROLL_VIEW`、`DG_NODE_TYPE_LIST`（`dg_list_*`、`DG_EVENT_LIST_SELECT`）、`dg_node_get_scroll_offset()`；`DG_PROP_MAX_LENGTH`，编辑框改用平台连击数；`dg_event::clicks`、`DG_EVENT_CONTEXT_CLICK`；`dg_timer_add()`/`dg_timer_cancel()`（`DG_EVENT_TIMER`）、`dg_wake()`/`dg_post_user_event()`（`DG_EVENT_USER`）；`DG_NODE_TYPE_IMAGE` + `dg_node_set_image_data()`/`_file()`；主题 token `color.scrim`（`DG_TOKEN_COLOR_SCRIM`，`PopupStyle::scrim` 取自它）；`dg_debug_post_wheel()`、`dg_debug_post_pointer_button_ex()`；**phase B**（组件层，轮询）：`dg_menu_open()`/`dg_menu_close()`（`DG_EVENT_MENU_ACTIVATE`/`_DISMISS`、`DG_MENU_ITEM_*`）、`DG_NODE_TYPE_DROPDOWN`（`dg_dropdown_*`、`DG_EVENT_VALUE_CHANGED`）、`dg_node_set_tooltip()`/`dg_app_set_tooltip_delay()`、`dg_dialog_open()`/`dg_dialog_close()`（`DG_EVENT_DIALOG_RESULT`）、`DG_NODE_TYPE_PROGRESS`（`DG_PROP_VALUE`、`DG_PROP_INDETERMINATE`）、`DG_NODE_TYPE_TABS`（`dg_tabs_*`、`DG_EVENT_TAB_SELECT`）、`DG_NODE_TYPE_TREE`（`dg_tree_*`、`DG_EVENT_TREE_*`、`DG_TREE_ITEM_*`）、`DG_NODE_TYPE_TABLE`（`dg_table_*`、`DG_EVENT_TABLE_*`、`DG_TABLE_COLUMN_*`）、`dg_debug_node_count()`，`dg_debug_post_text_field_key()` 追加 `Escape`；**表单控件**：`DG_NODE_TYPE_SLIDER`（`DG_PROP_VALUE` + `DG_PROP_MIN_VALUE`/`MAX_VALUE`/`STEP`、`dg_slider_get_value()`、`DG_EVENT_SLIDER_CHANGED`）、`DG_NODE_TYPE_RADIO`（`DG_PROP_RADIO_GROUP`、`dg_radio_select()`、`dg_node_get_checked()` 支持单选、方向键选中时 `clicks = 0` 的 `DG_EVENT_CLICK`）；**运行时补齐**（未发布，不升版本）：`DG_LENGTH_UNIT_AUTO`；`BUTTON`/`CHECKBOX`/`RADIO` 取主题色并随切换重涂（调用方写入/绑定的 `BACKGROUND_COLOR`/`TEXT_COLOR`/`BORDER_COLOR` 优先；复选框指示器圆角、默认内边距 6）；`DG_PROP_PLACEHOLDER`；`TEXT_FIELD` 文字内边距 10px（`PADDING_*` 修改）；`PADDING_*` 移动节点自己的文字；`DG_PROP_WRAP_TEXT`；`DG_PROP_IMAGE_TINT`；`LIST`/`TREE`/`TABLE` 跟随 `GROW`/stretch/百分比；`TABS` 标签栏背景铺满；**企业级特性（契约先行，运行时分阶段实现，未实现的函数返回 `DG_ERR_UNSUPPORTED`）**：`dg_event` 追加 `item`/`column`/`command`（`int32_t`，-1/-1/0 = 无；64 位上 `sizeof` 由 40 变 56，按 40 字节编译的调用方在 `dg_poll_events()` 前把 `out[0].size` 设为 40 即得到 40 字节的条目）、`DG_EVENT_ROW_PART`（`DG_ROW_PART_*`）、`DG_NODE_TYPE_MENUBAR`（`dg_menubar_set_menus()`/`dg_menubar_set_entry()`）、应用快捷键（`dg_app_add_shortcut()`/`dg_window_add_shortcut()`/`dg_app_remove_shortcut()`/`dg_chord_label()`、`DG_USER_ACTION_BASE`）、`dg_app_load_image()`/`_file()`、富行（`dg_list_set_rows()`/`dg_list_set_row_actions()`/`dg_list_row_checked()`、`DG_ROW_*`、`dg_tree_set_items_ex()`/`dg_tree_row_checked()`、`DG_TREE_ITEM_CHECKABLE`/`CHECKED`、`dg_table_set_cells_ex()`/`dg_table_cell_checked()`）、`dg_menu_open_ex()`、`dg_dialog_open_ex()`、对话框打开期间允许 `dg_menu_open()`/`_ex()`（菜单叠在对话框之上，原先返回 `DG_ERR_UNSUPPORTED`）/`dg_dialog_set_button_disabled()`、`DG_PROP_CHECKED`（切换按钮：写在 `BUTTON` 上即成为切换按钮，每次点击先翻转再产生 `DG_EVENT_CLICK`，`dg_node_get_checked()` 读取）；行键 `dg_list_set_row_keys()`/`dg_tree_set_item_keys()`/`dg_table_set_row_keys()` 与 `dg_event` 追加的 `item_key`（`uint64_t`，64 位上 `sizeof` 由 48 变 56）；节点用户数据 `dg_node_set_user_data()`/`dg_node_get_user_data()`；`LIST` 可聚焦（`dg_node_focus()` 接受它）并有键盘导航，`DG_EVENT_LIST_SELECT` 的 `clicks` 键盘选中时为 0；**运行时循环**：窗口关闭确认（`dg_window_set_close_confirmation()`、`DG_EVENT_CLOSE_REQUESTED`、`dg_window_close()`），由事件泵驱动的动画（`dg_node_animate()`、`dg_node_set_transition()`、`dg_animation_cancel()`、`DG_CURVE_*`、`DG_EVENT_ANIMATION_FINISHED`、`dg_app_set_reduced_motion()`） |

## 8. 相关文档

- 属性写入的完整语义（`PropStatus`/`PropValue`）：`doc/reference/properties.md`
- 主题包加载的完整设计：`include/drawgui/theme/theme_package.h`、`include/drawgui/theme/theme_loader.h` 的注释
- 快捷键/action 路由的四级模型：`design.md` 第 5.5.2/5.5.3 节
