# 主题（Theming）

## 这个子系统解决什么问题

一个应用希望有 light/dark 两套配色，并且能在运行期切换，而不需要重建整棵
widget 树、不需要每个 widget 自己记住"我现在是浅色还是深色"。drawgui 的
做法是：定义一小组语义 token（`color.surface`、`radius.md` 这类名字），
每个 widget 的属性绑定到某个 token 而不是绑定到具体颜色值；切换主题时，
只需要重新解析每个绑定、把新值写回属性即可。

这套机制不处理："这个颜色变亮 20%""半透明遮罩叠一层" 这类表达式化的取色
逻辑——见下面"token 模型"一节。

## 核心类型

- `dg::Theme`（`include/drawgui/theme/theme.h:84`）：一份已解析完成的主题数据，
  三张按 `dg_token_id` 索引的扁平表：`int` 值一张（与 variant 无关），
  `color` 值两张（light 一张、dark 一张）。不是 hash map。
- `dg::ThemeVariant`：只有 `kLight`/`kDark` 两个值（`theme.h:66`）。
- `dg::ThemeBindings`（`include/drawgui/theme/theme_bindings.h:81`）：
  记录"某个 node 的某个 prop_id 现在由某个 token 驱动"的旁表，per-node
  一个小 vector（一个 node 可能同时绑定 fill、border 等多个属性）。
- `dg::bind_token()`（`theme_bindings.h:138`）：绑定入口，绑定的同时会
  立即按当前 variant 解析出值，通过 `dg::set_prop()` 写一次——绑定发生后
  不存在"未解析的中间态"。
- `dg::load_theme()` / `dg::load_builtin_theme()`
  （`include/drawgui/theme/theme_loader.h:112,122`）：把 JSON 文本解析成
  `Theme`。

最小示例（改编自 `examples/18_theme/theme_scene.cpp:26-114`）：

```cpp
dg::Theme theme = dg::load_builtin_theme().value();
dg::ThemeBindings bindings;

dg::NodeId panel = tree.add_child(root, box, dg::NodeStyle{});
(void)dg::bind_token(tree, bindings, panel, DG_PROP_BACKGROUND_COLOR,
                      theme, dg::ThemeVariant::kLight, DG_TOKEN_COLOR_SURFACE);
(void)dg::bind_token(tree, bindings, panel, DG_PROP_BORDER_COLOR,
                      theme, dg::ThemeVariant::kLight, DG_TOKEN_COLOR_BORDER);
```

切换主题（同样来自 `theme_scene.cpp:116-120`）：

```cpp
std::size_t switch_variant(Scene& scene) {
  scene.variant = scene.variant == dg::ThemeVariant::kLight
                      ? dg::ThemeVariant::kDark : dg::ThemeVariant::kLight;
  return scene.bindings.apply(scene.tree, scene.theme, scene.variant);
}
```

`bindings.apply()` 重新解析**每一条**已记录的绑定并通过 `dg::set_prop()`
写回，不做任何"值没变就跳过"的优化。整棵 widget 树不重建、node id 也
不会失效。

## Token 模型：只有二分法

`themes/schema.toml` 是 token 的编译期契约（生成
`include/drawgui/theme/token_ids.generated.h` 等文件），运行时数据在
`themes/builtin/theme.json` 里。当前定义了 12 个 token：

| 分组 | token | 说明 |
|---|---|---|
| color | `color.surface` | 面板默认背景 |
| color | `color.on-surface` | surface 上的文字/图标色 |
| color | `color.border` | 默认边框色 |
| color | `color.primary` | 主色调（按钮填充等） |
| color | `color.on-primary` | primary 上的文字/图标色 |
| color | `color.primary-hover` | primary 的 hover 态 |
| color | `color.primary-pressed` | primary 的 pressed 态 |
| color | `color.focus-ring` | 焦点环颜色 |
| radius | `radius.sm` | 小圆角 |
| radius | `radius.md` | 默认圆角 |
| space | `space.sm` | 小间距 |
| space | `space.md` | 默认间距/padding |

即 7 个基础语义色 + 1 个 focus-ring 色 = 8 个 color token，加 2 个圆角 +
2 个间距 = 4 个 int token（见 `themes/schema.toml:67-162`）。

