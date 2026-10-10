# 输入（Input）：指针、键盘、焦点、快捷键、剪贴板

## 用 `dg::App`

指针、键盘、焦点都由运行时分发，你只处理 `App::run()` / `pump()` 交来的事件：

```cpp
app.run([&](const dg::Event& event) {
  std::visit(dg::Overloaded{
      [&](const dg::events::ClickEvent& e) { /* e.node, e.clicks（双击=2） */ },
      [&](const dg::events::ContextClickEvent& e) { /* 右键：e.x, e.y */ },
      [&](const dg::events::FocusChangedEvent& e) { /* e.node */ },
      [&](const dg::events::ActionEvent& e) { /* scope_action() 绑定的 action */ },
      [&](const dg::events::TimerEvent& e) { /* app.add_timer() */ },
      [&](const dg::events::UserEvent& e) { /* app.waker().post_user_event() */ },
      [](const auto&) {},
  }, event);
});
```

- 焦点：Tab/Shift+Tab、点击可聚焦控件、`Window::focus()/clear_focus()/focused()`；
  焦点环由运行时画。菜单和对话框打开时自动限制焦点范围。
- 快捷键：`Window::scope_action(node, action_id)` 让某个节点处理一个 action，
  触发时得到 `ActionEvent`。编辑框的复制/剪切/粘贴/全选/撤销由运行时直接处理。
- 应用自定义快捷键：`App::add_shortcut("Ctrl+Shift+N", id)`（第 4 级，所有窗口，
  `ActionEvent::node` 为空）、`Window::add_shortcut(scope, "F2", id)`（第 2 级，焦点在
  `scope` 子树内时触发，`node` = `scope`）、`App::remove_shortcut(chord)`、
  `App::chord_label(chord)`（菜单快捷键列的文字），见下文「应用自定义快捷键」。
- 键盘右键菜单：Menu 键或 Shift+F10 对焦点节点产生 `ContextClickEvent`，见下文。
- 鼠标指针形状：写 `DG_PROP_CURSOR`，运行时在指针移动时自己切换。
- 定时器与跨线程唤醒：`App::add_timer()` / `cancel_timer()`，`App::waker()`
  返回的 `Waker` 可以在任意线程调用。
- 测试里注入输入：`App::window_manager()` 的 `warp_pointer()`、
  `post_pointer_button()`、`post_key()`、`post_logical_key()`、`post_text_input()`、
  `post_text_editing()`（见 `examples/common/example_app.h`）。

下面的「直接使用底层」讲事件源、焦点和快捷键路由本身。

## 这个子系统解决什么问题

一个窗口每帧要回答几个问题：指针点在哪个 widget 上？Tab 该把焦点移到哪
一个？这个按键是给正在编辑的文本框，还是给某个全局快捷键，还是给 IME？
剪贴板复制/粘贴该怎么接进来？drawgui 把这些拆成几个独立、可分别测试的
机制：`WindowManager::pump()` 产出原始事件，`Focus` 管理焦点顺序和作用域，
`route_key_event()` 做四级快捷键路由，`apply_clipboard_action()`/
`apply_keyboard_scroll()` 是路由结果的具体执行者。

## 直接使用底层

直接持有 `WindowManager` / `Focus` / `ActionScopes` 时，下面这些事要自己做：

- 每帧按 `doc/getting-started.md` 第 5 节的顺序处理 `PumpResult`。
- 指针按下时决定焦点、更新焦点环（`update_focus_ring()`），弹层开关时进出焦点作用域。
- 编辑框的剪贴板快捷键交给 `apply_clipboard_key()`，其它 action 走 `route_key_event()`。
- 指针移动时用 `cursor_at()` 算出形状再 `set_cursor()`。

