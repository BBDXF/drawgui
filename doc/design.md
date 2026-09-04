# drawgui 设计文档

> 状态：设计阶段（2026-09-03）
> 参考教训来源：[stonegui2 项目复盘](https://github.com/BBDXF/stonegui2/blob/main/doc/retrospective.md)

## 1. 项目定位

drawgui 是一个**轻量级自绘 GUI 内核**，架构参考 Flutter 的分层渲染模型，用 C++ 实现，
对外导出**纯 C ABI** 供任意语言绑定，并官方提供一套 **Preact/Vue 风格的 JS 声明式框架**。

**独特价值主张**（stonegui 复盘第 4 节要求必须先回答这个问题）：

> 相对 Qt QML / Flutter，drawgui 的价值是**"一个可被任意语言以 C ABI 嵌入的自绘 GUI 内核"**，
> 而不是"又一个完整桌面应用框架"。宿主语言不需要 Dart VM、不需要 QML 引擎、
> 不需要嵌入整个 Chromium，只需要能调 C 函数。

明确接受的取舍：本项目**不与 Qt/Flutter 在功能完备度上竞争**。它在"嵌入成本"和
"语言中立性"上取胜，在无障碍、复杂文本编辑、原生集成深度上会长期落后。

## 2. 从 stonegui 复盘继承的硬约束

以下五条是**架构层约束**，不是待办事项。违反其中任何一条都需要重构而非增量修补。

| # | 约束 | 来源 |
|---|------|------|
| C1 | 平台胶水（窗口/事件循环/文件监听/信号/生命周期）**第一天**抽成接口，核心层不出现任何 `#ifdef __linux__` | 复盘 2.6 —— inotify + POSIX 信号把 stonegui 锁死在 Linux |
| C2 | 输入模型**原生设计为桌面语义**（鼠标 + 键盘 + 焦点链 + 修饰键 + 拖放），不是"先做触摸再打补丁" | 复盘 2.2 —— 三处 SDL 事件拦截被其作者定性为"先兆，不是终点" |
| C3 | 窗口语义（多窗口 / 对话框 / popup）**从第一天设计**，不是应用内模态层假装 | 复盘 2.1 |
| C4 | CI / formatter / linter / 回归测试 / 一致性检查脚本**在写第一个控件之前**就位 | 复盘 2.7 + 第 8 节 —— "删了就再没有任何防线" |
| C5 | 无障碍留架构挂点（a11y 节点树 + 语义属性），MVP 不实现但不能堵死 | 复盘 2.3 |

复盘第 7 节点名的**可迁移资产**，本项目直接继承其设计（非代码）：

- signal / effect / memo / owner 树 / batch 的响应式核心
- token 化主题分层：语义色阶 + 整数 token 的二分法、`$token` 活引用、伪状态与 part 的**一层嵌套上限**

## 3. 技术选型

| 维度 | 决定 | 理由 / 备注 |
|------|------|-------------|
| 目标平台 | Windows / macOS / Linux / Android / iOS / 嵌入式 Linux(有 GPU) | **明确排除无 GPU 裸屏** —— 这使 GPU 可作为渲染基线 |
| 核心语言 | **C++20 基线**，C++23 库特性有条件使用 | 见 §3.1 |
| 渲染 | **Skia 单后端**，不做多后端抽象 | 排版 / GPU / 五平台一次解决；接受 8–15 MB 体积 |
| Skia 获取 | **rust-skia/skia-binaries 预编译静态库** | 见 §3.2 |
| 窗口/输入 | SDL3（`IPlatform` 接口的首个实现） | 覆盖全部五平台；接口独立于 SDL，未来可换原生实现 |
| ABI | 纯 C99，**只导出精选面** | 不暴露内部 RenderObject 层 |
| JS 运行时 | Node/Bun FFI（一期） + 内嵌 QuickJS（二期） | 共用同一 C ABI |
| 主题 | 编译期 schema（TOML）+ 运行时 theme package（JSON + 资源） | 见 §5.7 |
| 构建 | CMake ≥ 3.24 + Ninja | |

### 3.1 C++ 标准选择

**语言基线定为 C++20**，理由是跨全平台的确定性：Android NDK r27（LLVM 18）与
Xcode 15 的 libc++ 对 C++20 支持完整，对 C++23 标准库支持则参差不齐。

C++23 的库特性（`std::expected`、`std::print`、`std::mdspan`）**不直接使用**，
而是提供最小自研替代（`dg::Expected<T, E>`），避免平台差异污染核心代码。
语言特性层面 deducing this 等同样不用。

实际用到的 C++20 特性：concepts（约束 RenderObject 协议）、ranges（子节点遍历）、
designated initializers（属性结构体）、`<span>`（零拷贝缓冲传递）、`consteval`（prop_id 编译期生成）。

> 若未来放弃移动端，可无痛升到 C++23。反之若一期就用 C++23，移动端会在最不该出问题的时候出问题。

### 3.2 Skia 集成策略

**一期：消费预编译静态库。** 不进 GN/depot_tools 的坑。

- 来源：`rust-skia/skia-binaries` releases（对应 Skia `chrome/m153`），活跃维护
- 覆盖的 target triple 正好是本项目目标平台：`x86_64-pc-windows-msvc`、
  `x86_64/aarch64-unknown-linux-gnu`、`x86_64/aarch64-apple-darwin`、
  `aarch64/x86_64-linux-android`、`aarch64/x86_64-apple-ios`
- 需要的 feature 组合：`textlayout`（SkParagraph）+ `svg` + `webp` +
  `gl`（Linux/Win/Android）/ `metal`（macOS/iOS）/ `vulkan`（可选）
  - **`webp` 必须显式启用** —— 默认 codec 集合仅含 BMP/GIF/ICO/JPEG/PNG/WBMP（§5.10.2）
  - `textlayout` 引入 HarfBuzz + ICU 依赖，`icudtl.dat` 的分发方式需单独处理（§5.10.5）
- 头文件：从 `rust-skia/skia` 对应 tag 取，与二进制版本严格对齐
- 实现：`cmake/FetchSkia.cmake` —— 按 target triple 下载、校验 SHA256、解压到 `third_party/skia-prebuilt/`

**已知风险与对策**：

- rust-skia 的产物同时包含 `libskia.a` 和 `libskia-bindings.a`，后者是 Rust 专用，**不链接**
- 该二进制来自 rust-skia 的 Skia fork（含少量 patch），需在 `doc/skia-provenance.md` 记录确切 tag 与 patch 列表
- **备选来源**：skia4delphi 的 `Binary/` 提供 `libsk4d`（动态库 + C ABI），但既然核心是 C++
  直接链 C++ API，不需要它。其真正价值是 GN args 与 compiler-rt workaround 参考
  （macOS `os_version_check.c.o`、Android `emutls.c.o` 弱符号处理）
- **JetBrains/skia-pack 已于 2026-03 归档，不作为依赖**

**二期（可选）**：若需裁剪体积或改 Skia 行为，再引入自建 GN 构建。届时 `FetchSkia.cmake`
的接口不变，只切换 provider。

## 4. 总体架构

```
┌─────────────────────────────────────────────────┐
│  JS 框架层  framework.js + JSX + drawgui.d.ts   │  ← 独立子目录
├─────────────────────────────────────────────────┤
│  语言绑定   Bun:ffi / node koffi / QuickJS 模块 │
╞═════════════════ C ABI 边界 ═════════════════════╡
│ 7  ABI       abi/drawgui.h  纯 C99 精选导出面   │
├─────────────────────────────────────────────────┤
│ 6  Theme     token 化主题、light/dark 运行时切换 │
│ 5  Widget    30+ 控件，全部由 RenderObject 组合  │
│ 4  Input     命中链 / 焦点树 / 快捷键 / 拖放     │
│ 3  Render    约束布局 / 绘制 / Layer / 命中测试  │
│ 2  Graphics  Skia 封装：Canvas / Paragraph / Gr │
│ 1  Platform  IPlatform / IWindow 抽象（C1）      │
└─────────────────────────────────────────────────┘
```

**关键设计**：第 3 层是"轻量"的来源。控件不各自造轮子，全部踩在约十来个
RenderObject 之上（Box / Flex / Stack / Text / Image / Path / Scroll / Clip /
Transform / Opacity / CustomPaint）。这是 Flutter 模型最值得抄的部分。

**第 4 层独立成层**是 C2 的直接落实。stonegui 的教训是：如果输入语义寄生在
渲染库的 indev 抽象上，桌面交互就只能靠拦截打补丁，成本随交互丰富度指数增长。

## 5. 分层设计

### 5.1 Platform 层

核心接口（`include/drawgui/platform/platform.h`）：

```
IPlatform
  ├─ capabilities() -> PlatformCaps
  ├─ create_window(WindowDesc) -> IWindow*
  ├─ attach_native(dg_native_window) -> IWindow*   // 嵌入模式（§5.14.5）
  ├─ run_loop() / wake()             独立模式：drawgui 拥有循环
  ├─ pump_once()                     嵌入模式：宿主拥有循环
  ├─ clipboard_get/set()             文本剪贴板为 T1，富格式为 T2
  ├─ displays()                      多显示器 + 每屏 DPI 缩放
  ├─ watch_files(paths, cb)          热重载用，抽象 inotify/FSEvents/RDCW
  ├─ on_lifecycle(cb)                pause/resume/low_memory（§5.14.7）
  └─ query_service<T>()              T2 可选服务，不支持返回 nullptr（§5.14.2）

IWindow
  ├─ id() / kind() / owner()
  ├─ set_title/size/position/visible/opacity   // opacity = 整窗合成不透明度（§5.11.5）
  ├─ begin_frame() -> SkSurface*  ←→ end_frame()
  ├─ dpi_scale() / logical_size() / physical_size()
  ├─ start_text_input(rect) / stop_text_input()   ← IME 挂点
  ├─ set_cursor() / capture_pointer()
  └─ request_redraw()
```

**PlatformCaps** 是多窗口跨平台的关键。移动端没有 OS 级 popup 窗口，
桌面端有。核心层不能假设：

```cpp
struct PlatformCaps {
  bool multi_window;      // 移动端 false
  bool native_popup;      // 决定 Menu/Dropdown/Tooltip 的落地方式
  bool native_menubar;
  bool system_tray;
  bool window_transparency;   // 带 alpha 的帧缓冲；为 WindowDesc 创建期标志（§5.11.5）
  bool file_dialog;
};
```

MVP 实现：SDL3 后端一份代码覆盖五平台。但**接口设计不引用任何 SDL 类型**，
将来 macOS 换 AppKit、Windows 换 Win32 时不动上层。

### 5.2 多窗口模型（C3）

对象归属：

```
App (单例)
 ├─ FontManager / ThemeRegistry / GrDirectContext（共享）
 ├─ active_window
 └─ Window[N]
      ├─ RenderView（根 RenderObject）
      ├─ LayerTree
      ├─ SkSurface（每窗口独立）
      ├─ FocusManager（每窗口独立焦点树 + tab 序）
      └─ PointerRouter（hover 状态 / pointer capture）
```

**GPU 上下文策略**：桌面上创建**一个共享 GL context**（SDL3 的 GL share group）
+ **一个 GrDirectContext**，每窗口一个 `SkSurface`。这样字体图集与纹理缓存跨窗口共享。
代价是渲染每个窗口前需 `SDL_GL_MakeCurrent`。

> 回退方案：每窗口独立 GrDirectContext。实现简单但缓存不共享，弹出一个下拉框
> 就重建一次字体图集。**不采用**，除非共享方案在某平台被证实不可行。

**窗口种类**：

| kind | 语义 |
|------|------|
| `Normal` | 普通顶层窗口 |
| `Dialog` | 有 owner，可模态 / 非模态 |
| `Popup` | 无边框、不抢焦点、点击外部自动关闭 |
| `Tooltip` | 无边框、不接收输入 |

**PopupHost 抽象 —— 本设计最关键的一处**：

下拉框、右键菜单、tooltip 需要能超出父窗口边界。桌面上正确做法是**真 OS 窗口**；
移动端只能是**应用内 overlay 层**。因此 Widget 层不直接创建窗口，而是向
`PopupHost` 请求展示，由 `PlatformCaps.native_popup` 决定落地形式：

```
PopupHost::show(content_node, anchor_rect, placement, flags)
  → caps.native_popup ? 创建 Popup 窗口 : 挂到当前窗口的 OverlayLayer
```

**这个抽象必须在 MVP 就存在**。如果先用应用内模态层实现菜单，后期改成真窗口
等于重写整个菜单/下拉/tooltip 子系统 —— 这正是 stonegui 复盘 2.1 描述的困境。

**事件路由**：所有输入事件带 `window_id`。快捷键两级路由（窗口级 → App 级）。
指针捕获是窗口级的（拖动时鼠标移出窗口仍收事件）。

**生命周期**：桌面上最后一个窗口关闭 → App 退出（可配置）。
`on_close_request` 可取消（用于"未保存"提示）。移动端由 OS 生命周期驱动
（`pause` / `resume` / `low_memory` 必须在 `IPlatform` 接口里有位置，即使 MVP 不实现）。

### 5.3 Graphics 层

薄封装，不做后端抽象（已决定 Skia 单后端）：

- `Canvas` —— 包装 `SkCanvas`，收敛到本项目实际用到的约 40 个绘制操作
- `Paragraph` —— 包装 `SkParagraph`，提供文本测量 / 换行 / 命中 / 光标定位 / 选区矩形
- `FontManager` —— 字体注册、fallback 链、CJK 字体自动发现
- `ImageCache` —— 解码结果与 GPU 纹理缓存

**Skia 类型不泄漏到第 3 层以上**。第 3 层只见 `Canvas` / `Paragraph`。
这不是为了换后端，是为了限制 Skia API 面的爆炸（Skia 有数千个符号，本项目用不到 2%）。

#### 5.3.1 渲染后端选择规则

Skia 提供 Ganesh（GL / GLES / Vulkan / Metal / D3D）、Graphite（较新）与 CPU raster。
选择按平台确定，不做运行时探测式切换：

| 平台 | 首选 | 引入阶段 | 说明 |
|------|------|---------|------|
| Linux 桌面 / 嵌入式 | GL / GLES（Ganesh） | P1 | SDL3 统一处理上下文创建 |
| Windows | GL（Ganesh） | P7 | D3D12 仅在实测有收益时评估 |
| Android | GLES（Ganesh） | P8 | Vulkan 待 GLES 出现瓶颈后评估 |
| **macOS / iOS** | **Metal（Ganesh）** | P7 / P8 | **必需**：Apple 已弃用 OpenGL，GL 路径是死路 |
| 全平台（测试） | **CPU raster** | **P0** | 见 §5.3.2 |

**回退链**：GPU 上下文创建失败 → CPU raster + 位图上屏。
不静默回退，需输出明确日志（§5.17.5），否则用户只会感到"莫名很慢"。

**新增后端的准入规则**（与 §5.14.3 同构，三条须同时满足）：

1. 首选后端在该平台**实测存在瓶颈**，有基准数据支撑
2. 新后端的上下文创建、交换链、设备丢失恢复（§5.17.3）三条路径均已实现
3. 黄金图像测试在该后端上通过

每个后端都是一整套上下文与交换链代码，成本远高于"打开一个开关"。

#### 5.3.2 CPU raster 是测试基础设施，不是降级方案

**GPU 渲染结果因驱动而异。** 同一份绘制命令在 Intel 核显、NVIDIA、Mali 上的
抗锯齿边缘会相差若干灰度级，直接用于像素比对会持续产生假失败。

因此：

- **黄金图像测试（§7）一律跑在 CPU raster 上**，结果确定可复现
- CI 容器无需 GPU，跨平台基准可共用同一套期望图像
- GPU 路径另行验证：只比对布局与绘制命令序列（`dump_layout_tree`），不比对像素

CPU raster 因此是必须长期维护的一等路径，不允许在 GPU 后端引入时退化。

#### 5.3.3 Skia 能力准入规则

Skia 的 API 面远超本项目所需。新增暴露的能力须同时满足：

1. **不可组合**：无法由已封装的操作组合表达
2. **后端一致**：在 CPU raster 与 GPU 后端上行为一致，否则黄金测试失去意义
3. **不泄漏 Skia 类型**：能被封装为本项目自有类型
4. **有实际消费方**：至少一个内置控件或主题特性需要它

明确拒绝的 Skia 能力：

| 能力 | 理由 |
|------|------|
| `SkRuntimeEffect`（SkSL 着色器） | CPU 后端支持受限、着色器编译成本、若允许主题携带即成为攻击面（§5.7.5） |
| 完整 `SkBlendMode` 集合 | 仅开放 `SrcOver` / `Multiply` / `Screen` / `Overlay` 等常用项；全集合会使黄金基准组合爆炸 |
| PDF / SVG **导出** | 与 GUI 无关（SVG **输入**作为图像源仍支持，§5.10.1） |
| 完整 `SkImageFilter` 链 | 仅开放背景模糊一项，且标注为高成本；任意滤镜链在 CPU 上不可接受 |
| `SkPicture` 跨进程序列化 | 无对应需求 |

`SkPathEffect`（虚线等）成本低且行为一致，予以支持。

#### 5.3.4 渲染质量参数固定

抗锯齿、图像采样方式、渐变抖动、MSAA 等**固定为单一配置，不对外暴露**。

理由与 §5.3.2 同源：每暴露一个质量开关，黄金图像基准就需要为其组合各建一套。
质量参数属于框架的行为定义，而非应用的可调项。

当前取值：解析抗锯齿常开、不启用 MSAA（Skia 的解析 AA 已足够且省显存）、
图像缩放采用 Mitchell 三次采样、渐变默认抖动以抑制条带。

### 5.4 RenderObject 与布局层

Flutter 模型的核心，也是本项目"轻量"的实现方式。

**固定位置与 flex 不是两套系统**，而是同一个约束协议下的两个 RenderObject
（`RenderStack` / `RenderFlex`），差异全部落在 `parentData` 上。

```cpp
RenderObject
  ├─ layout(BoxConstraints, bool parent_uses_size) -> Size
  ├─ paint(PaintContext&, Offset)
  ├─ hit_test(HitTestResult&, Offset) -> bool
  ├─ mark_needs_layout() / mark_needs_paint()
  ├─ is_repaint_boundary()
  ├─ parent_data                      // 由父节点定义与消费
  └─ [opt] intrinsic_width/height(), distance_to_baseline()
```

**内置 RenderObject（约 13 个）**：
`RenderBox`（padding/border/背景/装饰/圆角，margin 由父节点施加见 §5.9.4）、
`RenderFlex`、`RenderStack`、`RenderWrap`、
`RenderText`、`RenderImage`、`RenderPath`、`RenderViewport`（滚动）、
`RenderClip`、`RenderTransform`、`RenderOpacity`、`RenderCustomPaint`、`RenderView`（窗口根）

#### 5.4.1 布局协议与不变式

`BoxConstraints{min_w, max_w, min_h, max_h}` 向下传递，`Size` 向上返回。
父节点在收到子节点 size 后，把子节点的偏移写进 `parentData`。

三条不变式是单遍布局成立的前提，**违反任何一条都会退化为多趟布局**：

| # | 不变式 | 后果 |
|---|--------|------|
| L1 | 节点的 size 只依赖传入 constraints 与自身属性，**不依赖自己的位置** | 保证 relayout boundary 可成立 |
| L2 | 子节点不感知自己的 offset，offset 由父节点单方面决定 | 保证父子解耦 |
| L3 | 一趟布局中每个节点 `layout()` **恰好一次**，无循环依赖 | 保证 O(n) |

L3 的唯一例外是内在尺寸查询（§5.4.6），因此它必须缓存并被视为显式成本。

**脏标记传播**：`mark_needs_layout` 沿 relayout boundary 向上冒泡到最近的
"约束不依赖子节点尺寸"的祖先（即以 `parent_uses_size == false` 布局子节点的节点），
避免整树重排。

#### 5.4.2 固定位置：RenderStack

绝对定位不是独立机制，而是 `RenderStack` 的 `StackParentData`：

```cpp
struct StackParentData {
  std::optional<float> left, top, right, bottom;   // 相对 Stack 内容框的边距
  std::optional<float> width, height;              // 显式尺寸
};
```

**判定规则**：任一边距或显式尺寸被设置 → 该子节点为 *positioned*，脱离常规流；
全部未设置 → *non-positioned*，参与 Stack 自身定尺寸。

**Stack 的布局顺序**（两小步，每个子节点仍只 layout 一次，L3 成立）：

1. 用 loose 约束布局全部 non-positioned 子节点，取其并集 + Stack 自身约束确定 Stack 尺寸
2. 用第 1 步得到的 Stack 尺寸推导每个 positioned 子节点的约束并布局

主轴尺寸推导（水平方向，垂直同理）：

| 已设置 | 子节点宽度 | 子节点 x |
|--------|-----------|---------|
| `left` + `width` | `width`（tight） | `left` |
| `left` + `right` | `stackW - left - right`（tight，**拉伸**） | `left` |
| `right` + `width` | `width`（tight） | `stackW - right - width` |
| 只有 `left` | 子节点自然宽度（loose） | `left` |
| 都没有 | loose | 按 `alignment` 对齐 |

这一张表覆盖了 CSS `position: absolute` 的全部常用场景：固定坐标、贴边、双边拉伸、锚定。

> 不提供 `position: fixed`（视口固定）。等价效果通过在 `RenderViewport` **外层**
> 套一个 Stack 实现 —— 位置关系在树结构里显式表达，而不是靠一个属性偷偷改变参照系。

#### 5.4.3 Flex 布局：RenderFlex

```cpp
struct FlexParentData {
  float    grow   = 0;      // 剩余空间分配权重
  float    shrink = 0;      // 超出时的收缩权重
  std::optional<float> basis;  // 主轴基准尺寸，缺省用子节点自然尺寸
  Align    align_self = Align::kAuto;
};
```

容器属性：`direction`（row/column/row-reverse/column-reverse）、`justify`
（start/end/center/space-between/space-around/space-evenly）、`align`
（start/end/center/stretch/baseline）、`gap`、`main_size`（min/max）。

**布局算法**：

1. 对 `grow == 0 && shrink == 0` 的子节点，以**主轴无界**约束布局，记录自然尺寸
2. `free = 可用主轴空间 - Σ已用 - Σgap`
3. `free > 0` 且存在 `grow > 0` → 按 grow 权重分配，以 **tight** 主轴约束重新布局这些子节点
4. `free < 0` 且存在 `shrink > 0` → 按 `shrink × basis` 加权收缩（与 CSS 一致）
5. 交叉轴：`stretch` 给 tight 约束，其余给 loose 后按 align 定位
6. `align == baseline` 时查询各子节点基线，按最大上升距离对齐

**与 CSS flexbox 的差异（有意为之）**：

| CSS 特性 | 本项目 | 说明 |
|---------|--------|------|
| `flex-grow` / `flex-shrink` / `flex-basis` | 支持 | |
| `justify-content` / `align-items` / `align-self` | 支持 | |
| `gap` | 支持 | |
| `align-items: baseline` | 支持 | 表单标签对齐的刚需 |
| **`flex-wrap`** | **不在 Flex 上支持** | 拆到独立的 `RenderWrap`，见 §5.4.4 |
| `align-content` | 仅 `RenderWrap` 支持 | 只在多行时有意义 |
| `order` | 不支持 | 用数据层排序，不用布局层重排 |

> `flex-wrap` 拆出去的理由：混在 Flex 里会让单行路径（99% 的使用场景）也背上多行
> 分支的复杂度。两个 RenderObject 各自简单，好过一个带模式开关的复杂体。

#### 5.4.4 RenderWrap

主轴排满即折行，`run_spacing` 控制行间距，`align_content` 控制多行整体对齐。
子节点按传入约束顺序测量并断行，**每个子节点仍只 layout 一次**，L3 成立。

不支持 wrap 下的 `grow`（CSS 里这个组合的语义本身就令人困惑）。

#### 5.4.5 尺寸表达

`RenderBox` 的每个维度支持四种来源，优先级从高到低：

| 来源 | 写法 | 语义 |
|------|------|------|
| 固定 | `width: 120` | 逻辑像素，tight |
| 比例 | `width: "50%"` | 相对传入 `max_w`；无界约束下报错 |
| 弹性 | `flex.grow > 0` | 由父 Flex 分配 |
| 内容 | 缺省 | 由子节点尺寸决定（shrink-to-fit） |

另有 `min_width` / `max_width` / `min_height` / `max_height` 作为夹取，
在上述四种来源计算出结果后统一应用。

`aspect_ratio` 作为 `RenderBox` 的可选属性：一个维度确定后推导另一个维度。

#### 5.4.6 内在尺寸与性能悬崖

`intrinsic_width(height)` / `intrinsic_height(width)` 是**推测性布局**，
会打破 L3，最坏情况 O(n²)。

**明确的政策**：

- 常规 Flex / Stack / Wrap 布局路径**完全不需要内在尺寸**
- 仅三处需要：`Table` 列自适应、`baseline` 对齐、显式的 `IntrinsicWidth` 包装
- 每个节点缓存 `(输入约束 → 内在尺寸)` 的最近一次结果，`mark_needs_layout` 时失效
- 调试构建下统计单帧内在查询次数，超阈值输出警告

> Flutter 在文档里明确警告内在尺寸的代价，但没有工具让开发者感知它。
> 本项目在调试构建里主动报警，把隐性成本显性化。

#### 5.4.7 无界约束与错误诊断

垂直 `RenderViewport` 给子节点的 `max_h` 是无穷大。此时：

- `grow > 0` 的子节点**无法分配无限空间** → 这是**布局错误**，不是未定义行为
- `height: "50%"` 同理无意义 → 报错

必须输出可定位的诊断而非静默产出垃圾布局：

```
[drawgui] layout error: RenderFlex(direction=column) 的子节点 #2 设置了 grow=1，
          但传入的 max_h 无界（祖先 RenderViewport 允许无限滚动）。
          节点路径: RenderView > Box#root > Viewport > Column > Box#item2
          修复: 移除 grow，或给该 Column 一个确定高度。
```

> stonegui 复盘反复出现的模式是"静默失败最难查"。布局是最容易静默出错的子系统，
> 因此**所有约束冲突一律显式报错并给出节点路径**。

#### 5.4.8 Layer 与重绘边界

`RenderViewport` 与显式标记的节点是 repaint boundary，各自持有一个 `SkPicture` / GPU 层。
滚动时只重新合成，不重新绘制内容。`RenderOpacity` / `RenderTransform` 在动画期间
自动提升为重绘边界，动画结束后降级。

#### 5.4.9 单位与 DPI

**布局全程使用逻辑像素（dp）**，DPI 缩放只在 `RenderView` 施加到 canvas 变换上。
多显示器下窗口跨屏移动只需更新 `RenderView` 的缩放并重绘，**不触发重新布局**。

#### 5.4.10 布局调试

`dg_dump_layout_tree(node) -> JSON`：输出每个节点的类型、传入约束、结果尺寸、
偏移、parentData、以及是否为重绘/重排边界。这是自绘 GUI 里等价于 DOM Inspector
的东西，成本极低而收益极高，**在 P2 就要有**。

#### 5.4.11 决策记录：自研布局，而非引入 Yoga / Taffy

**候选评估**：

| | Yoga | Taffy |
|---|---|---|
| 语言 / 构建 | C++20 + CMake（与本项目基线一致，vcpkg 有 port） | Rust |
| 协议 / 活跃度 | MIT，18.9k star，活跃（仓库已迁至 `react/yoga`） | MIT，3.6k star，活跃 |
| 布局能力 | Flexbox | Block + Flexbox + **CSS Grid** |
| C 绑定 | 原生 C++ | **WIP（PR #404）** |
| 生产用户 | React Native | Servo、Slint、Bevy、Zed(GPUI)、Floem |
| 文本布局 | **不提供** | **不提供** |

Taffy 因 C 绑定未完成且需引入 Rust 工具链，暂不考虑。核心问题是是否采用 Yoga。

**结论：自研。** 集成难度不是障碍，**架构阻抗不匹配**才是：

| 维度 | Yoga | 本项目 |
|------|------|--------|
| 模型 | 样式 → 计算矩形 | 约束下行 / 尺寸上行 |
| 树 | 自有 `YGNode` 树 | RenderObject 树 |
| 脏标记 | `YGNodeMarkDirty`，向上标脏至根 | relayout boundary 精细化 |
| 无界约束 | 单一 `YGUndefined` | 显式 `BoxConstraints` + 语义诊断 |

四条具体后果：

1. **两棵树需持续同步** —— 每个 RenderObject 配一个 YGNode。这正是 React Native 的
   做法，是公认的 bug 与开销来源。
2. **L1–L3 不变式被架空** —— relayout boundary（§5.4.1）是本项目的核心性能机制，
   Yoga 自带一套不同的脏标记策略，两者无法叠加。采用 Yoga 等于 §5.4 整节需重写。
3. **§5.4.7 的诊断无法实现** —— Yoga 只有 `YGUndefined`，不携带"祖先是 Viewport
   所以主轴无界"这一语义，那条可定位到节点路径的错误信息就无从产生。
4. **文本布局省不掉** —— Yoga 与 Taffy 均不做文本布局，仍需 measure 回调进 SkParagraph。

**自研的代价可控**：§5.4.3 已主动砍掉 wrap（拆给 `RenderWrap`）、`order`、
`align-content`、`float`、`grid`。剩余子集的算法规模估计：
`RenderFlex` ≈ 400 行、`RenderWrap` ≈ 250 行、`RenderStack` ≈ 150 行、
`BoxConstraints` 及工具 ≈ 150 行，**合计 < 1000 行**。

**但要借用 Yoga 的测试资产**：Yoga 的 `gentest` 把测试夹具写成 HTML，
**在 Chrome 中渲染生成期望布局结果** —— 这是 flexbox 正确性的黄金标准。
本项目照此编写等价 fixture，用 Chrome 产出基准矩形作为布局快照测试的期望值，
从而在不引入依赖的前提下获得正确性信心。

**重新评估的触发条件**：若确定要实现 CSS Grid（§12），grid 规范复杂度远高于 flexbox，
自研不划算，届时应重新评估 Taffy（视其 C 绑定成熟度）。

### 5.5 Input / Focus 层（C2）

**必须原生实现的桌面语义**（stonegui 全都是靠拦截补出来的）：

- 指针命中链 + `hover` / `press` / `active` 状态的语义区分
- 焦点树 + 显式 `tab_index`（不是"控件创建顺序线性入组"）
- 键盘路由：修饰键组合（Ctrl+A/C/V/X、Home/End、Shift+方向选区）
- 滚轮：滚**光标下的**可滚动祖先，不是滚焦点控件
- 右键上下文菜单（走 `PopupHost`）
- 拖放：应用内 + OS 级（文件拖入）
- 文本选区：跨行选择、双击选词、三击选段
- 指针捕获：按下后即使移出控件/窗口仍持续收 move/up

**IME（低优先级）**：`IWindow::start_text_input(rect)` 接口签名 MVP 就定下来，
SDL3 实现留待 P7。MVP 的 `TextField` 只保证 ASCII 直接输入与已确认文本的正确显示。

> 降级理由：IME 是**验证性风险**而非**架构性风险** —— 接口位置已经留好，
> 后续填充实现不影响任何其他子系统。而多窗口是架构性的，所以它优先。

#### 5.5.1 快捷键绑定到「意图」，不绑定到按键

这是本层最重要的一条决策。**应用代码不得直接书写 `Ctrl+C`**，而是声明意图
（`Intent::Copy`），由平台绑定表映射到实际按键。

原因是平台差异远超"Ctrl 换成 Cmd"：

| 意图 | Windows / Linux | macOS |
|------|----------------|-------|
| 复制 | `Ctrl+C` | `⌘C` |
| 行首 / 行尾 | `Home` / `End` | `⌘←` / `⌘→`，**且 `Ctrl+A` / `Ctrl+E` 同样是行首/行尾** |
| 删除前一个词 | `Ctrl+Backspace` | `⌥Delete` |

注意 macOS 的 `Ctrl+A` 是系统级 emacs 绑定的**行首**。若应用把 `Ctrl+A` 硬编码为
"全选"，在 macOS 上就是错的 —— 这类错误无法靠"多测试"发现，只能靠架构避免。

**绑定表是单一真相源**：菜单项显示的加速键文本（`⌘C` / `Ctrl+C`）由绑定表生成，
不手写。构建期一致性检查同时检测同一作用域内的重复绑定。

`Mod` 作为伪修饰键表示"平台主修饰键"（macOS 为 `⌘`，其余为 `Ctrl`），
绑定表中大部分条目可写成 `Mod+C` 一行covering 全平台。

**键位匹配**默认使用布局相关的逻辑键（用户按下标着 C 的键即触发 `Mod+C`，
符合直觉），绑定表条目可显式改用物理扫描码，供游戏式 WASD 等场景使用。

#### 5.5.2 路由顺序

键盘事件按固定顺序解析，任一级消费即终止：

```
1. IME 组字中？ → 全部事件交给 IME，不进入下列任何一级
2. 焦点节点起，沿祖先链向上冒泡          （组件级：TextField 内的全选）
3. 窗口级绑定表                          （窗口菜单、对话框默认按钮）
4. App 级绑定表                          （全局菜单加速键）
```

**第 2 级必须先于第 3、4 级**：`TextField` 获得焦点时，其"全选文本"必须压过
应用菜单的"全选"。冒泡优先天然给出这个结果，无需特例。

**第 1 级是硬性隔离**：IME 组字期间方向键、`Enter`、`Esc` 全部属于候选框，
不得泄漏到快捷键路由。这是 stonegui 复盘 2.2 中"键盘事件被多方争抢"的根因之一。

**文本编辑按键不是快捷键**，而是 `TextField` 注册的编辑意图
（`MoveCaretLineStart`、`DeleteWordBackward` …），走同一套机制但只在文本控件上下文生效。
macOS 的 `Ctrl+A` = 行首因此可以只绑定在文本控件作用域内，不污染全局。

#### 5.5.3 ABI 表达

意图是生成的数值 `action_id`（与 `prop_id`、`token_id` 同源同构）。
解析在 C++ 内完成，结果以事件形式进入 §5.8 的轮询队列，不使用跨线程回调：

```c
int dg_app_bind_shortcut (dg_app_t*,    const dg_shortcut*, uint16_t action_id);
int dg_node_scope_action (dg_node_t*,   uint16_t action_id);   // 声明该节点消费此意图
// 触发后产生 dg_event{ type=DG_EVENT_ACTION, action_id, target_node, window }
const char* dg_shortcut_label(uint16_t action_id);             // 菜单显示用，平台相关
```

### 5.6 Widget 层

控件由 RenderObject 组合而成，内置于 C++ 核心。

MVP 八个：`Box` `Text` `Button` `TextField` `ScrollView` `List` `Image` `Row/Column`

`List` 与 `Table` **内置虚拟化**（§5.15.4）—— 这是控件定义的一部分，不是可选优化。

完整目标集（约 30 个）：加上 `Checkbox` `Radio` `Switch` `Slider` `Dropdown` `Menu`
`Tabs` `Table` `Tree` `Dialog` `Tooltip` `ProgressBar` `Spinner` `Splitter` `DatePicker` 等。

**验收标准**：如果实现 `Slider` 需要新增 RenderObject，说明第 3 层的原语集设计有缺陷。
MVP 阶段的八个控件就是用来验证这条的。

### 5.7 Theme 层

继承 stonegui 的 token 模型（复盘第 7 节列为可迁移资产），并扩展为
**编译期 schema + 运行时 theme package** 的两段式结构。

#### 5.7.1 核心区分：schema 是契约，取值是数据

| | 性质 | 何时确定 | 载体 |
|---|------|---------|------|
| **token schema**<br>（有哪些 token：`color.surface`、`radius.md`） | **API 契约** | 编译期 | `themes/schema.toml` |
| **token 取值**<br>（`color.surface` 在 light/dark 下等于什么） | **数据** | 运行时 | `theme.json` + 资源目录 |

schema 必须留在编译期，因为三件事从它派生：ABI 的数值 `token_id`、`.d.ts` 的类型提示、
以及"拼错 token 名要报错而非静默回退"的校验能力。**取值则完全可运行时加载**，
不需要重新编译即可换肤，第三方也能分发主题。

#### 5.7.2 token 模型

- **二分法**：语义色阶 token + 整数 token（尺寸/间距/圆角/字号）
- `$token` **活引用** —— 主题切换时自动生效，无需重建控件树
- 伪状态（hover/active/disabled/focus）与 part 的**一层嵌套上限**
- 变体（variant）：`light` / `dark` / 可扩展（如 `high-contrast`）

#### 5.7.3 为什么不做 GTK CSS 式主题

GTK3 的 CSS 主题出名地脆弱，根因是**选择器能伸进控件内部结构**（node 名、sub-node），
主题因此与控件内部树耦合 —— 每次控件重构就破坏一批主题。

GTK4 时代 libadwaita 的应对是**主动收窄**：不再允许任意 CSS，改为一组固定的具名颜色
+ style class。这与本项目"伪状态与 part 一层嵌套上限"是同一个结论。

> **抄 libadwaita 的收敛结果（固定 token 表 + 变体），不抄 GTK3 CSS 的开放度（任意选择器）。**

#### 5.7.4 Theme package 结构

```
mytheme/
├── theme.json      元数据 + token 取值 + light/dark 变体
├── fonts/          随主题分发的字体
├── icons/          SVG（Skia 自带 SVG 模块）
└── images/         9-patch / 纹理
```

```jsonc
{
  "schema_version": 1,
  "name": "mytheme",
  "fonts": [{ "family": "Inter", "src": "fonts/Inter.ttf" }],
  "base": { "radius.md": 8, "space.2": 8, "font.size.body": 14 },
  "variants": {
    "light": { "color.surface": "#FFFFFF", "color.on-surface": "#1A1A1A" },
    "dark":  { "color.surface": "#1E1E1E", "color.on-surface": "#E8E8E8" }
  }
}
```

**格式分工的理由**：schema 是构建期契约，需要逐 token 写注释和文档，用 TOML 且只有
Python 生成器读；theme package 是运行时数据、第三方作者要手写、JS 侧也要读，用 JSON，
C++ 侧只引入一个 JSON 解析器。

**内置主题同样是 JSON**，以字符串嵌入二进制。这样加载路径只有一条、天然 dogfooding，
代价仅是启动时解析几 KB。CI 在构建期校验内置主题，保证发布版不会因主题解析失败而起不来。

**打包**：MVP 只支持目录形式（配合热重载）；单文件归档留到 P7，加载接口不变。

#### 5.7.5 资源与安全

第三方主题是**不可信输入**，加载器必须满足：

- **路径穿越防护**：所有资源路径按 theme 根规范化后必须仍在根内，`../` 逃逸一律拒绝
- **解析限制**：JSON 嵌套深度上限、文件大小上限（防栈溢出与解压炸弹）
- **SVG 收敛**：禁用外部引用、限制画布尺寸与路径节点数（Skia SVG 模块是攻击面）
- **未知 token 名 → 加载失败并报出具体位置**，绝不静默忽略
  （静默失败正是 stonegui token 四文件漂移的同类问题）
- **类型校验**：schema 声明每个 token 的类型（color / int / float / string），取值不匹配即报错

**图标缓存**：SVG 每次解析代价高，加载后缓存为 `SkPicture`，并按 DPI 分档栅格化到 atlas。

#### 5.7.6 热重载

`IPlatform::watch_files`（原本为 JSX 热重载设计）直接复用来监听 theme 目录。
配合 `$token` 活引用，改 `theme.json` 即时生效且不重建控件树。

#### 5.7.7 一致性检查（C4）

schema 是**单一真相源**。stonegui 的教训是 token 在 `sg_theme.h` / `sg_theme.c` /
`theme.md` / `framework.d.ts` 四处静默漂移。本项目从一份 `themes/schema.toml` **生成**：
C++ token_id 常量、ABI 常量表、`.d.ts` 类型、文档、以及**加载器用的字符串→id 映射表**。
`tools/check_consistency.py` 在 CI 中验证内置主题覆盖了 schema 的全部 token。

### 5.8 ABI 层

**设计原则：只导出必要面。** RenderObject 层、Layer 树、Skia 原始对象一律不导出。

```c
// 版本
uint32_t dg_abi_version(void);          // MAJOR 破坏 / MINOR 追加

// 生命周期
dg_app_t*    dg_app_create(const dg_app_opts*);
dg_window_t* dg_window_create(dg_app_t*, const dg_window_opts*);
void         dg_window_set_root(dg_window_t*, dg_node_t*);

// 节点：数值 type_id / prop_id，非字符串
dg_node_t* dg_node_create(dg_app_t*, uint16_t type_id);
int        dg_node_set_prop(dg_node_t*, uint16_t prop_id, const dg_value*);
int        dg_node_insert_before(dg_node_t* parent, dg_node_t* child, dg_node_t* ref);
int        dg_node_remove(dg_node_t*);

// 事件：双模式
int dg_poll_events(dg_app_t*, dg_event* out, int max);    // Node FFI 用
int dg_wait_events(dg_app_t*, int timeout_ms);            // 避免空转
int dg_set_event_callback(dg_app_t*, dg_event_cb, void*); // QuickJS / 原生用

// 主题：运行时加载，token_id 为生成的数值常量
dg_theme_t* dg_theme_load_dir(dg_app_t*, const char* dir, dg_theme_err* err);
dg_theme_t* dg_theme_load_memory(dg_app_t*, const char* json, size_t len,
                                 const char* base_dir, dg_theme_err* err);
int         dg_theme_set_variant(dg_theme_t*, const char* variant);
int         dg_theme_override(dg_theme_t*, uint16_t token_id, const dg_value*);
int         dg_app_set_theme(dg_app_t*, dg_theme_t*);

// 逃生口
int dg_custom_paint_set_cmds(dg_node_t*, const dg_paint_cmd*, int n);

// 调试：返回布局树 JSON（类型/约束/尺寸/偏移/parentData/边界标记）
const char* dg_dump_layout_tree(dg_node_t*);

// 错误
const char* dg_last_error(void);
```

**关键决策**：

1. **属性用数值 `prop_id` + tagged union，不用字符串。** 每帧可能有上千次属性写入，
   字符串哈希是纯浪费。`prop_id` 由代码生成器从单一定义产出，同时生成 `.d.ts` 的
   字符串→数值映射表。

2. **事件双模式。** Node/Bun 的 FFI 无法安全接收来自非 JS 线程的回调，
   所以必须提供 `dg_poll_events` 出队模式。QuickJS 内嵌时可用回调模式。

3. **不导出 RenderObject 层**，但提供 `dg_custom_paint` 逃生口 —— 一组精简的
   绘制命令（矩形/圆角矩形/路径/文本/图片/渐变），够画自定义控件，不暴露 Skia。

4. **ABI 稳定性规则**：结构体只在末尾追加字段且带 `size` 字段；枚举值永不复用；
   函数只增不改签名。MAJOR 版本号变更才允许破坏。

5. **单一真相源（C4）**：`abi/drawgui.def.toml` → 生成 `drawgui.h`、`drawgui.d.ts`、
   QuickJS 绑定 stub、prop_id 常量表。**手写任何一份都会导致漂移。**

6. **主题 token_id 与 prop_id 同源同构**：都是生成的数值常量。theme.json 里的字符串
   token 名在加载时经生成的映射表转成 `token_id`，未命中即报错（§5.7.5）。

7. **parentData 属性是一类特殊 prop**：`flex.grow` / `left` / `top` 等设在**子节点**上，
   却由**父节点**消费。设值时父节点可能尚未挂载，因此校验推迟到布局期：
   若父节点类型不消费该 prop（如在 Stack 的子节点上设 `flex.grow`），
   输出带节点路径的诊断警告，而非静默忽略。

### 5.9 属性系统：受控的 CSS 子集

参考 React Native 的取舍——**只支持必要属性，不追求 CSS 完整性**。
但 RN 自身的历史证明"最小子集"会蠕变（0.71 之后属性持续加回、Yoga 逐步向规范靠拢），
因此本项目定义的不是一份快照清单，而是**准入规则**。

#### 5.9.1 拒绝的是复杂度轴，不是属性数量

属性数量只是症状。CSS 的实现成本来自以下complexity轴，本项目逐条表态：

| CSS 复杂度轴 | 决定 | 替代方案 |
|-------------|------|---------|
| 选择器 / 级联 / 特异性 | **拒绝** | token 表赋值（§5.7.3） |
| 隐式继承 | **拒绝** | 显式 `TextStyleScope` 节点 + 主题 token 默认值 |
| 单位系统（em/rem/vh/vw/ch/`calc()`） | **拒绝** | 只有逻辑 px 与 `%`（§5.4.5） |
| 简写属性（`margin: 1px 2px 3px`） | **拒绝** | 只有长写形式 |
| 布局模式（block/inline/float/table/grid） | **拒绝** | 只有 flex / stack / wrap（§5.4） |
| margin 折叠 | **拒绝** | margin 恒为实际间距 |
| `box-sizing` 开关 | **拒绝** | 恒定 **border-box**：`width` 含 padding 与 border |
| 全局层叠上下文 / `z-index` | **简化** | 绘制序 = 树序，Stack 内可显式 `z` |
| CSS 动画 / 过渡语法 | **拒绝** | 命令式动画 API |

> 有了这张表，将来"能不能加个 `calc()`"之类的提议不需要重新辩论。

#### 5.9.2 准入规则

新属性必须同时通过四条：

1. **不可组合**：无法由已有属性组合表达
2. **不引入新复杂度轴**：不触碰 §5.9.1 表中任何一行
3. **至少两个内置控件需要它**：单一控件的需求用该控件的专属 prop 解决
4. **类型单一**：能在 `drawgui.def.toml` 中表达为确定类型，不是"字符串里再解析一层语法"

**为什么要严**：prop 是生成的数值 `prop_id`，属于 ABI 契约。
新增是 MINOR（廉价），删除是 MAJOR（昂贵）。**加错了删不掉**，所以宁可少加。

#### 5.9.3 命名规则

**名字与 CSS 相同 ⇒ 行为必须与 CSS 相同；行为有差异则必须换名字。**

这是 RN 的教训：它保留了 `overflow`、`zIndex` 等 CSS 名称但语义存在偏差，
结果比直接换名字更容易误导。本项目宁可用 `main_size`、`hit_test_behavior`
这类非 CSS 名称，也不复用一个语义已被削减的 CSS 名。

#### 5.9.4 盒模型语义

**恒定 border-box**，四层结构：

```
   margin        ← 外间距，由父节点施加，不计入本节点 size
 ┌──────────────────────────┐
 │ border                   │  ← width / height 的边界
 │  ┌────────────────────┐  │
 │  │ padding            │  │
 │  │   ┌────────────┐   │  │
 │  │   │  content   │   │  │
 │  │   └────────────┘   │  │
 │  └────────────────────┘  │
 └──────────────────────────┘
```

- `width` / `height` = border + padding + content，**不含 margin**
- 背景与圆角裁剪的范围 = border-box（含 padding，不含 margin）
- `padding` 由节点自身处理：布局时按 padding 收缩传给子节点的约束

**margin 由父节点施加**。在约束模型中 margin 不能由节点自己处理，否则 `width`
的语义会被污染（背景是否绘制到 margin 区将无法确定）。因此：

1. 父节点把子节点约束按 `margin_l + margin_r` / `margin_t + margin_b` 收缩
2. 子节点返回 size
3. 父节点按 `margin_l` / `margin_t` 偏移放置

与 §5.8 决策 7 的 `flex.grow` / `left` 属同一类，但有一处关键区别：
`flex.grow` 是**特定容器**定义的 parentData，**margin 是所有容器都必须处理的通用外间距**。
因此它位于 `ParentData` 基类，布局协议提供 `layout_child_with_margin()`
供所有容器复用——这条统一入口是保证各容器行为一致的手段。

补充规则：

- **无 margin 折叠**（§5.9.1）：相邻 margin 相加，不取最大值
- Flex 中 `gap` 与 margin **叠加**：相邻子节点实际间距 = `gap + 前者 margin_r + 后者 margin_l`
- Stack 的 positioned 子节点：margin 与 `left/top/right/bottom` **叠加**
- 允许负 margin（实现上即负偏移），不做溢出特殊处理

#### 5.9.5 背景与装饰

`RenderBox` 支持的装饰层，绘制顺序自下而上：

| 层 | 属性 | 说明 |
|---|------|------|
| 外阴影 | `shadow` | offset / blur / spread / color |
| 背景色 | `background_color` | |
| 背景渐变 | `background_gradient` | linear / radial / sweep，多色标 |
| 背景图 | `background_image` | `fit`: fill / contain / cover / tile / nine-patch |
| 边框 | `border_width_{l,t,r,b}` `border_color` | 四边可独立设宽 |

全部受 `border_radius_{tl,tr,br,bl}` 裁剪。

**只有 `RenderBox` 有背景**。`Text` / `Image` 等叶子节点无背景属性——
需要时外套一层 Box。这符合 §5.9.2 准入规则第 1 条（可组合的不新增）。

**不支持多重背景**（CSS 允许背景分层叠加）：一层足够，需要叠加时用嵌套 Box。

**复杂值的 ABI 传递**：渐变、阴影、背景图不是标量，无法塞进 `dg_value` 标量联合。
为它们提供专用 setter，而非在字符串里再解析一层语法（§5.9.2 准入规则第 4 条）：

```c
int dg_node_set_gradient(dg_node_t*, uint16_t prop_id, const dg_gradient_desc*);
int dg_node_set_shadow  (dg_node_t*, uint16_t prop_id, const dg_shadow_desc*);
int dg_node_set_image   (dg_node_t*, uint16_t prop_id, const dg_image_desc*);
```

#### 5.9.6 属性集（P3 基线）

**布局**（详见 §5.4）：容器 `direction` `justify` `align` `gap` `main_size`；
Wrap 额外有 `run_gap` `align_content`；子节点 parentData
`grow` `shrink` `basis` `align_self` `left` `top` `right` `bottom`

**盒模型**：`width` `height` `min_width` `max_width` `min_height` `max_height`
`aspect_ratio` `padding_{l,t,r,b}` `margin_{l,t,r,b}`

**视觉**：`background_color` `background_gradient` `background_image`
`border_width_{l,t,r,b}` `border_color` `border_radius_{tl,tr,br,bl}`
`opacity` `shadow` `overflow` `transform`（分解为 translate/scale/rotate + origin，
便于动画插值）`cursor`

**文本**：`font_family` `font_size` `font_weight` `font_style` `line_height`
`letter_spacing` `color` `text_align` `text_decoration` `max_lines` `ellipsis`
`text_direction`

**交互**（非 CSS 但必需）：`disabled` `focusable` `tab_index` `hit_test_behavior`

#### 5.9.7 显式拒绝的属性及其替代

| 属性 | 替代 |
|------|------|
| `display: none` | 框架层 `<Show>` 条件挂载——避免"已布局但不可见"的歧义状态 |
| `position: fixed` / `sticky` | 树结构表达（§5.4.2） |
| `visibility: hidden` | 与 `opacity: 0` 语义重叠，只保留后者 |
| `float` / `clear` | 无对应需求 |
| `!important` | 无级联，不需要 |
| `content` / 伪元素 | 用真实节点 |
| 多重背景 | 嵌套 Box |

#### 5.9.8 相对 React Native 的一处结构性优势

RN 的 `shadow*` 仅 iOS 生效、`elevation` 仅 Android 生效，因为它桥接原生控件，
样式语义受各平台控件能力约束——这是 RN 长期存在的碎片化来源。

**本项目全自绘，每个属性只有一套实现，跨平台行为按定义一致。**
黄金图像测试（§7）在多平台 CI 上比对同一批基准，可机械地保证这一点。

### 5.10 图像、矢量与 Emoji

#### 5.10.1 统一抽象：ImageSource

三类来源共用一套加载、缓存与绘制路径，被 `RenderImage` 与 `background_image` 复用：

| 类型 | 来源 | 缓存形态 |
|------|------|---------|
| 位图 | PNG / JPEG / GIF / BMP / ICO / WBMP（+ WebP，见 §5.10.2） | `SkImage` + GPU 纹理 |
| 矢量 | SVG（`SkSVGDOM`） | `SkPicture`，按 DPI 分档栅格化 |
| 九宫格 | nine-patch 元数据 + 位图 | 同位图，附切片信息 |

**SVG 是图像源，不是一等节点类型。** 不提供 `RenderSvg`，理由：一等 SVG 节点会招来
"能否动画 SVG 属性 / 操作 SVG 子树"的需求，而 Skia 的 SVG 模块本就是**子集渲染器**
（无动画、无脚本、滤镜有限），承诺不了。作为图像源则边界清晰。

> 这同时确定了图标方案：**SVG 而非 icon font**。icon font 失去多色能力，
> 且会混入文本字体回退链、干扰 §5.10.4 的 emoji 回退。

#### 5.10.2 编解码能力与依赖

预编译 Skia 的默认 codec 集合：**解码** BMP / GIF / ICO / JPEG / PNG / WBMP，
**编码** JPEG / PNG。

- **WebP 不在默认集合中**，必须显式启用 `webp` feature —— 本项目需要，已计入 §3.2 的 feature 组合
- **AVIF / HEIF 不支持**，不在目标范围内
- **动图**：GIF / WebP 动画经 `SkAnimatedImage` 支持
- **Lottie**：Skia 的 `skottie` 模块可用，但列为**未来可选**，不进 P0–P8

#### 5.10.3 异步解码与布局稳定性

解码大图会阻塞 UI 线程掉帧，因此**解码在线程池中进行**，主线程只提交请求并接收结果。

由此产生一条**强制的布局约束**：

> `RenderImage` 必须在**内容尚未解码完成时**就能确定自身尺寸 ——
> 通过显式 `width`/`height`、`aspect_ratio`、或完全由父约束决定（如 `grow`）。
> 三者皆无时输出诊断错误，而非等解码完再布局。

理由：若允许尺寸由图片内容决定，则每张图加载完成都会触发一次重排，
表现为可见的布局抖动。这与 §5.4.7 "约束冲突显式报错"的处置方式一致。

其余机制：

- **DPI 变体**：`icon.png` / `icon@2x.png` / `icon@3x.png`，按 `IWindow::dpi_scale()` 选取
- **GPU 纹理缓存**有显式预算与 LRU 淘汰；窗口最小化 / 移动端 `low_memory` 时主动收缩
- **解码占位**：未就绪时绘制主题 token 指定的占位色，不留空洞

#### 5.10.4 Emoji

渲染本身由 Skia 承担 —— 四种彩色字体格式并存，Skia 均支持：

| 格式 | 类型 | 典型字体 / 平台 |
|------|------|----------------|
| CBDT / CBLC | 位图 | Noto Color Emoji（Android / Linux） |
| sbix | 位图 | Apple Color Emoji（macOS / iOS） |
| COLR / CPAL（v0 与 v1） | 矢量分层，v1 带渐变 | Segoe UI Emoji（Windows） |
| SVG-in-OpenType | 矢量 | 实际已边缘化 |

**真正的工作在另外两处**：

**（1）字体回退链** —— 文本含 emoji 时主字体无对应字形，须回退到系统 emoji 字体。
由 `FontManager`（§5.3）构建 `FontCollection` 并配置回退顺序，平台来源分别为
Linux fontconfig、Android 系统字体配置、macOS/iOS CoreText、Windows DirectWrite。
CJK 字体自动发现与 emoji 回退是同一套机制的两个消费者。

**（2）grapheme cluster 是文本编辑的基本单位** —— 这是必须写死的语义：

> **光标移动、选区扩展、退格删除的最小单位是 grapheme cluster，
> 既不是字节，也不是 code point。**

`👨‍👩‍👧‍👦` 由 7 个码点经 ZWJ 连接，退格必须整体删除；肤色修饰符
（`👍` + U+1F3FD）、区域指示符对组成的旗帜（`🇨🇳`）同理。
实现依赖 `SkUnicode` 的 grapheme 断句，即 §5.10.5 的 ICU 依赖。

> stonegui 复盘 2.2 记录过"LVGL SDL 键盘驱动逐字节吐出，拆坏 UTF-8"。
> 本项目从设计上跳过该层级问题：**文本操作 API 只接受 grapheme 边界**，
> 内部不存在能产生半个字符的路径。

#### 5.10.5 ICU 数据依赖

`textlayout`（skshaper + skparagraph）依赖 **HarfBuzz + ICU**。
ICU 数据文件 `icudtl.dat` 是 BiDi、断行、grapheme 分割的前提，绕不开。

**分发方式必须显式决定**（Windows 上 Skia 默认要求该文件与可执行文件同目录）：

- 首选：**嵌入二进制**并在首次使用 `textlayout` 前完成初始化，避免运行时文件依赖
- 需评估**裁剪 ICU 数据**（完整 `icudtl.dat` 约 10 MB 量级，本项目仅需
  BiDi / 断行 / grapheme / 大小写映射），裁剪收益直接体现在最终体积上
  - **不可裁掉**：UAX #14 断行规则表（CJK 无空格分词，完全依赖它，见 §5.13.6）

这一项列入 §12 未决，在 P3 文本控件落地前定案。

### 5.11 颜色与透明度

#### 5.11.1 两类透明度，语义不同而非冗余

| | 语义 | 实现 | 成本 |
|---|------|------|------|
| **颜色 alpha**<br>`#RRGGBBAA` | 单次绘制的混合 | 直接进 `SkPaint` 的颜色值 | **免费**，与不透明色同一 draw call |
| **节点 `opacity`** | **整个子树作为一组**先合成，再整体混合 | `SkCanvas::saveLayer()` | 离屏纹理 + 渲染目标切换 |

**可观察差异在子节点重叠时**：`<Box opacity=0.5>` 内两个重叠的红色子节点，
重叠处**不会**加深；而给每个子节点设 `background_color: red 50%`，重叠处**会**双重混合。

两者不可互相替代，因此**都要支持**。但节点 `opacity` 的成本必须显性化，
处理方式与 §5.4.6 的内在尺寸一致。

#### 5.11.2 opacity 的成本控制

绘制期按以下顺序判定，仅最后一档付出 `saveLayer` 代价：

| 条件 | 处理 |
|------|------|
| `opacity == 1` | 完全跳过，不产生任何额外开销 |
| `opacity == 0` | 跳过绘制与命中测试，**但仍参与布局** |
| 子树只有单个叶子且自身无重叠绘制 | 将 alpha 直接乘入该叶子的 `SkPaint`，**不开 layer** |
| 其余情况 | `saveLayer()` 合成整个子树 |

`opacity == 0` 仍参与布局，与 §5.9.7 拒绝 `visibility: hidden` 是同一立场：
需要移出布局流时用框架层 `<Show>` 卸载节点，语义不含糊。

**调试构建统计单帧 `saveLayer` 次数并在超阈值时告警**，与内在尺寸查询的告警同源 ——
隐性成本一律显性化。另提供 overdraw 可视化叠层。

#### 5.11.3 颜色表示的三条硬约定

这三条不写死必然出 bug：

1. **ABI 与主题 JSON 用直通（unpremultiplied）alpha，内部转预乘**
   Skia 内部为 `kPremul_SkAlphaType`。边界格式固定为 `0xAARRGGBB` 直通值，
   `#RRGGBBAA` 的主题写法自然对应。转换只在 Graphics 层入口发生一次。

2. **混合在 sRGB 空间进行，不做线性化**
   与 CSS、Figma 等设计工具行为一致。这不是"物理正确"的选择，
   但**一致性优先于正确性** —— 设计稿与实现必须像素级对得上。
   色彩空间统一为 sRGB，不支持 Display P3 等广色域。

3. **次像素抗锯齿在透明背景上必须降级**
   LCD 次像素渲染需要已知的真实背景色，在 `saveLayer` 内部或透明窗口上无法成立。
   此时文本自动降级为灰度抗锯齿。这是可见的质量差异，**属于预期行为而非缺陷**，
   黄金图像测试需为两种路径分别建立基准。

#### 5.11.4 主题 token 中的 alpha

token 是颜色值，天然携带 alpha。但"同一颜色的不同透明度"（如 Material 的状态层：
primary 色 12% 作为 hover 遮罩）**不引入表达式语法**：

- ❌ `"$color.primary @ 0.12"` —— 违反 §5.9.2 准入规则第 4 条（不在字符串里再解析一层语法）
- ✅ 定义独立 token `color.primary-hover`，取值在主题 JSON 中直接写全

代价是 token 表变长，收益是加载器不需要表达式求值器，且每个 token 都能被 schema 校验。

#### 5.11.5 窗口级透明

由 `PlatformCaps.window_transparency` 门控（§5.1）。一处实现约束：

> **透明窗口需要带 alpha 通道的帧缓冲配置，必须在创建 GL/Metal 上下文时请求，
> 事后无法切换。** 因此它是 `WindowDesc` 的创建期标志，不是可写属性。

`IWindow::set_opacity()` 是另一回事 —— 它设置的是**整窗合成不透明度**（OS 合成器行为），
与帧缓冲是否有 alpha 通道无关，可随时修改。两者不要混淆。

### 5.12 文本：显示与编辑的能力边界

"富文本"一词混淆了四件成本差极大的事，本项目逐项表态：

| 能力 | 成本 | 决定 |
|------|------|------|
| **富文本显示**：一段内多字体 / 颜色 / 字号 / 装饰 | **近乎免费**，SkParagraph 原生支持 styled spans | **支持（P3）** |
| **纯文本编辑**：`TextField` / `TextArea` | 中 | **支持（P3/P4）** |
| **纯文本编辑 + 计算样式**：语法高亮 | 中，文档模型仍是平坦字符串 | 架构可行，**列为后续** |
| **富文本编辑（WYSIWYG）** | **人年级** | **不做** |

#### 5.12.1 为什么显示便宜而编辑昂贵

显示只需把 styled span 数组喂给 SkParagraph，排版、BiDi、字体回退全由它承担。

编辑则要求一整套子系统：

- **文档模型**：不再是字符串，而是带属性的 span 树或 piece table
- **属性化 undo/redo**：撤销单位包含样式变更，不只是文本增删
- **带格式的剪贴板往返**：HTML / RTF 的解析与生成
- **光标处样式回传**：工具栏按钮状态需实时反映插入点的样式
- **跨样式边界的选区语义**：选区跨越样式边界时的合并、拆分与继承规则

这五项彼此耦合，无法增量实现 —— 与 stonegui 复盘的判断一致：
**缺口是子系统级的，不是加几个属性能闭合的。**

#### 5.12.2 只读多样式显示覆盖的实际场景

这条边界之所以划得住，是因为**只读多样式**已覆盖大部分真实需求：
代码高亮展示、Markdown 预览、带提及与链接的聊天消息、日志查看器、
带强调片段的说明文本。

第三行"纯文本编辑 + 计算样式"是一个**便宜的中间态**：文档模型仍是平坦字符串，
样式由文本内容推导（词法分析）而非用户直接施加，因此不需要上述五个子系统。
代码编辑器属于这一档。若将来需要，可在不改动文档模型的前提下加入。

### 5.13 文本编码、索引与国际化

#### 5.13.1 UTF-8 是唯一的边界格式

ABI、主题 JSON、内部存储一律 **UTF-8**。字符串以 `{指针, 字节长度}` 传递，
不依赖 NUL 终止。

理由：JSON 天然 UTF-8；JS 侧的 `TextEncoder` 转换成本可忽略；
其余语言绑定 UTF-8 均无摩擦。不提供 UTF-16 / UTF-32 入口。

#### 5.13.2 三种索引空间——本层最大的错误来源

文本位置有三种互不相同的计量方式，且**它们在纯 ASCII 下完全一致**，
因此错误只在遇到非 ASCII 时才暴露：

| 索引空间 | 使用者 |
|---------|--------|
| UTF-8 字节偏移 | 本项目内部存储、ABI 对外接口 |
| **UTF-16 码元偏移** | **SkParagraph 的返回值**、JS 字符串 |
| grapheme cluster 索引 | 用户可见的"第几个字符"（§5.10.4） |

**Skia 是 UTF-16 语义的**（它为 Chrome / Flutter 而生）。
`Paragraph::getGlyphPositionAtCoordinate()` 返回 UTF-16 偏移，
直接拿去索引 UTF-8 缓冲区在 ASCII 下测试全部通过，一遇中文即静默错位。

**处置：靠类型系统挡住，不靠纪律。**

- 三种偏移用互不隐式转换的强类型包装：`ByteOffset` / `Utf16Offset` / `GraphemeIndex`
- 转换必须经显式函数，函数名即文档
- **Skia 返回的 `Utf16Offset` 在 Graphics 层入口即转换，绝不向上层泄漏**
- ABI 对外只暴露 `ByteOffset`；文本编辑 API 额外保证其落在 grapheme 边界

> 这是 stonegui 复盘中"指针身份判等导致 setter 静默 no-op"的同类问题——
> 类型正确但语义错误。区别在于本项目用类型系统把它变成编译错误。

#### 5.13.3 非法编码的处置

ABI 输入是不可信数据。收到非法 UTF-8 序列时**返回错误码，不静默替换为 U+FFFD**，
与 §5.7.5 未知 token 名的处置一致。另提供显式的 `dg_utf8_sanitize()`
供调用方在明知输入可能脏时主动清洗。

#### 5.13.4 ABI 字符串所有权

（本条定案 §12 原第 3 项）

- **传入**：drawgui 立即拷贝，调用返回后调用方即可释放
- **传出**：drawgui 拥有，有效期至下一次同类调用或显式释放函数

单向、无共享、无引用计数。牺牲少量拷贝换取绑定层实现简单——
FFI 场景下所有权歧义的代价远高于拷贝。

#### 5.13.5 语言标签与汉字统一表意

**这是 CJK 项目必须处理的一项。** 由于汉字统一表意（Han unification），
同一码点在中日韩字体下字形不同——`骨`、`直`、`次`、`海` 皆是典型。

不提供语言提示时，字形取决于字体回退链中谁排在前面，
简中用户可能看到日文字形。因此：

- `TextStyle` 携带 **BCP 47 语言标签**，透传至 `SkParagraph::TextStyle::setLocale`
- 语言标签有三级来源，就近优先：节点属性 → `TextStyleScope` → App 默认 locale
- `FontManager` 的回退链按语言标签分别构建（zh-Hans / zh-Hant / ja / ko 各一条）

#### 5.13.6 断行

CJK 不以空格分词，断行完全依赖 **ICU 的 UAX #14 规则**。
中文另有避头尾约束（行首不得为 `。，！？」`，行尾不得为 `「（`）。

**因此 §5.10.5 裁剪 ICU 数据时，断行规则表不可裁掉**——
它与 BiDi、grapheme 分割同属必需项。泰语、老挝语需要词典分词，
若裁剪掉相应数据则这些语言断行退化，属已知取舍。

#### 5.13.7 BiDi 与 RTL 布局镜像

两件事，成本差距很大：

| | 说明 | 决定 |
|---|------|------|
| **文本 BiDi** | 阿拉伯语 / 希伯来语的双向文本排版 | **支持** —— SkParagraph + ICU 提供，近乎免费 |
| **RTL 布局镜像** | 整个界面镜像：Flex 主轴反转、padding 左右互换、图标翻转 | **不做**（§11） |

**为什么现在不做也不会锁死**：布局属性目前是物理方向（`padding_l` / `padding_r`）。
将来若需 RTL 镜像，是**新增**一组逻辑属性（`padding_start` / `padding_end`），
在 §5.8 的 ABI 稳定性规则下属 **MINOR（可加）而非 MAJOR（破坏）**。
两组属性可共存，LTR 下 `padding_start ≡ padding_l`。

> 因此这条不需要现在决策。记录此推理是为了避免将来重新辩论。

#### 5.13.8 内置控件的字符串表

内置控件自带文本（右键菜单的"复制/粘贴"、对话框的"确定/取消"等）需要本地化。
机制与主题 token 同构：编译期生成 `string_id` 常量，运行时可覆盖。

```c
int dg_set_string(dg_app_t*, uint16_t string_id, const char* utf8, size_t len);
```

默认英文。宿主语言负责提供译文，不内置任何语言包。

#### 5.13.9 明确不提供：区域格式化

数字、日期、货币、复数规则的本地化格式化**不由本项目提供**。
宿主语言完成格式化后传入结果字符串（JS 侧有 `Intl`）。

理由：这是"内核"定位的边界——格式化不涉及渲染，纳入只会把 ICU 的
完整数据依赖锁死，与 §5.10.5 的裁剪目标直接冲突。

### 5.14 平台集成的扩展策略

平台能力无法预先穷举。若每新增一项就往 `IPlatform` 加方法，接口将无界膨胀，
且每个后端被迫实现或桩掉全部条目。因此本节定的是**规则**，不是清单。

#### 5.14.1 四级分类

| 级别 | 判据 | 承载方式 | 例子 |
|------|------|---------|------|
| **T1 核心** | 所有目标平台都有，且缺失则 GUI 无法工作 | `IPlatform` / `IWindow` 直接方法 | 窗口、事件、绘制表面、DPI、文本剪贴板、光标 |
| **T2 可选服务** | 部分平台有；缺失时功能降级但应用仍可用 | `query_service<T>()`，不支持返回 `nullptr` | 托盘、原生菜单栏、系统通知、文件对话框、全局热键、单实例、URL scheme、系统配色跟随 |
| **T3 宿主自理** | 平台特有或需求稀疏 | 暴露原生句柄，宿主语言自行调用 | 打印、屏幕录制、各平台专有 API |
| **T0 不做** | 成本与定位不符 | —— | 无障碍（§11） |

**T1 保持最小**。`IPlatform` 上现有的 `open_file_dialog()` / `show_notification()`
按此规则应下沉为 T2 服务——移动端与嵌入式的语义与桌面差异过大，
放在 T1 会迫使每个后端桩掉。

#### 5.14.2 服务查询

```cpp
template <class T> T* IPlatform::query_service();   // 不支持返回 nullptr

// 调用方必须显式处理缺失，无法忽略
if (auto* tray = platform->query_service<ITrayService>()) { ... }
```

ABI 侧对应按服务 ID 查询，返回函数表指针或 `NULL`：

```c
const void* dg_query_service(dg_app_t*, uint16_t service_id);
```

**能力必须可查询而非试错**——调用一个不支持的服务应当在查询阶段就得到明确答复，
而不是调用后返回一个含糊的失败码。

#### 5.14.3 提升规则

T3 提升为 T2，须同时满足三条（与 §5.9.2 的属性准入规则同构）：

1. **三个以上目标平台**存在对应能力
2. 语义能**收敛为单一跨平台接口**，而非各平台一套参数
3. 至少**两个真实用例**提出需求

不满足则留在 T3。这条规则的作用是把"接口该不该加"从每次辩论变成一次判定。

#### 5.14.4 原生句柄逃生口（T3 的载体）

```c
typedef struct {
  uint32_t kind;    // WIN32_HWND / COCOA_NSWINDOW / X11_WINDOW /
                    // WAYLAND_SURFACE / ANDROID_NATIVE_WINDOW / UIKIT_UIVIEW
  void*    handle;
  void*    display; // X11 Display* / Wayland wl_display*，其余为 NULL
} dg_native_window;

int dg_window_native_handle(dg_window_t*, dg_native_window* out);
```

**这是有意提供的不安全出口**：使用它即离开 drawgui 的跨平台保证，
句柄生命周期由 drawgui 管理，宿主不得持有跨帧引用。文档需明确标注风险。

#### 5.14.5 嵌入模式与事件循环所有权

§1 将 drawgui 定位为"可被任意语言嵌入的内核"。若 drawgui 必须拥有事件循环，
该定位不成立——无法嵌入既有 Qt / Win32 / Cocoa 应用，无法作为插件 UI
（VST、图像软件插件），Android 上无法作为一个 `View` 存在。

**因此事件循环从第一天支持两种所有权模式**：

| 模式 | 循环归属 | 接口 | 用途 |
|------|---------|------|------|
| **独立** | drawgui | `run_loop()` / `wake()` | 独立应用（MVP 实现） |
| **嵌入** | 宿主 | `pump_once()` + `inject_event()` + `render_now()` | 嵌入既有应用、插件 UI、移动端 View |

嵌入模式下 drawgui **不创建窗口**，而是渲染进宿主提供的表面：

```c
dg_window_t* dg_window_attach_native(dg_app_t*, const dg_native_window*,
                                     const dg_window_opts*);
int          dg_pump_once(dg_app_t*);              // 宿主循环中调用
int          dg_inject_event(dg_window_t*, const dg_input_event*);
int          dg_render_now(dg_window_t*);
```

**MVP 只实现独立模式，但接口形状现在确定。** 理由与 C1 同类：
"谁拥有事件循环"渗透到窗口创建、渲染时机、输入来源三处，
事后改动等同重构。现在把形状定下来成本接近零。

#### 5.14.6 系统配色跟随

T2 服务，与 §5.7 的主题变体直接对接：查询系统 light/dark 偏好并订阅变更通知，
变更时切换 `theme.variant`。因 `$token` 是活引用，切换不重建控件树。

不支持该服务的平台上退化为固定变体，由应用自行选择。

#### 5.14.7 移动端生命周期

`pause` / `resume` / `low_memory` / `safe_area_changed` 属 T1——
它们不是可选功能，而是移动平台的强制契约，忽略会导致崩溃或被系统终止。

**接口位置 MVP 即确定**，桌面后端实现为空操作。
`low_memory` 需触发 §5.10.3 的 GPU 纹理缓存收缩。

### 5.15 性能模型

stonegui 复盘 2.5 列出 LVGL 的三项天花板：纯软件光栅、单线程绘制、DPI 写死。
本项目在这三处分别有答案（GPU Ganesh、解码线程池、逻辑像素 + 每屏缩放）。
本节把散落各层的性能机制收拢为统一模型。

#### 5.15.1 按需渲染，而非持续渲染

这是 GUI 工具包与游戏引擎的分野。持续 vsync 循环意味着空闲时 CPU 仍在满速运转。

```
空闲                → wait_events() 阻塞，CPU 占用 0%
存在脏区或活跃动画   → 进入 vsync 驱动的帧循环
动画结束且无脏区     → 退回阻塞
```

多窗口下**每窗口独立脏标记**，只渲染脏窗口（§5.2）。

#### 5.15.2 三级失效，代价递增

脏标记分三级，各自有边界限制传播范围：

| 级别 | 触发 | 边界 | 代价 |
|------|------|------|------|
| 需重新合成 | `opacity` / `transform` 变化 | Layer | 最低，仅重放已录制的 `SkPicture` |
| 需重绘 | 颜色 / 文本内容变化 | repaint boundary | 中，重新录制该子树绘制命令 |
| 需重排 | 尺寸 / 布局属性变化 | relayout boundary（§5.4.1） | 最高，重新布局并重绘 |

**关键认知：保留模式 GUI 的开销通常在"遍历树 + 录制绘制命令"，而非光栅化本身。**
因此 repaint boundary 的 `SkPicture` 缓存比脏矩形更重要——滚动时不重新遍历子树。

**但全屏重绘的内存带宽成本随分辨率线性增长，且集成显卡与 CPU 共享内存带宽**：
4K 全屏每帧写入约 33 MB，60 fps 即 2 GB/s，会直接挤占 CPU 可用带宽；
1080p 仅 8 MB/帧，压力为其四分之一。

因此**部分呈现按物理像素数自动启用，不是可选优化**：

| 窗口物理像素 | 呈现策略 |
|-------------|---------|
| < 2 M（约 1080p 及以下） | 全屏重绘，实现简单且带宽充裕 |
| ≥ 2 M（高 DPI / 2K / 4K） | **启用脏区部分呈现**（`eglSwapBuffersWithDamage` 等） |

阈值可覆盖，但默认行为不依赖使用者判断——低端集成显卡配高分屏是常见组合，
默认配置必须在该组合下成立。

#### 5.15.3 属性存储：不使用 per-node 哈希表

ABI 是动态的（`dg_node_set_prop(node, prop_id, value)`），但**动态性只存在于边界**。

- ❌ 每节点一个 `map<prop_id, value>`：内存膨胀、缓存局部性崩溃、每次读属性一次哈希
- ✅ `prop_id` 进 `switch`，直接写 RenderObject 的**具体成员字段**

RenderObject 内部仍是紧凑 POD 结构，与 Flutter 的编译期字段布局等价，
同时保留 ABI 的动态属性能力。这是两者兼得的关键，**必须在 P2 就按此实现**——
先上哈希表后期再改，等同重写全部 RenderObject。

节点内存目标：`RenderBox` ≤ 256 字节，罕用属性置于按需分配的溢出块。

#### 5.15.4 列表虚拟化是必需品

1000 项列表若为每项创建 RenderObject，前述所有优化均无意义。

**`List` / `Table` 内置虚拟化**：仅为可视区加上下缓冲区的项创建节点，
滚出即回收进对象池。这不是可选优化，而是控件的定义的一部分。

对 JS 框架层的要求：`<For>` 在虚拟化容器内的语义是"提供数据与项渲染函数"，
而非"立即挂载全部子节点"。§6 的 `<For>` 需支持这两种模式。

#### 5.15.5 内存分布与预算

| 项 | 量级 | 控制手段 |
|---|------|---------|
| GPU 纹理（字体图集 / 图片 / layer） | **最大项** | 显式预算 + LRU；跨窗口共享图集（§5.2）；`low_memory` 主动收缩 |
| `SkPicture` 录制 | 中 | 随 repaint boundary 数量增长，边界不宜过密 |
| Paragraph 缓存 | 中 | 按 `(文本, 样式, 宽度)` 键控 LRU——文本 shaping 是 CPU 大头，值得缓存 |
| RenderObject 树 | 小 | §5.15.3 的紧凑布局 |
| ICU 数据 | ~10 MB | 裁剪（§5.10.5、§12） |

#### 5.15.6 线程模型

| 工作 | 线程 |
|------|------|
| 布局、绘制命令录制、事件处理 | 主线程 |
| 图片解码 | 线程池（§5.10.3） |
| 光栅化 | GPU |
| 字体 shaping | 主线程，靠 Paragraph 缓存摊薄 |

不引入多线程布局。收益有限而复杂度极高，且与 L1–L3 不变式（§5.4.1）的
单遍语义冲突。

#### 5.15.7 可验证的目标

"性能好"不可验证。以下指标进入 CI 作为回归门槛，任一劣化即失败。

**指标必须绑定基准硬件，否则无意义**——同一个 60 fps 在独显与集成显卡上
是完全不同的要求：

> **基准硬件下限**：Intel UHD 620 级集成显卡 / 双核四线程 / 8 GB 内存，
> **1080p @ 100 % 缩放**。全部指标在该配置上达成。
>
> **附加验证配置**：同一硬件 + 4K @ 200 % 缩放，用于验证 §5.15.2 的
> 部分呈现策略确实生效。

| 指标 | 目标 |
|------|------|
| 空闲 CPU 占用 | **0%** |
| 冷启动至首帧（~200 节点界面） | < 100 ms |
| 滚动 1000 项虚拟化列表 | 稳定 60 fps，无掉帧 |
| 空窗口常驻内存（含 Skia + ICU） | < 30 MB |
| 单窗口二进制体积（Release，含 Skia） | < 20 MB |
| 单帧布局耗时（~500 节点） | < 2 ms |

**当前无任何实测数据**，上述取值基于机制推断。P1 完成上屏后即做首次实测并校准。

#### 5.15.7a 高成本特性与降级

少数特性在集成显卡上代价显著，需显式标注并可关闭：

| 特性 | 集显代价 | 处置 |
|------|---------|------|
| 背景模糊（毛玻璃） | 高 | §5.3.3 已限定为唯一开放的图像滤镜；主题可禁用 |
| 大量并发 `opacity` 动画 | 中高，每个触发 `saveLayer` | §5.11.2 的四档优化；调试构建告警 |
| 大面积 overdraw | 中 | overdraw 可视化叠层（§5.11.2） |

**框架检测并上报，但不自动降级。** 帧预算持续超支时发出事件，
由宿主决定是否切换低配模式。自动降级会使行为不可预测且难以测试，
与 §5.3.4 固定渲染质量参数的取向一致。

#### 5.15.8 帧预算与可观测性

60 Hz 下单帧预算 16.6 ms，调试构建按阶段分解计时：
**布局 / 录制 / 上传 / GPU 提交**。

超预算时输出告警并指出耗时最高的子树路径。这与已有的两处告警同源
（内在尺寸查询 §5.4.6、`saveLayer` 次数 §5.11.2）——
**隐性成本一律显性化**，这是本项目在性能上的统一立场。

`dg_dump_layout_tree`（§5.4.10）的输出附带每节点上一帧的布局与绘制耗时。

### 5.16 动画、滚动与手势

三者高度耦合：滚动惯性是动画，惯性由手势速度驱动，因此合并设计。

#### 5.16.1 动画在 C++ 内完成，宿主只声明

**这是本节最关键的约束。** 宿主语言不得逐帧驱动动画：

- 每帧穿越 FFI 边界写属性，在 Node/Bun FFI 下是性能灾难
- `setTimeout` / `setInterval` 与 vsync 无关，必然掉帧

因此动画时钟由 C++ 核心拥有（vsync 驱动），宿主只声明起止值与曲线，
**插值与属性写入全程不离开 C++**：

```c
// 显式动画：返回句柄，可暂停 / 反向 / 取消
dg_anim_t* dg_animate(dg_node_t*, uint16_t prop_id,
                      const dg_value* from, const dg_value* to,
                      uint32_t duration_ms, uint16_t curve_id);

// 隐式动画：声明后该属性的任何变化自动插值（CSS transition 模型）
int dg_node_set_transition(dg_node_t*, uint16_t prop_id,
                           uint32_t duration_ms, uint16_t curve_id);
```

动画完成 / 取消以事件形式进入 §5.8 的轮询队列，不使用跨线程回调。

**可插值类型**：float、color、offset、size、transform 的分解分量（§5.9.6 将
transform 分解为 translate/scale/rotate 正是为此）。不可插值的属性直接跳变，
不报错但在调试构建下告警。

动画期间相关节点自动提升为重绘边界，结束后降级（§5.11.2）。
活跃动画使帧循环保持运行，全部结束后退回阻塞（§5.15.1）。

**尊重系统的减弱动态偏好**（reduced motion）：作为 T2 服务查询（§5.14.2），
开启时动画时长统一归零而非跳过逻辑——保证状态机行为一致。

#### 5.16.2 滚动

`RenderViewport` 的滚动语义：

| 方面 | 决定 |
|------|------|
| 惯性（fling） | 指针抬起时按速度进入 decay 模拟 |
| 过度滚动 | **默认跟随平台**（macOS 橡皮筋回弹 / Windows 硬停），可显式覆盖 |
| 嵌套滚动 | 内层到达边界后将剩余滚动量传递给外层可滚动祖先 |
| 滚动锚定 | 可视区上方插入内容时保持当前视觉位置，不跳动 |
| 键盘 | `PageUp/Down`、`Home/End` 经 §5.5.1 的意图机制绑定 |
| 滚轮 | 滚光标下的可滚动祖先，不是焦点控件（§5.5） |

**滚动条形态影响布局**：叠加式（overlay，macOS / 移动端）不占空间；
占位式（Windows / Linux 传统）会减少内容可用宽度。
因此它是布局输入而非纯装饰，由平台默认值决定并可覆盖。

#### 5.16.3 手势竞技场

移动端在目标平台内（§3），而 §5.5 描述的全是桌面语义。补齐手势识别。

**核心问题**：同一指针序列可能被多个识别器竞争——单击 vs 拖动 vs 长按，
且命中链上多个祖先节点都可能声明手势（列表项的 tap 与列表的 pan）。
需要一个**延迟决策**阶段进行仲裁。

采用竞技场模型：

```
指针按下 → 命中链上所有手势识别器进入竞技场
         → 各识别器观察后续事件，可声明"胜出"或"退出"
         → 唯一剩余者或首个声明胜出者获得该序列，其余收到取消
```

**必须现在设计的原因**：竞技场在指针事件路由中插入了一个延迟裁决阶段，
事后加入等同重写整个指针路由——与 C2 同类的架构性风险。

内置识别器：`tap` / `double_tap` / `long_press` / `pan` / `pinch` / `rotate` / `fling`。
桌面同样受益：拖放的启动阈值、双击与三击判定走同一机制。

### 5.17 健壮性与资源所有权

#### 5.17.1 ABI 异常边界

**C++ 异常穿越 C ABI 是未定义行为。** 每个导出函数必须完整拦截：

```c
// 生成器统一产出，不手写
DG_EXPORT int dg_xxx(...) {
  try { /* 实现 */ }
  catch (const std::bad_alloc&) { return DG_ERR_OOM; }
  catch (...)                   { return DG_ERR_INTERNAL; }
}
```

**由 `abi/drawgui.def.toml` 的生成器统一包裹**（§5.8 决策 5）。手写必然遗漏，
而遗漏的后果是进程级崩溃且难以定位。

回调方向同理：宿主传入的回调若抛出（如 QuickJS 抛 JS 异常），
必须在 C++ 侧转换为错误码，不得穿透。

#### 5.17.2 节点所有权

（补齐 §5.13.4 只覆盖字符串的空白）

**drawgui 始终拥有节点**，宿主持有的是句柄：

- `dg_node_create()` 返回句柄，节点归 drawgui 的 App 级竞技场所有
- 挂载到父节点不转移所有权，仅改变树结构
- `dg_node_remove()` 从树中摘除，节点仍存活可重新挂载
- `dg_node_destroy()` 显式销毁，递归销毁子树；此后句柄失效
- App 销毁时回收全部节点

**不引入引用计数**。FFI 场景下引用计数的所有权歧义代价远高于显式销毁，
与 §5.13.4 的字符串策略取向一致。

调试构建维护句柄有效性表，对已销毁句柄的调用返回 `DG_ERR_INVALID_HANDLE`
而非崩溃——use-after-free 在跨语言场景中难以排查，值得付出这份运行时开销。

#### 5.17.3 GPU 设备丢失

Windows 驱动更新、系统休眠、TDR 超时均会导致 `GrDirectContext` 失效。
这不是边缘情况，而是桌面应用的常态事件。

恢复流程：检测失效 → 丢弃全部 GPU 资源 → 重建 context →
重新上传字体图集与纹理 → 重放 `SkPicture` 重绘全部窗口。

**RenderObject 树与布局结果不受影响**，因为它们不持有 GPU 资源——
这正是 §5.15.2 中 Layer 与 RenderObject 分离的附带收益。

#### 5.17.4 无障碍挂点（落实 C5）

C5 要求"留架构挂点，MVP 不实现但不堵死"。挂点的具体形态：

- 每个 RenderObject 可携带语义信息：`role` / `label` / `value` / `state` / 可操作性
- 由此构建一棵 **a11y 树**：与 render 树平行，但更稀疏（纯装饰节点不出现）
- MVP **只构建树并支持 dump**，不对接任何 OS 无障碍 API

即使不对接 OS，这棵树立刻有两项收益：**可作为自动化测试的语义断言目标**
（比像素比对更稳定），以及将来接入 UIA / NSAccessibility / AT-SPI 时
无需改动任何控件实现。

#### 5.17.5 断言与日志

- **调试构建**：断言失败即 abort 并输出节点路径
- **发布构建**：降级为可恢复行为并记录，绝不静默
- 日志经回调交由宿主处理，drawgui 不直接写文件或 stdout：

```c
int dg_set_log_callback(dg_app_t*, dg_log_cb, void* user, uint8_t min_level);
```

## 6. JS 框架层

继承 stonegui `framework.js` 的设计（复盘第 7 节确认为与后端解耦的资产）：

- **响应式核心**：`signal` / `effect` / `memo` / `owner` 树 / `batch`
- **挂载一次 + 属性级更新，无虚拟 DOM** —— 每个属性绑定是一个独立 effect，
  直接调 `dg_node_set_prop`
- `<Show>` / `<For>` 是**唯一的动态挂载点**
- `createRoot` 隔离行级 owner，防止列表项更新时级联 dispose
- JSX 经 esbuild 转译
- `drawgui.d.ts` **生成而非手写**

**运行时适配层** `js/runtime/`：统一 `Bun:ffi` / `node koffi` / QuickJS native module
的调用签名。上层框架代码对运行时无感知。

**事件泵**：Node/Bun 一期用 `setImmediate` 循环驱动 `dg_poll_events`，
配合 `dg_wait_events(timeout)` 避免空转。

## 7. 工程化（C4 —— 在第一个控件之前就位）

| 项 | 内容 |
|---|---|
| 构建 | CMake ≥ 3.24 + Ninja；`FetchSkia.cmake` 下载校验预编译库 |
| CI | GitHub Actions：Linux 全量 / macOS+Windows 编译验证 / Android+iOS 交叉编译验证 |
| 格式 | `.clang-format`，CI 强制 |
| 静态检查 | `clang-tidy`，含 `bugprone-*` `cppcoreguidelines-*` |
| 内存 | ASan + UBSan 跑测试；ABI 层额外跑 Valgrind |
| **黄金图像测试** | **一律在 CPU raster 上**渲染 → PNG → 像素比对（容差 + 差异图）。GPU 结果因驱动而异，不可用于像素断言（§5.3.2） |
| 单元测试 | doctest —— 布局约束、命中测试、焦点序 |
| 布局快照测试 | `dump_layout_tree` 输出与基准 JSON 对比，比像素比对更快且失败信息更精确 |
| 布局基准夹具 | 仿 Yoga `gentest`：夹具写成 HTML，在 Chrome 中渲染生成期望矩形（§5.4.11） |
| ABI 测试 | 纯 C 编写的示例程序，验证头文件不依赖 C++ |
| 主题加载器 fuzz | 用畸形 JSON / 穿越路径 / 超大 SVG 做模糊测试（§5.7.5） |
| 一致性检查 | `tools/check_consistency.py` —— schema、prop_id 表、`.d.ts`、内置主题四方对齐 |
| 快捷键检查 | 各平台绑定表无重复绑定；每个 `action_id` 在三平台均有绑定（§5.5.1） |
| 性能门槛 | §5.15.7 的六项指标入 CI，任一劣化即失败 |

> **黄金图像测试必须在 P2 就建立。** 自绘 GUI 没有 DOM 可断言，
> 像素比对是唯一能防住"改了布局代码，某个边角悄悄错了"的手段。
> stonegui 复盘提到过 `check(true, …)` 式假测试的坑 —— 断言必须真的比对像素。

## 8. 目录结构

```
drawgui/
├── cmake/                    FetchSkia.cmake 等
├── include/drawgui/
│   ├── platform/             IPlatform / IWindow / PlatformCaps
│   ├── graphics/             Canvas / Paragraph / FontManager
│   ├── render/               RenderObject 及派生
│   ├── input/                FocusManager / PointerRouter / Shortcuts
│   ├── widgets/
│   └── theme/
├── src/                      对应实现
│   └── platform/sdl3/        SDL3 后端
├── abi/
│   ├── drawgui.def.toml      ★ ABI 单一真相源
│   ├── drawgui.h             生成
│   └── abi_impl.cpp
├── js/
│   ├── framework.js          响应式核心
│   ├── runtime/              bun.js / node.js / quickjs.js
│   └── drawgui.d.ts          生成
├── themes/
│   ├── schema.toml            ★ token schema 单一真相源（编译期契约）
│   └── builtin/               内置主题 package（JSON + 资源，嵌入二进制）
├── tools/                    代码生成器 + 一致性检查（★ 不许删）
├── tests/
│   ├── unit/
│   └── golden/               黄金图像基准
├── examples/
├── third_party/skia-prebuilt/
└── doc/design.md
```

## 9. 实施路线图

| 阶段 | 内容 | 验收标准 |
|---|---|---|
| **P0** 地基 | CMake + CI + clang-format/tidy + `FetchSkia.cmake` + **CPU raster 路径** | Linux 上编译通过；CPU raster 输出一张 PNG，黄金测试管道贯通 |
| **P1** 平台与上屏 | `IPlatform`/`IWindow` 接口（含 T2 服务查询与双循环模式形状）+ SDL3 实现 + Skia GL context | **同时打开两个窗口**，各画一个不同颜色矩形；共享 GrDirectContext 生效 |
| **P2** 渲染层 | RenderObject（紧凑字段存储，非哈希表）、约束布局、Stack/Flex/Wrap、Layer、命中测试、`dump_layout_tree` | 黄金图像测试框架就位并有 ≥10 个基准通过；固定定位与 flex 均可工作；无界约束冲突能报出节点路径；空闲 CPU 0% |
| **P3** 控件与主题 | 8 个 MVP 控件（`List` 含虚拟化）+ schema 生成器 + JSON 主题加载器（内置主题） | 不新增 RenderObject 即可实现全部 8 个控件；light/dark 运行时切换；1000 项列表 60fps |
| **P4** 输入与窗口语义 | Focus/Pointer/Shortcut 意图系统 + **手势竞技场** + `PopupHost` + Dialog | 右键菜单以真 OS 窗口弹出并超出父窗口边界；Tab 序正确；快捷键走意图表且四级路由正确；tap 与 pan 竞争仲裁正确 |
| **P4.5** 动画与滚动 | C++ 内动画时钟、隐式过渡、惯性与回弹 | 动画全程不跨 FFI 边界；惯性滚动手感跟随平台 |
| **P5** ABI | `drawgui.def.toml` + 生成器 + ABI 实现 | 纯 C 示例程序创建两个窗口并响应点击 |
| **P6** JS 绑定 | framework.js + Bun/Node FFI + JSX | TodoMVC 级示例；多窗口示例 |
| **P7** 补齐 | IME、外部 theme package + 资源/图标、热重载、Windows/macOS 验证 | 中文输入可用；第三方主题目录可加载且热重载；三桌面平台 CI 全绿 |
| **P8** 移动端 | Android/iOS 宿主 + 生命周期 + 触摸 | 单个示例在真机跑通 |

依赖关系：P2/P3 可与 P1 后半程并行；P4 依赖 P1+P3；P5 依赖 P3+P4；P6 依赖 P5。

## 10. 风险登记

| 风险 | 影响 | 对策 |
|---|---|---|
| SDL3 的 popup 窗口支持不足以实现原生菜单 | **高** | P1 就做 spike 验证；退路是桌面上直接写三份原生 popup |
| 预编译 Skia 的 fork patch 与官方行为有差异 | 中 | 记录确切 tag；黄金图像测试能发现渲染差异 |
| 共享 GL context 在某平台失效 | 中 | 回退到每窗口独立 GrDirectContext（已有方案） |
| Node FFI 事件泵与窗口消息循环的耦合 | 中 | `dg_wait_events(timeout)` + poll 模式已设计 |
| 第三方 theme package 是不可信输入（路径穿越 / 解析炸弹 / 恶意 SVG） | 中 | §5.7.5 的四项限制；fuzz 主题加载器 |
| C++ 异常穿越 C ABI 导致进程崩溃 | 中 | 生成器统一包裹 try/catch（§5.17.1），不允许手写导出函数 |
| GPU 设备丢失（驱动更新 / TDR / 休眠） | 中 | §5.17.3 重建流程；RenderObject 树不持有 GPU 资源 |
| 低端集成显卡 + 高分屏组合下带宽不足 | 中 | §5.15.2 按像素数自动启用部分呈现；基准硬件下限含 4K 验证配置 |
| 老旧 Intel / AMD OpenGL 驱动的 bug 与性能陷阱 | 中 | 回退 CPU raster（§5.3.1）；需在真实老硬件上验证，无法靠模拟发现 |
| 控件内置核心 → 扩展需改 C++ 重编 | 中 | 已接受；`dg_custom_paint` 缓解 |
| Skia 静态库使二进制达 15 MB+ | 低 | 已明确接受 |

## 11. 明确不做（Scope Boundary）

- **无 GPU 裸屏设备** —— 已排除，不为此做软件光栅回退路径
- **无障碍实现** —— 只留架构挂点（a11y 节点树），不对接 UIA / NSAccessibility / AT-SPI
- **富文本编辑（WYSIWYG）** —— 富文本**显示**支持，**编辑**不做（§5.12）
- **AVIF / HEIF 图像** —— 不在 Skia 默认 codec 集合内（§5.10.2）
- **Lottie 动画** —— Skia `skottie` 模块可用，但列为未来可选，不进 P0–P8
- **SVG 动画 / 脚本 / 完整滤镜** —— Skia SVG 是子集渲染器，SVG 仅作图像源（§5.10.1）
- **广色域（Display P3 等）** —— 统一 sRGB，混合不做线性化（§5.11.3）
- **RTL 布局镜像** —— 文本 BiDi 支持，但界面不镜像；将来可以 MINOR 方式追加逻辑属性（§5.13.7）
- **区域格式化**（数字 / 日期 / 货币 / 复数） —— 由宿主语言完成（§5.13.9）
- **内置语言包** —— 只提供可覆盖的字符串表，默认英文（§5.13.8）
- **Web / WASM** —— 不在目标平台内
- **多后端渲染抽象** —— Skia 单后端，不为换 Blend2D/自研光栅预留接口
- **CSS 完整布局模型** —— 无 `float`、`position: fixed`、`order`、`grid`；
  布局能力边界见 §5.4.3 的差异表
- **GTK CSS 式选择器主题** —— 主题只能赋值固定 token 表，不能匹配控件内部结构（§5.7.3）
- **运行时新增 token** —— token schema 是编译期契约，主题只能提供取值
- **与 Qt/Flutter 功能对标** —— 定位是"可嵌入的内核"，不是"完整应用框架"
- **原生控件混排** —— 全自绘，不嵌入 OS 原生控件

## 12. 未决问题

1. SDL3 的 popup 窗口（`SDL_CreatePopupWindow`）在 Wayland / Windows / macOS 上的
   实际行为一致性 —— **P1 必须 spike 验证**，它决定 `PopupHost` 的实现路径
2. 多窗口下共享 GL context 与 `SkSurface` 的 MakeCurrent 开销 —— P1 基准测量
3. JSON 解析器选型（nlohmann/json 便利 vs 更轻量的方案）—— P3 前定案，权衡二进制体积与编译时间
5. **ICU 数据的嵌入与裁剪**（§5.10.5）—— 完整 `icudtl.dat` 约 10 MB 量级，
   本项目仅需 BiDi / 断行 / grapheme / 大小写映射，P3 文本控件落地前定案
6. 是否需要 `RenderGrid`（二维网格布局）—— P3 后按实际控件需求决定。
   `Table` 可能可以用 Flex + 内在尺寸覆盖；若确需完整 CSS Grid，
   按 §5.4.11 的触发条件重新评估 Taffy 而非自研
7. 主题 `schema_version` 的跨版本迁移策略（拒绝旧版 vs 自动升级）—— P7 外部主题包落地前定案
8. 窗口位置 / 尺寸 / 滚动位置的持久化：内置 vs 交给宿主 —— P7 评估
7. 热重载的粒度：整树重建 vs 增量 patch —— P7 再定
