# 动画（Animation）

## 用 `dg::App`

运行时会推进每个窗口自己的 `AnimationEngine`：`App::pump()` 每次都按应用时钟 `tick()`
一次，把插值写进节点属性并重绘；只要还有动画在跑，等待就被截到约 16 ms 一帧，
没有动画时 `pump(-1)` 照旧一直阻塞（空闲不占 CPU）。

```cpp
// 显式动画：from 为 std::nullopt 时取当前值
auto id = win->animate(card, DG_PROP_OPACITY, std::nullopt, dg::PropValue::number(1.0F),
                       200, dg::kCurveEaseOut);
// 隐式过渡：之后的 set_prop()/set() 都会插值；时长 0 删除过渡
win->set_transition(card, DG_PROP_BACKGROUND_COLOR, 150, dg::kCurveEaseInOut);
win->set_prop(card, DG_PROP_BACKGROUND_COLOR, dg::PropValue::color(hover));
win->cancel_animation(id.value());   // 停在当前值，不产生结束事件
```

- `Window::animate(node, prop, from, to, duration_ms, curve)` 返回 `AnimationId`
  （> 0）；动画走到终点时 `pump()` 返回 `events::AnimationFinishedEvent{window, node, animation}`。
  被 `cancel_animation()` 取消、节点被删除、隐式过渡都**不**产生这个事件。
- 只插值 float/length/color 属性；其它类型直接跳到 `to` 并返回 0。`from` 省略时只有
  背景色、边框色、不透明度、left/top/width/height 读得回当前值，其它属性返回 `kUnsupported`。
- 动画写在节点自身：`set_prop()` 会转发到组合子节点的属性（编辑框的文字、滚动视图的内容）
  不参与动画/过渡。
- `Window::animating()` 表示该窗口还有动画在跑。
- `App::set_reduced_motion(true)`：所有窗口（含以后创建的）的动画时长都视为 0，下一次
  `pump()` 即到终值（照常产生结束事件）。
- `App::set_clock(fn)`：测试用的时钟接缝，返回 `AnimTime`；不能早于已经用过的时间
  （从 `steady_anim_time()` 起步）。确定性测试见 `tests/unit/test_runtime_loop2.cpp`。
- 一段空闲之后才开始的动画从开始那一刻计时，不会因为之前的等待而一下子跳完。
- C ABI：`dg_node_animate()`、`dg_node_set_transition()`、`dg_animation_cancel()`、
  `DG_CURVE_*`、`DG_EVENT_ANIMATION_FINISHED`、`dg_app_set_reduced_motion()`，见
  [c-abi.md](../reference/c-abi.md)。

`Window::animation()` 仍是逃生舱：直接调用它的 `animate()` 也会被运行时 `tick()`，但
不会分配 `AnimationId`、不产生 `AnimationFinishedEvent`。主题切换与飞行中的过渡冲突的
已知缺陷（见下文）在运行时里同样存在。

## 这个子系统解决什么问题

一个属性从 A 值变到 B 值，希望它在一段时间内平滑过渡而不是瞬间跳变——
按钮 hover 时背景色渐变，弹窗打开时滑入，光标闪烁。`dg::AnimationEngine`
提供两种用法：显式的 `animate(from, to, duration, curve)` 一次性动画，和
CSS `transition` 风格的隐式过渡——声明一次 `set_transition()`，之后任何
`set_value()` 写入都会自动插值。

0.2 起动画也导出到了 C ABI——见下文"ABI 边界"一节。

## 直接使用底层

自己持有 `AnimationEngine` 时：每帧用同一个时钟值 `tick()`，`has_active()` 为真时
用短超时 `pump()`，并在主题切换前处理飞行中的过渡（见下文已知缺陷）。

