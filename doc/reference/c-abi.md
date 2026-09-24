# C ABI 参考

权威来源：`abi/drawgui.def.toml`（唯一真源）。`include/drawgui/abi/drawgui.h`
（纯 C 头，无 C++/Skia 类型）、`src/abi/drawgui_abi.generated.cpp`（try/catch
边界）、`abi/drawgui.d.ts`（TypeScript 环境声明）三者都由
`tools/gen_abi.py` 从它生成，三者中任何一个手改都会被
`tools/abi_lock.py --check` 拦下。

## 1. 能力边界（先看这个）

在细看每个函数之前，必须先知道这个 ABI **不能**做什么——这些不是遗漏，是
`abi/drawgui.def.toml` 文件头注释里点名拒绝的：

- **只能创建两种节点**：`DG_NODE_TYPE_BOX`（=1，对应 `WidgetKind::kPanel`）、
  `DG_NODE_TYPE_BUTTON`（=2，对应 `WidgetKind::kButton`）。C++ 侧
  `WidgetKind` 实际有 9 种（`kPanel`/`kLabel`/`kButton`/`kCheckbox`/
  `kScrollView`/`kSlider`/`kTextField`/`kList`/`kDropdown`），ABI 只暴露了
  前两种——没有 checkbox、slider、文本框、list、dropdown。
- **画不了文字**：49 个属性里没有 text 类属性，头文件里也没有
  `dg_node_set_text` 这样的函数。`kLabel` 节点类型本身都没有导出。
- **`dg_node_insert_before` 的 `ref` 参数必须为 NULL**：这个 ABI 只支持
  在父节点末尾追加子节点（append-only）；传非 NULL 的 `ref` 返回
  `DG_ERR_UNSUPPORTED`。
- **四个复杂属性 setter（`set_gradient`/`set_shadow`/`set_image`/
  `set_transform`）完全没有导出到 ABI**——`abi/drawgui.def.toml` 里没有
  `dg_node_set_gradient`/`_shadow`/`_image`/`_transform` 这样的
  `[[function]]` 条目。C ABI 调用方目前只能写标量属性
  （`dg_node_set_prop` + `dg_value`），拿不到渐变/阴影/图片/变换。
- **没有焦点 API、没有弹层（popup/menu/tooltip/dialog）API、没有动画 API、
  没有自定义绘制、没有回调模式**——这几项在 `abi/drawgui.def.toml`
  文件头注释里被具名拒绝（`dg_set_event_callback`、
  `dg_custom_paint_set_cmds`、`dg_animate`/`dg_node_set_transition`），
  理由是各自"没有真实调用方可供提炼接口"，而不是技术上做不到。
- **`abi/drawgui.d.ts` 生成了，但本仓库里没有任何消费者**——没有 JS/TS
  框架层读它。这是"一个已敲定的目标形状，而非已被证明的形状"（"a settled
  TARGET shape, not a proven one"，见 `include/drawgui/abi/drawgui.h:548`
  附近的生成器注释）。

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

（原始任务描述给的数字是"6 个"；核对 `abi/drawgui.def.toml` 后实际是
5 个 `[[struct]]` 条目，以源码为准。）

调用方必须先 `memset` 清零结构体、再设置 `size = sizeof(struct)`，这是
版本兼容机制：未来某个 MINOR 版本在结构体末尾追加字段时，能通过 `size`
辨别调用方的旧结构体是否比当前定义短，而不会读越界。

| 结构体 | 用途 |
|---|---|
| `dg_value` | 一个带类型标签的属性值——`type`（`DG_VALUE_*`）、`number`（浮点/长度）、`bits`（颜色 ARGB 或枚举序号） |
| `dg_app_opts` | 应用级选项，目前为空，只留 `size` 供未来扩展 |
| `dg_window_opts` | 窗口初始状态：`title`（创建时拷贝，不持有）、`width`、`height`、`background_argb` |
| `dg_event` | `dg_poll_events()` 的一条事件：`kind`、`window`、`node`、`x`/`y`、`action_id` |
| `dg_theme_err` | `dg_theme_load_dir`/`_memory` 失败时的机读原因（`DG_THEME_ERR_*`） |

## 4. 常量组

| 组 | 前缀 | 数量 | 说明 |
|---|---|---|---|
| `node_type` | `DG_NODE_TYPE_` | 2 | 只有 `BOX`(1)、`BUTTON`(2)——见第 1 节 |
| `value_type` | `DG_VALUE_` | 5 | `FLOAT`/`LENGTH`/`COLOR`/`ENUM`/`TOKEN`（`TOKEN` 是 7-6 加入的主题绑定值类型） |
| `event_kind` | `DG_EVENT_` | 3 | `WINDOW_CLOSED`/`CLICK`/`ACTION` |
| `error` | `DG_ERR_` | 13 | 见下 |
| `theme_err` | `DG_THEME_ERR_` | 9 | 主题包加载失败原因，一一对应 `dg::ThemeLoadStatus` |

`error` 常量组（13 个值，对应 `dg::PropStatus`/ABI 自身的失败原因）：
`OK`(0)、`OOM`(-1)、`INTERNAL`(-2)、`INVALID_ARGUMENT`(-3)、
`INVALID_HANDLE`(-4)、`UNKNOWN_ID`(-5)、`TYPE_MISMATCH`(-6)、
`VALUE_OUT_OF_RANGE`(-7)、`NOT_APPLICABLE`(-8)、`UNSUPPORTED`(-9)、
`ALREADY_ATTACHED`(-10)、`NO_WINDOW`(-11)、`NO_ACTIVE_THEME`(-12)。