`TokenType` 只有两种（`theme.h:40-43`）：

```cpp
enum class TokenType : std::uint8_t { k_color, k_int };
```

- `color` token **随 variant 变化**，存在 `theme.json` 的 `"variants"` 表里。
- `int` token **与 variant 无关**（设备像素的圆角/间距值），存在 `"base"` 表里。

这个二分法是刻意的，**没有第三种类型**，也**没有透明度表达式或多级嵌套语法**
（`themes/schema.toml:36-40`）："同一个颜色不同透明度" 这种状态（如
Material 的 state layer）不通过表达式实现，而是各自建一个独立 token——
`color.primary-hover`、`color.primary-pressed` 都是完整写出的独立颜色值，
不是 `color.primary` 加一个 alpha 修饰。这意味着：如果你需要一个新的
悬停/按下态颜色，去 schema 里加一个新 token，而不是尝试在运行时算出来。

`token_id_for_name()`（`theme.h:61`）是名字到 id 的正向解析，供 loader
在读 JSON 时用；`token_name()`/`token_type()` 是反向查询，通常用来拼诊断
信息。

## 颜色切换零重排，整数切换必然重排

这是本节最容易被忽视的一点：**`ThemeBindings::apply()` 对每条绑定都无条件
重写一次**，写入走的是普通 `dg::set_prop()` 门——这条门决定"这次写入触发
哪种失效"的规则和其他任何属性写入完全一样（`theme_bindings.h:40-50`）。
`background_color`/`border_color` 这类 `NodeStyle` 上的字段只触发重绘，
不触发布局；而 `gap`/`border_radius_*` 这类会影响布局的字段一旦被绑定，
哪怕两个 variant 下这个 int token 的值完全相同（内置主题里 int token
本来就与 variant 无关，永远不会变），**这次 `apply()` 依然会导致一次
relayout**，因为 `apply()` 不比较新旧值，只管重写。

`examples/24_theme_package/theme_package_check.cpp` 里的两组检查把这一点
钉死了：

- `check_color_only_reload()`（约 :91-145）构建一个**只绑定颜色属性**的场景，
  编辑 `theme.json` 的 `color.surface` 后热重载，断言
  `stats.nodes_visited != 0 || stats.nodes_relaid_out != 0` 时判为 **FAIL**
  （即期望两者都是 0，纯色切换零布局开销）。
- `check_int_token_reload()`（约 :151-184）构建一个把 `gap` 绑定到
  `space.md` 的**独立**场景，编辑 `space.md` 的值后热重载，断言
  `stats.nodes_visited == 0 || stats.nodes_relaid_out == 0` 时判为 **FAIL**
  （即期望两者都非 0，int token 切换必然引发重排，并且真的挪动了后续
  panel 的位置）。

这两个检查特意拆成两个独立场景（而不是同一场景先测颜色再测整数），因为
`apply()` 对绑定过 int token 的场景，哪怕只改颜色 token，也会因为"这条
绑定也被重写了一遍"而污染纯色场景的开销统计——`examples/18_theme/theme_scene.cpp:46-65`
的注释专门解释了为什么该 demo 故意不把 `gap` 绑定成 token（只做一次性字面量
读取），从而让自己的 light/dark 切换开销保持诚实的"纯色、零重排"。

## `load_theme()`：未知 token / 类型不匹配即失败

`dg::load_theme()`（`theme_loader.h:112`）不做任何静默降级：

- **未知 token 名**：JSON 里某个 key 在 `themes/schema.toml` 里根本不存在
  → `ThemeLoadStatus::kUnknownToken`，报出具体的 JSON key path（如
  `"variants.light.color.mystery"`）。
- **类型不匹配**：数字被用在需要颜色字符串的地方，或者 int token 出现在
  `variants` 里、color token 出现在 `base` 里 → `ThemeLoadStatus::kTypeMismatch`。
- **不支持的 schema_version**：目前只接受 `{1}`，绝不自动升级
  （`theme_loader.h:67-75`）。

`load_builtin_theme()` 走的是**和外部主题包完全相同**的这条加载路径——内置
主题不走捷径，`tools/check_consistency.py` 在 CI 时校验它覆盖了 schema 里
每一个 token。