改完要遵守 [`app.md` 的逃生舱约定](app.md#逃生舱约定)：只改运行时不组合的东西，改完调用 `Window::invalidate(node)`。

### 核心类型

- `dg::PumpResult`（`include/drawgui/window/window_manager.h:424`）：一次
  `pump()` 调用捞到的所有事件，按"调用方要怎么处理"分类。
- `dg::Focus`（`include/drawgui/widget/focus.h:174`）：单例每窗口一份的
  焦点状态机，`current()`/`set()`/`focus_next()`/`focus_previous()`/
  `enter_scope()`/`exit_scope()`。
- `dg::FocusRing`（`focus.h:380`）：焦点可视化，四条描边的位置数据。
- `dg::Chord`/`dg::ShortcutBinding`（`include/drawgui/shortcuts/chord.h`）：
  一个解析好的按键组合、以及它绑定到的 action。
- `dg::route_key_event()`（`include/drawgui/shortcuts/router.h:160`）：
  四级路由的唯一入口。
- `dg::ActionScopes`（`include/drawgui/shortcuts/action_scopes.h:40`）：
  记录"哪个 node 消费哪个 action_id"的旁表（level 2 路由用）。

### 事件从哪里来：`pump()`

```cpp
const dg::PumpResult pumped = manager.pump(40);  // 超时毫秒数
for (const dg::WindowId id : pumped.needs_repaint) { /* 先处理重绘/resize */ }
for (const dg::PointerEvent& event : pumped.pointer) { /* 再处理指针 */ }
for (const dg::KeyEvent& event : pumped.key) { /* 键盘 */ }
for (const dg::TextInputEvent& event : pumped.text_input) { /* 已提交文本 */ }
for (const dg::TextEditingEvent& event : pumped.text_editing) { /* IME 预览态 */ }
for (const dg::WindowEvent& event : pumped.window_events) { /* 移动/最小化/焦点/全屏… */ }
```

**`needs_repaint` 必须先于 `pointer` 处理**（`window_manager.h:445,451-453`）：
一个 resize 改变了每个 widget 的位置，如果同一次 `pump()` 里恰好也带来了
指针事件，而调用方还没来得及按新尺寸重新布局，这个指针事件就会命中错误的
坐标。这条顺序是硬性约束，详细理由见 `doc/contracts.md`。

`PumpResult::window_events` 报告窗口自身的状态变化：`kResized`（新的物理像素尺寸，
同一个窗口也一定在 `needs_repaint` 里，按 `needs_repaint` 处理就够了）、`kMoved`、
`kMinimized`/`kMaximized`/`kRestored`、`kFocusGained`/`kFocusLost`、
`kEnteredFullscreen`/`kLeftFullscreen`。调用方自己发起的 `set_size()`/`minimize()`/
`set_fullscreen()` 等也会经由这里确认——在真实显示服务器上这些请求是异步生效的。

`PumpResult::closed`/`close_requested` 处理窗口关闭；`text_editing` 是 IME
组字预览（大多数真实 IME 在这台开发机上从未产生过这个事件——见 `window_manager.h:463-469`
的记录，这条路径主要靠合成注入测试）。

#### 多击计数（双击/三击）

`PointerEvent::clicks` 在 `kDown`/`kUp` 上给出本次按下在连击序列中的序号（1 单击、2 双击、
3 三击），`kUp` 沿用它所结束的那次 `kDown` 的值；其他动作为 0。

- 平台报告了计数（SDL 的 `SDL_MouseButtonEvent::clicks`，用的是系统双击时间/距离，会一直
  数到 4、5…）时直接采用。
- 没有平台计数的按下（`post_pointer_button()` 默认 `clicks = 0`）由 `WindowManager` 自己
  按 `dg::ClickCounter` 的阈值推导：间隔 ≤ `kMultiClickMs`（500ms）且两轴位移 ≤
  `kMultiClickSlopPx`（4 物理像素）算连续，按 1、2、3、1 循环；时间取自 `set_clock()` 设置的时钟。
  每个按键（主/次/中）各自计数。
- 判断双击请写 `clicks == 2`，两种来源结果一致。

编辑框也读这个字段：`text_field_click()`/`text_area_click()` 的 `click_count` 为 2 时选中
指针下的词，为 3 时在 TextField 里全选、在 TextArea 里选中所在逻辑行（两个 `\n` 之间）。
`dg::App` 和 C ABI 都自动做这件事。

#### 指针捕获（pointer capture）

拖动时希望按下的控件在指针离开窗口后仍收到移动和抬起：

```cpp
case PointerAction::kDown:
  interaction.pressed_on(hit);
  manager.capture_pointer(event.window);   // 开始拖动
  break;
case PointerAction::kMove:
  if (auto target = interaction.capture_target(hit)) { /* 把拖动增量发给 target */ }
  break;
case PointerAction::kUp:
  interaction.released_on(hit);            // 窗口外抬起：released 但不 clicked
  break;                                   // 所有按键都抬起时捕获自动结束
```

语义：

- 捕获期间的坐标可能落在窗口外（负数或 ≥ `drawable_size()`），命中测试返回空；
  `Interaction` 本来就会保持按住状态并在窗口外抬起时只报 `released` 不报 `clicked`；
  `Interaction::capture_target(hit)` 在按住期间返回被按住的控件，否则返回 `hit`。
- 捕获在三种情况下结束：`release_pointer_capture()`、被捕获窗口关闭、某个 `kUp` 之后
  不再有任何按键按住。`pointer_capture()` 查询当前捕获的窗口。
- 捕获期间仍可能收到该窗口的 `kLeave`，它不会结束捕获。
- 平台调用（`SDL_CaptureMouse`）是尽力而为：dummy 驱动不支持；SDL 在多数桌面后端上
  按住按键时本来就会隐式捕获——这个 API 让它变得显式、可查询。`capture_pointer()`
  只在窗口 id 无效时返回 false。

### 事件循环：定时器与跨线程唤醒

定时器属于 `WindowManager`（不属于某个窗口），没有回调，触发结果从 `PumpResult` 读出：

```cpp
const dg::TimerId blink = manager.add_timer(530, /*repeat=*/true);
const dg::TimerId once = manager.add_timer(2000);
manager.cancel_timer(once);                 // 已触发/已取消/无效的 id 返回 false，重复取消无害

const dg::PumpResult pumped = manager.pump(-1);  // -1 = 不限时，只在有事情时醒来
for (dg::TimerId id : pumped.timers) { /* 按截止时间先后，同时到期按创建顺序 */ }
for (std::uint64_t payload : pumped.user_events) { /* 其他线程 post 的数据 */ }
if (pumped.woken) { /* 其他线程调用过 wake() */ }
```

- `pump(timeout)` 的实际等待时间是 `effective_wait_ms(timeout)`：被截短到最近一个定时器
  的截止时间（已过期则为 0）。没有定时器、没有事件时线程阻塞在 SDL 自己的等待里，空闲 CPU 为 0。
- 重复定时器每次 `pump()` 最多出现一次；迟到多个周期也只报一次，然后从"被发现到期的时刻"
  再排一个周期，不会补发。周期下限 1ms，延迟下限 0。
- 时间来自 `set_clock()`（默认 `dg::steady_anim_time`，即动画用的同一个时钟接缝）。测试传入
  一个返回手动推进的 `AnimTime` 的函数，截止时间就完全确定；派生的点击计数也读这个时钟。

#### 线程安全

`WindowManager` 的所有方法都只能在调用 `create()` 的线程（主线程）上调用，**唯二的例外**是
`wake()` 和 `post_user_event(uint64_t)`：可在任意线程、任意时刻调用（包括主线程正阻塞在
`pump()` 时），它们通过 SDL 自身线程安全的事件队列（`SDL_RegisterEvents` 注册的用户事件类型 +
`SDL_PushEvent`）投递，和其他事件一起按顺序送达、不会丢失；队列满时返回 false。多次 `wake()`
在一次 `pump()` 中合并为一个 `woken = true`。调用方必须在 `WindowManager` 析构或被移动之前
停止/join 这些线程。

### 焦点：复用 `RenderTree` 自身的树，没有第三棵树

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

#### `focus_order()` 的规则（`focus.h:147-172`）

- 默认按树的前序遍历（DOM 顺序）；`Widget::tab_index` 可以覆盖，正数排前面
  按升序，其余（含 0/未设置）按树序排在后面，负数**从 Tab 序里剔除**（但仍可
  被直接点击聚焦），完全对应 HTML 的 `tabindex` 语义。
- 跳过：没有挂 `Widget` 的节点、`is_focusable()` 判否的 kind、被禁用的节点及其
  整个子树、零面积节点（这也是弹层关闭后不再进 Tab 序的机制来源——关闭是裁成空矩形，
  见 [popups.md](popups.md)）。
- **不跳过 `opacity == 0`** 的节点：hit_test 本来就忽略 opacity，Tab 能到达
  的集合和点击能到达的集合保持一致。

#### `is_focusable()`（`focus.h:113-130`）

```
kButton / kCheckbox / kSlider / kTextField / kTextArea / kDropdown  -> true
kPanel / kLabel / kScrollView / kList / kScrollbar                  -> false
```

**`kScrollView`/`kList` 不能获得焦点**——不是遗漏，是刻意的：让一个滚动区域
成为 Tab 停靠点意味着需要键盘直接操作它（箭头/PageUp/PageDown 滚动它），
但这套"键盘滚动"机制此前完全不存在，先让它可 Tab 到达会造成"Tab 能到但没有
键能操作"的更差体验。（现在 `apply_keyboard_scroll()` 已经实现了 PageUp/
PageDown/Home/End 滚动，但走的是**从当前焦点节点向上找最近可滚动祖先**这条
路径，不是让 ScrollView/List 自身可聚焦——见下文。）

#### 焦点作用域（弹层边界与模态陷阱）

```cpp
focus.enter_scope(popup_root);          // Tab 被限制在 popup_root 子树内
// ... 打开期间 ...
focus.exit_scope(tree.render());        // 离开作用域，若当前焦点在作用域内会先 blur
```

`enter_scope(root, modal)` 的 `modal` 参数默认 `false`；传 `true` 时额外让
`set_guarded()` 拒绝把焦点设到作用域外——这是 `Dialog` 的模态焦点陷阱，和
普通弹层用的是**同一个作用域机制**，只是多了一个拒绝开关，不是第二套概念。

**只有一个活跃作用域，不是栈**：因为 `PopupHost` 从不允许弹层里再开弹层
（见 [popups.md](popups.md)），重复调用 `enter_scope()` 直接替换旧作用域，
没有嵌套恢复。

#### 焦点环：内缩，且每个弹层需要自己的实例

```cpp
dg::update_focus_ring(tree.render(), parent, ring, focus.current(), ring_color,
                      /*thickness=*/2, /*gap=*/2);
```

环的几何是**从目标 bounds 内侧向内画**——四条描边永远不超出目标自身的
bounds（`focus.h` 中 `FocusRing` 上方的长注释记录了它是从"外扩"改
过来的：外扩假设目标周围有空白边距可占用，但一个满宽贴边的列表行（如
下拉菜单的行）左右贴边、上下和相邻行贴边，外扩会画到相邻行内部甚至画到
surface 外面。内缩不依赖这个假设。

**每个打开的 popup 需要自己的 `FocusRing` 实例**——`update_focus_ring()`
只在 `ring.created == false` 时调用一次 `add_child()`，之后这四个节点就
永久挂在当时的 parent 下，而 `RenderTree` 没有 reparent 原语（commit
`5e630ef`）。宿主窗口的环节点在弹层打开之前就已经挂在 `LayoutTree::root()`
下了，如果复用它，画出来的环在绘制顺序上会被弹层内容盖住，而不是显示在
目标上方。所以每次开弹层都要 `FocusRing{}` 重新构造一个，挂在弹层自己的
`content_root` 下（`examples/21_focus/focus_window.cpp` 的弹层）。

**降级行为**：目标任一轴的尺寸小于 `2*(gap+thickness)`（默认 gap=2,
thickness=2，即 8px）时，`fill_ring()` 直接把整个目标涂成环颜色，而不是
画出一个反转/重叠的怪异描边（`src/widget/focus.cpp` 的 `fill_ring()`；
签名见 `focus.h` 的 `update_focus_ring()` 声明）。

### 快捷键：11 个 action，四级路由

`input/shortcuts.toml` 是唯一真相源，生成 action id、`LogicalKey` 枚举、
`ActionScope` 枚举——**内置绑定运行期不可改**，改它们要改这个 toml 重新生成
（应用自己的快捷键走运行期用户表，见下文「应用自定义快捷键」）。

当前定义的 11 个 action（`input/shortcuts.toml`，完整表见 `doc/reference/shortcuts.generated.md`）：

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
| `undo` | text_field | Mod+Z |
| `redo` | text_field | Mod+Shift+Z |
| `redo_alt` | text_field | Mod+Y |

`redo` 有两个惯用组合键，而一行 action 只能有一个绑定，所以 Mod+Y 是独立的
`redo_alt`（效果相同）。C++ 侧 `apply_clipboard_action()` 也处理这三个
action（调 `WidgetSet::editor_undo()`/`editor_redo()`）；C ABI 则在路由之前
直接对获得焦点的编辑框执行它们，宿主不需要 scope 或处理，改了文字照常发
`DG_EVENT_TEXT_CHANGED`。

**编辑键（不是快捷键，不进路由表）**：TextField 和 TextArea 都支持
Ctrl+Left/Right 按词移动（加 Shift 扩展选区）、Ctrl+Backspace/Ctrl+Delete
删除前/后一个词——`TextFieldMove`/`TextAreaMove` 的 `kWordLeft`/`kWordRight`
和 `WidgetSet::editor_delete_word()`。词的切分见 `text_edit.h` 的
`word_left()`/`word_right()`。

编辑框的双击/三击见上文「多击计数」。

#### 四级路由（design.md section 5.5.2，`router.h`）

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
  没有任何 shortcut 绑定）能共存而不冲突。kTextArea 的编辑意图更多：
  上面六个再加 Up/Down/PageUp/PageDown（`dg::Key::kPageUp`/`kPageDown`，
  SDL 的 `SDLK_PAGEUP`/`SDLK_PAGEDOWN` 映射过来），以及**不带 Mod/Alt** 的 Enter
  （裸 Enter 或 Shift+Enter 插入换行）；Ctrl/Alt+Enter 仍然进快捷键路由。
  kTextField 的六键集合没有变。Tab 对两种编辑框都不是编辑意图，仍然是焦点切换。
- IME 组字：`WidgetSet::editor_is_composing(focused)`（两种编辑框通用）为真时，**每一个**
  按键（包括组字期间也可能出现的方向键/Enter/Escape）都归输入法候选窗口，
  是硬隔离，不区分是否恰好也是编辑意图。

`route_key_event()` 把这两条早退写在函数体最前面，`resolve_action()` 本身
甚至不会被提到——这是测试能直接验证的结构性保证：删掉
早退语句，`tests/unit/test_shortcut_routing.cpp` 就会失败。

#### 应用自定义快捷键（运行期表）

生成表之外，运行时另有一张**运行期用户表**（`dg::UserShortcut`，`router.h`）：

```cpp
app.add_shortcut("Ctrl+Shift+N", dg::kUserActionBase + 1);        // 第 4 级
window->add_shortcut(panel, "F2", dg::kUserActionBase + 2);        // 第 2 级
std::string label = app.chord_label("ctrl+shift+n").value();       // "Ctrl+Shift+N"
```

- **id 空间**：`action_id >= dg::kUserActionBase`（0x8000，C 的
  `DG_USER_ACTION_BASE`）归应用；更小的是 `input/shortcuts.toml` 的内置
  `DG_ACTION_*`。id 太小或组合键解析失败返回 `kInvalidArgument`。
- **语法**：`parse_user_chord()`——修饰键 `Ctrl`/`Control`/`Mod`/`Cmd`（都是
  `Mod`）、`Shift`、`Alt`，任意顺序、不分大小写，最后一段是键名（同样不分大小写）。
  可用键由 `input/shortcuts.toml` 的 `[keys] extra` 列表生成：A–Z、0–9、F1–F12、
  Delete、Insert、Enter、Escape、Space、Backspace、方向键、Home/End/PageUp/PageDown、
  Minus/Equals/Comma/Period/Slash、Menu（以及仅供菜单栏用的 `AltKey`，不能做组合键）。
  SDL 键码映射也从这张表生成（`src/platform/sdl3/sdl_key_table.generated.inc`）。
- **优先级**：每一级先查用户表、再查生成表——第 2 级在冒泡到的每个节点上
  先看该节点的 `Window::add_shortcut()` 绑定，第 4 级先看 `App::add_shortcut()`。
  所以自定义键能遮蔽内置键（例如把 `PageDown` 改作他用）。
- **编辑键永远优先**：编辑框自己的编辑键（方向键、Home/End、Backspace/Delete、
  TextField 上不带 Ctrl/Alt 的 Enter/Up/Down/PageUp/PageDown/Escape）、撤销/重做、
  剪贴板组合键都在路由之前被编辑框吃掉，不会触发同名的自定义键。另外，焦点在
  编辑框里时，**不带 Ctrl/Alt 的可打字键**（字母、数字、Space、标点，含 Shift+字母）
  不查用户表——绑定裸 `K` 不会吞掉键入的 k。F2 之类的非编辑键照常触发。
- **冲突与替换**：`App::add_shortcut()` 对同一组合键再绑定即替换；
  `Window::add_shortcut()` 在同一个 `scope` 上重复绑定同一组合键返回
  `kAlreadyAttached`（不同 scope 各自独立，最近的 scope 胜）。
  `App::remove_shortcut(chord)` 删除该组合键的全部用户绑定（应用级和所有窗口的
  作用域级），没有则 `kUnknownId`。scope 节点被删除时它的绑定随之删除。
- 模态对话框打开期间不路由快捷键。

#### 键盘右键菜单（Menu 键 / Shift+F10）

Menu 键（裸按）或 Shift+F10 在编辑键处理之后、快捷键路由之前被运行时直接处理
（不是生成表里的 action，不占锁定 id）：对**焦点节点**所属的 create() 节点（没有
焦点时是窗口根）产生 `ContextClickEvent`，`clicks = 0`。若该节点是 List/Tree/Table
且有选中行，`item` = 选中行（通过公开的 `ListRef/TreeRef/TableRef::selected()`）。
`x`/`y` 是该**节点**的左下角——行几何不是公开接口，所以不是行本身的位置；在此处
`open_menu()` 即得到节点下方的菜单。

#### `Mod` 伪修饰键

```cpp
enum class Platform : std::uint8_t { kLinux, kWindows, kMacos };
std::string_view mod_label(Platform platform);  // Linux/Windows -> Ctrl, macOS -> Cmd
```

`Platform::kWindows`/`kMacos` **只是为了让这条替换规则本身可测试**——这个
项目目前只有 Linux 后端（README 的"no platform abstraction ahead of a
second backend"原则），这两个枚举值不代表存在对应的窗口系统实现。

#### 剪贴板

```cpp
manager.set_clipboard_text("hello");
std::string text = manager.get_clipboard_text();

dg::TextFieldEditContext ctx{tree, fonts, widgets, manager};
bool did = dg::apply_clipboard_action(DG_ACTION_COPY, field_node, ctx);
```

`apply_clipboard_action()`（`include/drawgui/shortcuts/clipboard_actions.h:87`）
处理 copy/cut/paste/select_all 四个 action，全部注册在 `ActionScope::kTextField`
下。**复制/剪切在没有选区时是 no-op**，剪贴板不动，返回 `false`——这是
刻意决定，不是"选中全部再复制"的隐式回退。粘贴走 `WidgetSet::editor_insert()`（单行框即 `text_field_insert()`），
它会先删除选区再插入；单行框会丢弃 ASCII 控制字符（含换行）——粘贴
带换行的文本到单行文本框会静默丢失换行（多行编辑框保留换行，见 [text.md](text.md)）。

#### 键盘滚动

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

### 鼠标指针形状

指针形状是**节点属性** `DG_PROP_CURSOR`（id 53，枚举，见 `doc/reference/properties.md`），
写进 `NodeStyle::cursor`；默认 `auto` = 继承。解析在 `drawgui/widget/cursor.h`：

- `dg::resolve_cursor(tree, widgets, node)`：从 `node` 沿父链往上，
  1. 第一个自己设了非 `auto` 值的节点决定（包括被禁用的节点——想在禁用按钮上显示
     `not_allowed` 就显式设它）；
  2. 否则，第一个**有自己默认值**的控件决定，但命中节点被禁用（`is_disabled()`）时控件
     默认值一律不算；
  3. 都没有：`default`（箭头）。
- `dg::widget_default_cursor(kind)`：**只有文本框是 `text`（I 形），其余全是箭头**。这是
  桌面惯例（Windows/macOS/GNOME/KDE 的原生按钮、勾选框、下拉框、列表行、滑块都不变成
  手形；手形是超链接光标），不是网页惯例。想要网页风格就给按钮（或它们的容器）设
  `pointer`。默认值是箭头的控件“没有意见”，不会挡住祖先设置的值。禁用控件显示箭头，
  不是 `not_allowed`，同样是原生工具包的做法。
- `dg::cursor_at(tree, widgets, interaction, point)`：事件循环该调的入口。没有按压时对
  `hit_test(point)` 的节点做上面的解析；**按压进行中则由被按住的控件决定**——在文本框
  里拖选拖出框外仍是 I 形，按住按钮拖到别处也不会一路闪过邻居的光标，和
  `Interaction::moved_over()` 的隐式抓取一致。

窗口这一侧（`WindowManager`）：

```cpp
manager.set_cursor(window, dg::cursor_at(tree.render(), widgets, interaction, at)); // 每次指针事件后
manager.set_cursor_override(window, dg::Cursor::kResizeEw);  // 自己的拖拽期间钉住光标
manager.set_cursor_override(window, std::nullopt);           // 拖拽结束
manager.current_cursor(window);                               // 生效值：覆盖值优先
```

- 每个窗口各记一份“解析值”和可选的“覆盖值”；`auto` 按 `default` 存。
- 平台光标是全局（每个指针一个）的，所以只有指针所在的窗口（最近一次 motion/enter
  事件来自的窗口；还没有过这类事件时任何窗口都算）的光标会真正上屏，指针进入另一个窗口
  时切换成那个窗口的。
- **只在形状变化时才调用 `SDL_SetCursor`**，所以每次 motion 都调 `set_cursor()` 是安全的。
  系统光标按需 `SDL_CreateSystemCursor` 并缓存到 `WindowManager` 销毁。
- 平台造不出某个光标（SDL 的 dummy 驱动根本没有光标）时静默跳过，每种形状只尝试一次，
  不刷错误；`current_cursor()` 照样报告请求的值。`cursor_switch_count()` 是给测试观察
  “只在变化时切换”用的计数器。

C ABI 不需要做任何事：`dg_wait_events()`/`dg_poll_events()` 的内部泵在每次指针
移动/按下/抬起后调用 `cursor_at()` + `set_cursor()`。注意属性改了但指针没动时，光标要等
下一次指针事件才更新。`dg::App` 同样如此。

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
- `examples/22_dropdown_menu`、`examples/23_menu_tooltip_dialog` — 运行时里的弹层焦点限定与模态对话框（`--verify-*` 用注入的按键检查）。
- 快捷键路由与剪贴板动作目前没有独立的可视化 example，主要靠
  `tests/unit/test_shortcut_routing.cpp`、`tests/unit/test_shortcuts.cpp`
  验证。