## 5. 导出函数（共 24 个）

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

### 节点

| 函数 | 用途 |
|---|---|
| `dg_node_create(app, type_id)` | 分配一个 pending 节点；`type_id` 只能是 `DG_NODE_TYPE_BOX`/`BUTTON`/`LABEL` |
| `dg_node_set_prop(node, prop_id, value)` | 标量属性写入，`dg::set_prop()` 的 ABI 映射；对 pending 或 live 节点都有效 |
| `dg_node_set_text(node, prop_id, utf8, len)` | 专用 setter 通道（`prop_id` 必须是 `DG_PROP_TEXT`）：UTF-8 字符串装不进 `dg_value` 的定长联合，形状与 `dg::set_gradient/_shadow/_image` 一致；只对 live 节点有效 |
| `dg_node_insert_before(parent, child, ref)` | 把 pending 的 `child` 挂到 live 的 `parent` 下；**`ref` 必须为 NULL**，否则 `DG_ERR_UNSUPPORTED` |
| `dg_node_remove(node)` | 使句柄失效（此后任何调用返回 `DG_ERR_INVALID_HANDLE`）；对一个 LIVE 节点，**也**会把它的整棵子树从父节点的子节点列表中摘除（`RenderTree::remove_child()`——"Detaches `id`'s whole subtree from its parent's children"），不只是让这一个句柄失效 |

### 快捷键 / Action

| 函数 | 用途 |
|---|---|
| `dg_node_scope_action(node, action_id)` | 声明 `node` 消费某个 `action_id`；`node` 必须是 live 的（否则 `DG_ERR_NO_WINDOW`），重复声明是空操作而非错误 |
| `dg_shortcut_label(action_id)` | 返回该 action 的平台快捷键文字（如 `"Ctrl+A"`）；没有绑定时返回 NULL；返回值只在下次调用 `dg_shortcut_label` 前有效 |

### 事件泵

| 函数 | 用途 |
|---|---|
| `dg_wait_events(app, timeout_ms)` | 阻塞至多 `timeout_ms`（负数=永久），泵所有窗口，返回现在可轮询的事件数 |
| `dg_poll_events(app, out, max)` | 把至多 `max` 个事件出队到 `out[]`；从不阻塞，返回实际拷贝数 |

### 诊断

| 函数 | 用途 |
|---|---|
| `dg_last_error()` | 本线程最近一次失败调用的消息；有效期到本线程下一次 drawgui 调用为止 |
| `dg_dump_layout_tree(node)` | 把该节点子树导出为 JSON（type/bounds/parentData），细节见 `include/drawgui/abi/drawgui.h:514` 附近的注释 |

### 主题

| 函数 | 用途 |
|---|---|
| `dg_theme_load_dir(app, dir, err)` | 加载一个外部、不可信的主题包目录（路径穿越/大小/资源数都有上限校验） |
| `dg_theme_load_memory(app, json, len, base_dir, err)` | 从内存中的 JSON blob 加载主题；`base_dir` 非空时仍按主题包规则解析资源路径 |
| `dg_theme_set_variant(theme, variant)` | 切换 `"light"`/`"dark"`；若这是当前激活主题，所有绑定节点立即重新解析 |
| `dg_theme_override(theme, token_id, value)` | 就地覆盖某个 token 的值（作用于当前激活的 variant） |
| `dg_app_set_theme(app, theme)` | 把 `theme` 设为该 app 的激活主题，立即对所有窗口重新应用绑定——这是热重载的第二步（第一步是重新 `load_dir`/`load_memory`） |

### 仅测试用（`dg_debug_*`，四个，**非稳定面**）

这四个函数只是为了让 `examples/19_c_client` 的 CTest 用例能在无显示环境下
通过真实的 SDL 事件队列驱动一次真实点击/按键；**不是**给真实宿主使用的
生产接口，`abi/drawgui.def.toml` 文件头注释本身也这样声明。

| 函数 | 用途 |
|---|---|
| `dg_debug_warp_pointer(window, x, y)` | 移动指针到指定坐标 |
| `dg_debug_post_pointer_button(window, down, x, y)` | 注入一次指针按下/抬起 |
| `dg_debug_post_key(window, down, logical_key_name)` | 注入一次按键；`logical_key_name` 必须是不带修饰键的裸键名（如 `"PageUp"`），带修饰键的名字（如 `"Mod+C"`）返回 `DG_ERR_INVALID_ARGUMENT` |
| `dg_debug_trigger_exception(kind)` | 无条件抛异常（1=bad_alloc，2=runtime_error，其它=int），用于测试异常边界本身 |

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
- **常量**（`node_type`/`value_type`/`event_kind`/`error`/`theme_err`）：
  按 `(name, value)` 锁定，同 `props/prop_ids.lock`/`themes/token_ids.lock`
  的机制——只能新增，不能重新编号或复用。
- **不透明类型**：仅按名字锁定——只能新增，不能删除或改名（改名在 ABI 层是
  可观察的，因为它改变了 mangled struct tag）。

以上全部由 `tools/abi_lock.py` 针对 `abi/drawgui_abi.lock` 机制性校验，
而非依赖人工自觉。

## 8. 相关文档

- 属性写入的完整语义（`PropStatus`/`PropValue`）：`doc/reference/properties.md`
- 主题包加载的完整设计：`include/drawgui/theme/theme_package.h`、`include/drawgui/theme/theme_loader.h` 的注释
- 快捷键/action 路由的四级模型：`design.md` 第 5.5.2/5.5.3 节
