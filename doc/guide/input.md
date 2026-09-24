# 输入（Input）：指针、键盘、焦点、快捷键、剪贴板

## 这个子系统解决什么问题

一个窗口每帧要回答几个问题：指针点在哪个 widget 上？Tab 该把焦点移到哪
一个？这个按键是给正在编辑的文本框，还是给某个全局快捷键，还是给 IME？
剪贴板复制/粘贴该怎么接进来？drawgui 把这些拆成几个独立、可分别测试的
机制：`WindowManager::pump()` 产出原始事件，`Focus` 管理焦点顺序和作用域，
`route_key_event()` 做四级快捷键路由，`apply_clipboard_action()`/
`apply_keyboard_scroll()` 是路由结果的具体执行者。

## 核心类型

- `dg::PumpResult`（`include/drawgui/window/window_manager.h:358`）：一次
  `pump()` 调用捞到的所有事件，按"调用方要怎么处理"分类。
- `dg::Focus`（`include/drawgui/widget/focus.h:167`）：单例每窗口一份的
  焦点状态机，`current()`/`set()`/`focus_next()`/`focus_previous()`/
  `enter_scope()`/`exit_scope()`。
- `dg::FocusRing`（`focus.h:363`）：焦点可视化，四条描边的位置数据。
- `dg::Chord`/`dg::ShortcutBinding`（`include/drawgui/shortcuts/chord.h`）：
  一个解析好的按键组合、以及它绑定到的 action。
- `dg::route_key_event()`（`include/drawgui/shortcuts/router.h:160`）：
  四级路由的唯一入口。
- `dg::ActionScopes`（`include/drawgui/shortcuts/action_scopes.h:40`）：
  记录"哪个 node 消费哪个 action_id"的旁表（level 2 路由用）。

## 事件从哪里来：`pump()`

```cpp
const dg::PumpResult pumped = manager.pump(40);  // 超时毫秒数
for (const dg::WindowId id : pumped.needs_repaint) { /* 先处理重绘/resize */ }
for (const dg::PointerEvent& event : pumped.pointer) { /* 再处理指针 */ }
for (const dg::KeyEvent& event : pumped.key) { /* 键盘 */ }
for (const dg::TextInputEvent& event : pumped.text_input) { /* 已提交文本 */ }
for (const dg::TextEditingEvent& event : pumped.text_editing) { /* IME 预览态 */ }
```

**`needs_repaint` 必须先于 `pointer` 处理**（`window_manager.h:379,385-387`）：
一个 resize 改变了每个 widget 的位置，如果同一次 `pump()` 里恰好也带来了
指针事件，而调用方还没来得及按新尺寸重新布局，这个指针事件就会命中错误的
坐标。这条顺序是硬性约束，详细理由见 `doc/contracts.md`。

`PumpResult::closed`/`close_requested` 处理窗口关闭；`text_editing` 是 IME
组字预览（大多数真实 IME 在这台开发机上从未产生过这个事件——见 `window_manager.h:397-403`
的记录，这条路径主要靠合成注入测试）。

## 焦点：复用 `RenderTree` 自身的树，没有第三棵树

`Focus` 不维护自己的树结构——`focus_order()` 直接对 `RenderTree` 做一次
前序遍历（`focus.h:11-23`）。这是从"焦点树"这个设想（design.md 原文用词）
出发追问"它到底要回答什么问题"后的结论：Tab 顺序需要一个遍历序，弹层需要
一个边界，而 `RenderTree` 已经暴露了 `children()`/`parent()`，`Focus` 只
额外需要一个"哪个 node 是作用域边界"的 `std::optional<NodeId>`。

```cpp
dg::Focus focus;
const dg::FocusChange change = focus.set(node_id);   // 直接聚焦
focus.focus_next(tree.render(), widgets, root_node);  // Tab
focus.focus_previous(tree.render(), widgets, root_node);  // Shift-Tab
```

### `focus_order()` 的规则（`focus.h:134-165`）

- 默认按树的前序遍历（DOM 顺序）；`Widget::tab_index` 可以覆盖，正数排前面
  按升序，其余（含 0/未设置）按树序排在后面，负数**从 Tab 序里剔除**（但仍可
  被直接点击聚焦），完全对应 HTML 的 `tabindex` 语义。
- 跳过：没有挂 `Widget` 的节点、`is_focusable()` 判否的 kind、零面积节点（这也
  是弹层关闭后不再进 Tab 序的机制来源——关闭是裁成空矩形，见 `doc/guide/popups.md`）。
- **不跳过 `opacity == 0`** 的节点：hit_test 本来就忽略 opacity，Tab 能到达
  的集合和点击能到达的集合保持一致。

### `is_focusable()`（`focus.h:109-124`）

```
kButton / kCheckbox / kSlider / kTextField / kDropdown  -> true
kPanel / kLabel / kScrollView / kList                    -> false
```