## 外部主题包

`dg::ThemePackage`（`include/drawgui/theme/theme_package.h`）是给第三方主题
目录（`theme.json` + 资源树）用的加载器，把"这是不可信输入"这件事当真：

```cpp
dg::Expected<dg::ThemePackage, dg::ThemeLoadError> opened =
    dg::ThemePackage::open(package_dir);
dg::Expected<dg::Theme, dg::ThemeLoadError> loaded = opened.value().load_theme_json();
```

`open()` 在读任何资源内容之前就完成规范化路径（跟随符号链接、解析 `..`）
和资源树体量检查，四个上限常量（`theme_package.h:54-71`）：

| 常量 | 值 | 含义 |
|---|---|---|
| `kMaxThemeJsonBytes` | 1 MiB | `theme.json` 自身大小上限 |
| `kMaxResourceFileBytes` | 4 MiB | 单个资源文件大小上限 |
| `kMaxResourceFileCount` | 256 | 资源树文件数上限 |
| `kMaxTotalResourceBytes` | 32 MiB | 资源树总大小上限 |

`read_resource()`（`theme_package.h:114`）拒绝任何解析后落在包根目录之外
的路径——包括**包内符号链接指向包外**这种间接方式，检查针对的是**完全
解析后的路径**，不是原始字符串，所以裸 `../` 和通过符号链接的绕行都会被
挡下（`ThemeLoadStatus::kPathTraversal`）。符号链接**死循环**是
`kIoError`（根本无法解析出一个真实路径），不是崩溃或挂起。

**仅支持位图资源，不支持 SVG**（`theme_package.h:14-27`）：resources 被当作
字节交给调用方自己送去 `dg::ImageCatalog::decode()`；SVG 被明确声明拒绝，
理由是 SVG 解析器是一个远比 `mini_json.h`（~300 行、已被充分模糊测试）更大
的攻击面（外部引用、无界的路径/节点数、自身的递归文档结构）。

热重载：

```cpp
dg::Expected<dg::Theme, dg::ThemeLoadError> reloaded =
    dg::reload_theme_package(scene.package);
if (reloaded) {
  scene.theme = std::move(reloaded).value();
  scene.bindings.apply(scene.tree, scene.theme, scene.variant);
}
```

`reload_theme_package()`（`theme_package.h:131`）就是重新读一遍并重新校验
`theme.json`——没有增量 patch 机制，全部开销都在 `ThemeBindings::apply()`
这一步。

## Token id 是 ABI 契约

`themes/schema.toml` 里的 `id` 是 append-only 的：新 token 只能追加、绝不
重排、绝不复用旧 id（`schema.toml:20-23`）。`themes/token_ids.lock` 钉死了
这一点，防止某次改动意外重新编号已发布的 token。这意味着你可以安全地把
`DG_TOKEN_COLOR_SURFACE` 这样的常量硬编码进你自己的代码——它的数值在未来
版本中不会变。

## 边界与坑

- **主题切换与同一属性上飞行中的 transition 冲突**：如果 `AnimationEngine`
  正在对某个 (node, prop_id) 跑一个显式动画或隐式过渡，恰好这个属性又被
  `bind_token()` 绑定了，此时切换主题——`ThemeBindings::apply()` 会写一次
  新值，但**下一次 `tick()`** 会用动画自己保存的 `from`/`to` 重新算出插值
  并覆盖回去，主题切换的效果只在**一帧**内可见。这是两个系统互相不知道
  对方存在导致的已知缺陷，未修复。详见 `doc/limitations.md`。
- token 只有 color/int 二分法，没有第三种类型，也没有嵌套/表达式语法。
- `int` token 与 variant 无关；给它绑定属性会让每次主题切换都触发重排，
  即使该 token 的值从不随 variant 变化。
- 外部主题包只信资源字节，不解析 SVG。
- 未知 token 名或类型不匹配会让加载**直接失败**，不会静默跳过。

## 相关 example

- `examples/18_theme` — 内置主题、`$token` 绑定、light/dark 切换、颜色 vs
  整数 token 的开销对比。
- `examples/24_theme_package` — 外部主题包加载、热重载、路径穿越防护实测。