改完要遵守 [`app.md` 的逃生舱约定](app.md#逃生舱约定)：只改运行时不组合的东西，改完调用 `Window::invalidate(node)`。

### 核心类型

```cpp
struct AnimTime { std::int64_t ms = 0; };
AnimTime steady_anim_time();  // 唯一真正读系统时钟的地方

using dg_curve_id = std::uint16_t;
constexpr dg_curve_id kCurveLinear, kCurveEaseIn, kCurveEaseOut, kCurveEaseInOut;

struct AnimHandle { std::uint32_t index; std::uint32_t generation; };

class AnimationEngine {
 public:
  AnimHandle animate(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                     const PropValue& from, const PropValue& to,
                     std::int64_t duration_ms, dg_curve_id curve_id);
  void set_transition(NodeId node, dg_prop_id prop_id,
                      std::int64_t duration_ms, dg_curve_id curve_id);
  void clear_transition(NodeId node, dg_prop_id prop_id);
  PropWrite set_value(LayoutTree& tree, NodeId node, dg_prop_id prop_id,
                      const PropValue& target);
  void tick(LayoutTree& tree, AnimTime now);
  bool has_active() const;
  std::vector<AnimEvent> poll_events();
  void set_reduced_motion(bool enabled);
};
```

### 时钟是值语义

`AnimTime` 是一个普通值（`ms` 计数），不是虚接口（`include/drawgui/anim/clock.h`）。
生产代码调用 `steady_anim_time()`；测试直接构造任意 `AnimTime` 推进
`tick()`，用手选的精确增量，没有任何 sleep——这个项目在 `src/`/`include/`
里没有一处 `virtual`，每一个需要"可测试的可变输入"的地方都是把变化量做成
普通参数（`WindowManager::warp_pointer()` 传 x/y 而不是抽象一个"输入设备"），
`AnimationEngine` 延续了同样的做法。

### 四条曲线

```cpp
kCurveLinear, kCurveEaseIn, kCurveEaseOut, kCurveEaseInOut
float evaluate_curve(dg_curve_id curve_id, float t);  // t 已被调用方 clamp 到 [0,1]
```

`dg_curve_id` **刻意不接入** prop_id/token_id/action_id 那套"生成器 +
append-only lock"体系（`include/drawgui/anim/curve.h:10-23`）：那套机制存在
的意义是防止一个事实在多个独立消费者之间漂移，而 `curve_id` 目前只有一个
消费者——`evaluate_curve()` 自己。等 C ABI 真正导出 `dg_animate` 时，
`curve_id` 才会成为第四个 ABI 编号族，在那之前只是普通 `constexpr` 常量。
未知 curve_id 会退化成线性插值，而不是报错——因为这是一个完全在本地控制
的小闭集，不像 prop_id 那样可能来自跨进程边界。

### 显式动画：`animate()`

```cpp
slide_handle_ = engine_.animate(scene.tree, scene.handles.slide_chip, DG_PROP_LEFT,
                                dg::PropValue::number(from), dg::PropValue::number(to),
                                anim_scene::kSlideDurationMs, dg::kCurveEaseInOut);
```

（`examples/17_animation/anim_window.cpp:145-147`）

覆盖所有 `PropType::k_float`/`k_length`/`k_color` 属性——`PropValue` 里
"有明确的中间值"的三种标量类型。`k_enum`/`k_gradient`/`k_shadow`/
`k_transform`/`k_image` 不插值：请求给这些属性 `animate()`，写入会立刻跳到
目标值（不报错，但 debug 构建会在 stderr 打印诊断）。

`animate()` 对类型不匹配（`from`/`to` 类型不一致，或与 `dg::prop_type(prop_id)`
不一致）返回 `AnimHandle{}`（generation 为 0，真实动画的 handle 永远不会是
这个值）——用返回值报告失败，不抛异常，和 `PropWrite` 的"每种结果都要能
被观察到，不能静默"的风格一致。

`AnimHandle{index, generation}` 用两个字段而不是一个裸 index，是因为动画槽
位的生命周期和渲染节点不同：渲染节点**从不销毁**（append-only），一个动画
槽位**必须**在动画完成/取消后被回收复用，否则一个长期运行的进程（比如一个
按钮被 hover 了一万次）会无限增长槽位数组。裸 index 在复用场景下会产生 ABA
问题——一个持有旧 handle 的调用方可能意外操作到复用了同一 index 的新动画。
`generation` 在每次槽位被释放（完成、取消、被回收复用）时递增，每个访问器
在操作前都会校验它，不匹配就返回 `kStaleHandle` 而不是操作别人的动画。

### 隐式过渡：`set_transition()` + `set_value()`

```cpp
engine_.set_transition(scene.handles.hover_panel, DG_PROP_BACKGROUND_COLOR,
                       anim_scene::kHoverTransitionMs, dg::kCurveEaseInOut);
// ... hover 事件发生时,调用方改用 set_value() 代替 dg::set_prop() ...
engine_.set_value(scene.tree, scene.handles.hover_panel, DG_PROP_BACKGROUND_COLOR,
                  dg::PropValue::color(hover_fill));
```

`set_transition()` 只是**声明**："从现在起对 (node, prop_id) 的写入会插值"，
和当前有没有正在跑的动画无关，就像 CSS 的 `transition` 声明和它将来要
应用到的值无关一样。`set_value()` 是调用方之后每次真正写值时改用的入口。

#### 只对 7 个属性有效——这是隐式过渡最容易被忽视的边界

`set_value()` 第一次触发某个 (node, prop_id) 的隐式过渡时，需要知道
"当前值"才能算出插值的起点（`from`）。这个"读回当前值"的能力由
`AnimationEngine::read_current()` 提供，它的 switch 是**有限列表**，核实
`src/anim/animation_engine.cpp:113-161` 后确认恰好是这 7 个：

| prop_id | 读回的位置 |
|---|---|
| `DG_PROP_BACKGROUND_COLOR` | `NodeStyle::fill` |
| `DG_PROP_BORDER_COLOR` | `NodeStyle::border_color` |
| `DG_PROP_OPACITY` | `NodeStyle::opacity` |
| `DG_PROP_LEFT` | `BoxStyle::left` |
| `DG_PROP_TOP` | `BoxStyle::top` |
| `DG_PROP_WIDTH` | `BoxStyle::width` |
| `DG_PROP_HEIGHT` | `BoxStyle::height` |

**这 7 个之外的属性，第一次触发 `set_transition()` 时会静默跳变**——不是
报错，是直接落到目标值，和"没有声明过 transition"表现一样，只是在 debug
构建里会有一条诊断信息。这个列表是**故意有限而非穷举**的：`node_props.h`
描述的可插值属性大约还有 29 个没有被覆盖，只是因为现有客户端没有用到，
不是因为技术上做不到——想要支持另一个属性只需要在这个 switch 里加一个
case，没有新机制要建。排查"为什么这个属性没有过渡效果"时先查这张表。

已经在跑的动画槽位（第二次及以后的 `set_value()` 调用，转为"重定向"逻辑，
不受这条限制——只有**第一次触发、需要从当前值起插值**的场景才需要
`read_current()`。

#### `clear_transition()` 是唯一的逆操作

```cpp
engine_.clear_transition(node, DG_PROP_BACKGROUND_COLOR);
```

`set_transition(node, prop_id, /*duration_ms=*/0, curve_id)` **不等价于清除**：
duration 只是被钳到 0 并存下来，`set_value()` 仍然会走 transition 分支，
只是会在（近乎）一帧内完成——它依然是一次声明，不是撤销声明。撤销声明的
唯一方式是 `clear_transition()`：之后 (node, prop_id) 的写入直接落回普通
`dg::set_prop()`。清除**不影响**已经在飞的动画槽位——那个槽位会播放到
结束，和 CSS 的 `transition: none` 不影响已经在跑的过渡是一个道理。

`cancel_all_for(node)` 是**整节点级**的：取消该节点上所有正在飞行的动画槽位
（无论是显式 `animate()` 还是隐式过渡），并且**忘记该节点的一切已声明
transition**——不要把它当成"清除某一个 (node, prop_id) 的 transition"的
替代品，它的粒度是整个节点。

### `tick()` / `has_active()` / `poll_events()`：轮询，不是回调

```cpp
engine.tick(tree, dg::steady_anim_time());
if (engine.has_active()) { /* 帧循环用短超时驱动 pump() */ }
for (const dg::AnimEvent& event : engine.poll_events()) {
  // event.kind: kCompleted / kCancelled
  // event.handle 对隐式过渡永远是 AnimHandle{}（index=0, generation=0）,
  // 显式 animate() 才有真实 handle —— 用 event.node/event.prop_id 区分
}
```

完成和取消**通过轮询事件传递，从不使用跨线程回调**——设计上明确禁止
回调（"不用跨线程回调"）。`PumpResult` 是这个模式的先例：把"这次调用
捞到了什么"整体返回，而不是边发生边通知调用方。

`tick()` 每次调用会推进每一个存活且未暂停的槽位——推进量是本次 `now` 与
上一次 `tick()` 调用时间的差值（第一次调用差值为 0，所以创建动画后用
**同一个** `AnimTime` 立即 tick 一次不会把它意外推进）。写入依然走
`dg::set_prop()`，这意味着一个动画帧和一次手动写入对下游（layout、damage、
paint）而言完全无法区分。

### `AnimHandle{index, generation}`

见上文"显式动画"一节——两个字段是为了解决动画槽位复用引发的 ABA 问题，
渲染节点那套"永不释放、所以永不悬空"的做法在这里不适用：渲染节点的数量在一个会话里是有界增长的（append-only），
动画数量是无界的（每一次短暂的 hover 都会用掉一个槽位），必须回收复用。

### 已知缺陷：主题切换与飞行中的 transition 冲突

（与 [theming.md](theming.md) 共享同一条记录，未修复）

如果一个 (node, prop_id) 同时被 `bind_token()` 绑定、又被
`AnimationEngine` 驱动着一个正在飞行的动画/过渡：

1. `ThemeBindings::apply()` 在主题切换时无条件重写一次这个属性的值。
2. `AnimationEngine::tick()` 根据自己保存的 `from`/`to` 重新算出插值并
   写回——它完全不知道主题刚刚切换过。
3. 结果是主题切换的效果**只在一帧内可见**，下一次 `tick()` 就把它覆盖
   回动画自己的插值轨迹。

这不是内存安全问题，也不违反任何一方自己的文档承诺——两个系统各自都
按自己文档写的规则运作，只是两者组合在一起产生了此前任何单独测试都没
覆盖到的行为。目前没有修复计划。

### ABI 边界

0.2 起动画通过 `dg::App` 的事件泵导出到 C ABI（见上文「用 `dg::App`」）：
`dg_node_animate()` 返回每窗口的 `int32_t` 动画 id（不是设计稿里的 `dg_anim_t*`），
`dg_node_set_transition()` 声明隐式过渡，`dg_animation_cancel()` 取消。
`pause()`/`resume()`/`reverse()` 仍只在 C++ 的 `AnimationEngine` 上可用。

### CPU 光栅下的开销约束

这个引擎本身的插值计算很轻，真正的成本在每一帧因为动画而触发的重绘/重排
落到 Skia CPU 光栅管线上要花多少时间——这属于渲染管线本身的开销特性，
不是动画子系统独有的问题，详见 `doc/limitations.md`。

## 相关 example

- `examples/17_animation` — 三个真实客户端：显式 `animate()` 做滑动、
  隐式 transition 做 hover 背景色渐变、四段串联的一次性动画做光标闪烁，
  以及 `--reduced-motion` 的降级效果。