**`kScrollView`/`kList` 不能获得焦点**——不是遗漏，是刻意的：让一个滚动区域
成为 Tab 停靠点意味着需要键盘直接操作它（箭头/PageUp/PageDown 滚动它），
但这套"键盘滚动"机制此前完全不存在，先让它可 Tab 到达会造成"Tab 能到但没有
键能操作"的更差体验。（现在 `apply_keyboard_scroll()` 已经实现了 PageUp/
PageDown/Home/End 滚动，但走的是**从当前焦点节点向上找最近可滚动祖先**这条
路径，不是让 ScrollView/List 自身可聚焦——见下文。）

### 焦点作用域（弹层边界与模态陷阱）

```cpp
focus.enter_scope(popup_root);          // Tab 被限制在 popup_root 子树内
// ... 打开期间 ...
focus.exit_scope(tree.render());        // 离开作用域，若当前焦点在作用域内会先 blur
```

`enter_scope(root, modal)` 的 `modal` 参数默认 `false`；传 `true` 时额外让
`set_guarded()` 拒绝把焦点设到作用域外——这是 `Dialog` 的模态焦点陷阱，和
普通弹层用的是**同一个作用域机制**，只是多了一个拒绝开关，不是第二套概念。

**只有一个活跃作用域，不是栈**：因为 `PopupHost` 从不允许弹层里再开弹层
（见 `doc/guide/popups.md`），重复调用 `enter_scope()` 直接替换旧作用域，
没有嵌套恢复。

### 焦点环：内缩，且每个弹层需要自己的实例

```cpp
dg::update_focus_ring(tree.render(), parent, ring, focus.current(), ring_color,
                      /*thickness=*/2, /*gap=*/2);
```

环的几何是**从目标 bounds 内侧向内画**——四条描边永远不超出目标自身的
bounds（`focus.h:296-362` 的长注释记录了这是 commit `d7283c2` 从"外扩"改
过来的：外扩假设目标周围有空白边距可占用，但一个满宽贴边的列表行（如
下拉菜单的行）左右贴边、上下和相邻行贴边，外扩会画到相邻行内部甚至画到
surface 外面。内缩不依赖这个假设。

**每个打开的 popup 需要自己的 `FocusRing` 实例**——`update_focus_ring()`
只在 `ring.created == false` 时调用一次 `add_child()`，之后这四个节点就
永久挂在当时的 parent 下，而 `RenderTree` 没有 reparent 原语（commit
`5e630ef`）。宿主窗口的环节点在弹层打开之前就已经挂在 `LayoutTree::root()`
下了，如果复用它，画出来的环在绘制顺序上会被弹层内容盖住，而不是显示在
目标上方。所以每次开弹层都要 `FocusRing{}` 重新构造一个，挂在弹层自己的
`content_root` 下（`examples/22_dropdown_menu/dropdown_window.cpp:158-168`）。

**降级行为**：目标任一轴的尺寸小于 `2*(gap+thickness)`（默认 gap=2,
thickness=2，即 8px）时，`fill_ring()` 直接把整个目标涂成环颜色，而不是
画出一个反转/重叠的怪异描边（`src/widget/focus.cpp` 约 :60-66,:263-267；
签名见 `focus.h:376-390`）。

## 快捷键：8 个 action，四级路由

`input/shortcuts.toml` 是唯一真相源，生成 action id、`LogicalKey` 枚举、
`ActionScope` 枚举——**运行期不可改键**，改绑定要改这个 toml 重新生成。

当前定义的 8 个 action（`input/shortcuts.toml:97-168`）：

| action | scope | 绑定 |
|---|---|---|
| `copy` | text_field | Mod+C |
| `cut` | text_field | Mod+X |
| `paste` | text_field | Mod+V |
| `select_all` | text_field | Mod+A |
| `scroll_page_up` | app | PageUp |
| `scroll_page_down` | app | PageDown |
| `scroll_to_start` | app | Home |
| `scroll_to_end` | app | End |

### 四级路由（design.md section 5.5.2，`router.h`）

```
1. IME 正在组字？          -> 每个按键都归 IME，永不进入路由
2. 焦点节点向上冒泡         -> 组件级（比如 TextField 自己的 select_all）
3. 窗口级表                -> 声明性存在，当前无任何 action 使用
4. 应用级表                -> 全局
```

**第 3 级只是声明性存在，不要把"窗口级快捷键"描述成一个可用特性**：
`ActionScope`（`action_scope.generated.h`）目前只生成了 `kApp` 和
`kTextField` 两个值，因为 `input/shortcuts.toml` 里没有任何 action 需要
窗口级作用域（`router.h:14-27`）。加它需要先有一个真实消费者去
`shortcuts.toml` 里注册第三个 scope 值，生成器才会长出对应枚举——目前
没有。

**第 2 级是"最近节点冒泡优先"，没有特例**：`resolve_action()` 沿
`RenderTree::parent()` 从 `focused` 一路往上走，第一个 `ActionScopes::consumes()`
命中的节点获胜。这就是为什么聚焦的 TextField 自己的 `select_all` 能
战胜应用级绑定——路由根本没走到应用级表，不需要为此写特例。

**IME 隔离（第 1 级）和"文本编辑键不算快捷键"是两回事**，虽然效果都是
"这个键不进 `resolve_action()`"（`router.h:40-75`）：

- 文本编辑键：当聚焦的 widget kind 会把 `KeyEvent::key` 当编辑意图直接
  消费（如 kTextField 上的 Left/Right/Home/End/Backspace/Delete），这个
  事件根本不会被打包成 `Chord`，也不会传给 `resolve_action()`——不是
  "level 2 赢了 level 4"，是快捷键表压根没被查。这也是为什么应用级的
  `scroll_to_start`（绑定裸 `Home`）和文本框自己的行首（也是 `Home`，
  没有任何 shortcut 绑定）能共存而不冲突。
- IME 组字：`WidgetSet::text_field_is_composing(focused)` 为真时，**每一个**
  按键（包括组字期间也可能出现的方向键/Enter/Escape）都归输入法候选窗口，
  是硬隔离，不区分是否恰好也是编辑意图。

`route_key_event()` 把这两条早退写在函数体最前面，`resolve_action()` 本身
甚至不会被提到——这是 8-3b 的缺陷注入测试能直接验证的结构性保证：删掉
早退语句，`tests/unit/test_shortcut_routing.cpp` 就会失败。

### `Mod` 伪修饰键

```cpp
enum class Platform : std::uint8_t { kLinux, kWindows, kMacos };
std::string_view mod_label(Platform platform);  // Linux/Windows -> Ctrl, macOS -> Cmd
```

`Platform::kWindows`/`kMacos` **只是为了让这条替换规则本身可测试**——这个
项目目前只有 Linux 后端（README 的"no platform abstraction ahead of a
second backend"原则），这两个枚举值不代表存在对应的窗口系统实现。

### 剪贴板

```cpp
manager.set_clipboard_text("hello");
std::string text = manager.get_clipboard_text();

dg::TextFieldEditContext ctx{tree, fonts, widgets, manager};
bool did = dg::apply_clipboard_action(DG_ACTION_COPY, field_node, ctx);
```

`apply_clipboard_action()`（`include/drawgui/shortcuts/clipboard_actions.h:85`）
处理 copy/cut/paste/select_all 四个 action，全部注册在 `ActionScope::kTextField`
下。**复制/剪切在没有选区时是 no-op**，剪贴板不动，返回 `false`——这是
刻意决定，不是"选中全部再复制"的隐式回退。粘贴复用既有的 `text_field_insert()`，
它已经会先删除选区再插入，也已经会丢弃 ASCII 控制字符（含换行）——粘贴
带换行的文本到单行文本框会静默丢失换行，这是既有行为，不是本节引入的。

### 键盘滚动

```cpp
bool did = dg::apply_keyboard_scroll(DG_ACTION_SCROLL_PAGE_DOWN, focused, layout, widgets);
```

目标不是 `ResolvedAction::target`（这四个 action 都注册在 `kApp` 作用域，
target 永远是 `std::nullopt`），而是**从当前聚焦节点向上找最近的可滚动
祖先**（`keyboard_scroll.h:8-29`）——因为 `is_focusable()` 本来就拒绝
`kScrollView`，"滚动被聚焦的东西"这个说法对键盘滚动不成立，唯一说得通的
语义是"从任何持有焦点的非滚动 widget 出发，向上找滚动容器"，和滚轮滚动
用的是同一条祖先查找逻辑（`scrollable_owner_of()`），只是起点不同。**只走
垂直轴**，没有水平版本；焦点为空、没有可滚动祖先、或已经到达滚动上限
都是合法的 no-op，不发明"滚动树里第一个/唯一一个 ScrollView"这类兜底
目标。

## 边界与坑

- `needs_repaint` 必须先于 `pointer` 处理，否则可能对旧布局做命中测试。
  详见 `doc/contracts.md`。
- 窗口级快捷键（level 3）目前只是声明性存在，没有任何 action 使用它，
  不要指望它能工作。
- 快捷键绑定运行期不可改，只能重新生成。
- `kScrollView`/`kList` 永远不可聚焦。
- 每个弹层需要独立的 `FocusRing` 实例，复用宿主窗口的环会导致绘制顺序
  错误。
- `Platform::kMacos` 不代表有 macOS 后端。
- 更多边界见 `doc/limitations.md`。

## 相关 example

- `examples/21_focus` — Tab 序、焦点环、弹层作用域。
- `examples/22_dropdown_menu` — 弹层内的 Tab 限定与焦点环重建。
- 快捷键路由与剪贴板动作目前没有独立的可视化 example，主要靠
  `tests/unit/test_shortcut_routing.cpp`、`tests/unit/test_shortcuts.cpp`
  验证。
