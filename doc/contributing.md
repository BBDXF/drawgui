# 贡献指南

## clone 之后第一件事

```sh
git config core.hooksPath .githooks
```

`.git/hooks/` 不在版本控制里，所以一个 hook 不能随 clone 一起带过来；这条命令
就是让 git 改指向被跟踪的 `.githooks/` 目录。这只是一条命令，也正因为如此很
容易被漏掉——所以它启用的检查并不是唯一一道防线（见下文）。

`.githooks/pre-commit` 会拒绝作者或提交者身份不是本仓库约定身份的提交。这个
检查的存在是因为：一台机器上全局的 `user.email` 会被任何 clone 无声继承，
而这个项目已经为此付过一次代价——某个雇主邮箱一路混进了 185 个提交中的
184 个，最后是靠重写全部提交才清理干净的。

客户端 hook 只是护栏，不是强制关卡——`git commit --no-verify` 能跳过它，而
从没跑过上面那条命令的人则从来没装上它。真正起作用的是
`.github/workflows/ci.yml` 里的 `identity` job：它重新读取每个被推送提交的
作者和提交者身份，提交者本人无法跳过这一关。

然后安装锁定版本的格式化工具，因为 CI 会强制检查：

```sh
pip install "clang-format==18.1.3"     # 或者: uv tool install "clang-format==18.1.3"
clang-format --style=file -i <你改动的文件>
```

**必须从 PyPI 安装，不要用发行版自带的包，并且要精确到这个 patch 版本。**
两个条件都重要，且都不直观：

- clang-format 不同大版本之间在一些具体写法上会有分歧：21 版把
  pointer-to-member 格式化成 `int C::* m`，18 版写成 `int C::*m`。
- 同大版本下的 patch 版本之间也会有分歧：18.1.3 会把
  `<< "a" << "b"` 保持在一行，18.1.8 会把它拆开。只锁定 `18` 是不够的——
  这条路试过，CI 依然拒绝了本地能通过的文件。
- 发行版本身也给不了你想要的版本。CI 跑的 ubuntu-24.04 打包的是 18.1.3，
  更新的 Ubuntu 打包的是 18.1.8，apt 没有办法指定要另一个版本。PyPI 的
  wheel 在所有地方都是同一份二进制，这是贡献者和 CI 能达成一致的唯一原因。

## Skia 依赖

Skia 通过 `cmake/FetchSkia2.cmake` + `find_package(skia2)`/`skia2::skia2`
以 `BBDXF/libskia2` 的形式引入（P7 阶段 7-1 切片；取代了旧的、基于
rust-skia 的 `cmake/FetchSkia.cmake`）。完整的切换记录——验证过什么、
golden-image 的风险评估、design.md §12/§5.10.5/§5.13.6 的决定、以及
具体测量数据——记录在被移除的开发切片文档中，见 git 历史。

**新增系统前置依赖：`libfontconfig1-dev`。** `skia2Config.cmake` 在 Linux
上链接时需要 `libfontconfig` 及其头文件（Skia 自己的 `BUILD.gn` 从不
vendor fontconfig）。这只是**构建期**依赖——`src/render/font_catalog.cpp`
仍然从不调用 `SkFontMgr_New_FontConfig`，所以没有任何 drawgui 产出的
二进制在运行期依赖 `libfontconfig`（用 `ldd` 验证过；测量过程记录在被移除
的开发切片文档中，见 git 历史）。在一台
全新机器上：

```sh
sudo apt-get install -y libfontconfig1-dev
```

## 新增一个属性

每个布局/视觉属性都定义在 `props/drawgui.props.toml` 这个唯一真源里
（design.md 第 5.8 节决定 5）。任何"知道某个属性存在"的代码都不应该在别处
手写第二份。

1. 在文件**末尾**追加一个 `[[property]]` 表。id 取自 `meta.next_id`，
   然后把 `meta.next_id` 加一。条目按 id 升序排列，所以追加永远只在文件
   底部产生 diff。
2. 重新生成 C++ 代码：

   ```sh
   python3 tools/gen_props.py
   ```

   这会重写 `include/drawgui/render/prop_ids.generated.h` 和
   `src/render/prop_dispatch.generated.inc`。**永远不要**手改这两个文件。
3. 把新 id 记录到 ABI 台账里：

   ```sh
   python3 tools/prop_lock.py --write
   ```

   这会往 `props/prop_ids.lock` 追加一行。这一步不是可选的：第 2 步已经
   让 `DG_PROP_<NEW>` 成为一个可以被消费方编译进去的真实常量，在这行记录
   落地之前，这个新 id 处于"已生效但无人守护"的状态。缺了这一行，
   `prop_lock.py --check` 会失败。
4. 把 TOML、两个生成文件、锁文件**一起**提交。新增一个属性是一次跨两个
   文件的源码改动——`props/drawgui.props.toml` 和 `props/prop_ids.lock`——
   两者应属于同一个提交。任何一次 checkout 都必须让这两个工具的 `--check`
   同时通过。

## 新增一个主题 token

`themes/schema.toml` 是同一形状在更上一层的复刻，只是把 `prop_id` 换成了
`token_id`（为什么这是第二个独立的小工具、而不是
`gen_props.py` 的扩展，理由记录在被移除的开发切片文档中，见 git 历史）。

1. 在文件末尾追加一个 `[[token]]` 表，名字用点号分隔（如
   `color.something`、`radius.something`），`id` 取自 `meta.next_id`
   并递增。
2. 重新生成：

   ```sh
   python3 tools/gen_theme.py
   ```

   这会重写 `include/drawgui/theme/token_ids.generated.h`、
   `src/theme/token_table.generated.inc` 和
   `doc/reference/theme-tokens.generated.md`。
3. 记录 id：

   ```sh
   python3 tools/theme_lock.py --write
   ```

   往 `themes/token_ids.lock` 追加一行；缺了这行 `--check` 会失败，
   原因和 `prop_lock.py --check` 一样。
4. 把新 token 的值加到 `themes/builtin/theme.json`——`int` 类型的 token
   放进 `"base"`，`color` 类型的 token 要同时放进 `"variants.light"` 和
   `"variants.dark"`——然后校验覆盖是否完整：

   ```sh
   python3 tools/check_consistency.py
   ```

   这道检查在 `ctest` 里注册为 `theme.consistency`，CI 通过 `build` job
   的 `ctest` 跑它；如果 schema 里的某个 token 没有在内建主题里
   给出对应的值，这一步会失败。
5. 把 schema TOML、三个生成文件、锁文件、更新后的 `theme.json`，以及
   第 2 步同一次 `python3 tools/gen_theme.py` 调用顺带重新生成的内建主题
   嵌入文件（`include/drawgui/theme/builtin_theme.generated.h`）一起提交。

## 为什么 id 永不复用

一个 `prop_id` 是跨越 C ABI 边界的 `uint16_t`。消费方会把它编译进自己的
二进制里，所以这些数字是一份契约：只能追加，不能复用，不能重新编号。
只有 MAJOR 版本才可能打破它（design.md 第 5.8 节决定 4）。

有两道检查守着这条规则，它们回答的是两个不同的问题：

| 检查 | 回答的问题 | 命令 |
| --- | --- | --- |
| `props.no_drift` | 生成文件是否和 TOML 一致？ | `python3 tools/gen_props.py --check` |
| `props.abi_lock` | TOML 是否仍然遵守它已经发布过的那些 id？ | `python3 tools/prop_lock.py --check` |

只有第一道检查是不够的。把 `width` 的 id 从 1 改成 7，
`gen_props.py` 会毫无异议地重新生成，而每一个已经编译好的消费方都会开始
写错字段。`props/prop_ids.lock` 保存的是已提交的"名字→id"映射，第二个
问题的答案就靠它。

`prop_lock.py --check` 恰好在以下四种情况下失败：

- 某个 id 被**重新编号**——一个已锁定的名字现在对应了不同的 id
- 某个属性被**改名**——一个已锁定的 id 现在对应了不同的名字
- 一个已锁定的属性被**删除**——它的 id 必须被永久占用下去
- 一次追加**没有被记录**——TOML 里有一个锁文件从未见过的属性

最后一条不是在限制追加。追加属于 MINOR 变更，永远允许；被拒绝的是"追加了
却没有记下来"。`cmake --build build` 单靠 TOML 就能重新生成头文件，所以
一个新属性在被添加的瞬间就是可编译的，但在它进入锁文件之前没有任何东西
守护它的 id。一个"只有在有人记得跑 `--write` 之后才开始生效"的守护机制
存在一个滞后窗口，而"依赖有人记得"正是 stonegui 事后复盘记录下来的那类
失败。在同一个提交里跑 `--write` 就能把这个窗口关掉。

## 锁检查失败时怎么办

**不要去改 `props/prop_ids.lock`。** 这个检查抱怨的不是锁文件，它是在报告
`props/drawgui.props.toml` 违反了自己的契约。编辑台账并不能修复这个违反，
只会把它藏起来不让下一个 reviewer 看见——而且 `prop_lock.py --write`
根本不会替你做这件事。

错误信息会指出具体是哪个属性、锁定的值是什么、新值是什么。去修 TOML：

- 重新编号了——把 id 改回锁定的那个。想要一个新属性，就用
  `meta.next_id` 追加一个新的。
- 改名了——把名字改回锁定的那个。如果确实想要一个名字不同的属性，
  追加一个新的；旧 id 保留它原来的名字。
- 删除了——把条目加回去。退役一个属性不算"清理"。
- 未记录——运行 `python3 tools/prop_lock.py --write`，把锁文件的改动
  和 TOML 的改动一起提交。这是唯一一种"修复方式就是更新锁文件"的情况，
  而且 `--write` 会替你完成——你依然从不需要手改它。

如果确实需要打破一条已锁定的契约，那是一次 MAJOR 版本变更，应该作为一个
被明确评审、说明理由的提交落地——而不是悄悄改一下锁文件。

## 在 WSL 下用 GPU 加速跑 GUI 示例

WSL 下 `/dev/dri` 不存在，看起来像"没有 GPU"，但其实不是——完整的测量过程
记录在被移除的开发切片文档中，见 git 历史。WSL 通过 `/dev/dxg` 和 Mesa 的 D3D12 驱动暴露
GPU，需要用环境变量选中它：

```sh
GALLIUM_DRIVER=d3d12 ./build/examples/<gui_example>
```

不设这个变量的话，Mesa 会无声地退回到 `llvmpipe` 软件光栅器。没有任何
报错或警告——示例照样能跑，只是跑在 CPU 路径而不是 GPU 路径上——所以一次
没设这个变量跑出来的性能测试或视觉检查，测的是错的东西,却不会告诉你。

## 本地跑完整测试

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest` 会把属性/主题/ABI 的相关检查（`props.*`、`theme.*`、`abi.*`）和
golden-image 测试一起跑掉。CI 的 `build` job 跑的就是这三条命令；另有一个
不需要构建的 `generated` job 直接跑属性和快捷键的 `--check`/selftest 脚本，
所以属性或快捷键方面过期或破坏契约的提交不必等构建就会失败。

## 跑 clang-tidy（仅本地——已不是 CI 关卡）

`clang-tidy` 这个 CI job 已被移除；`design.md` 第 7.1 节记录了这个决定、
它的代价、以及如何正确恢复它。`.clang-tidy` 本身还在，所以检查可以按需
手动跑：

```sh
CC=clang CXX=clang++ cmake -B build-tidy -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tidy --target drawgui_props_generate
python3 - <<'PY' > /tmp/tidy-files.txt
import json, os
root = os.getcwd()
for e in json.load(open("build-tidy/compile_commands.json")):
    f = e["file"]
    if f.startswith(root) and "/third_party/" not in f and "generated" not in f:
        print(f)
PY
clang-tidy -p build-tidy $(cat /tmp/tidy-files.txt)
```

像这样从 `compile_commands.json` 推导文件列表是故意的，也是旧 CI job
本应该做的事：它那份手工维护的列表已经漂移到 187 条，而实际的一方
翻译单元有 195 个，也就是说有 8 个文件从来没被这道"看起来很完整"的
关卡检查过。

预期会有 findings。现在没有任何东西强制这个检查，所以这棵代码树不保证
在你手头这个 clang-tidy 版本下是干净的——而且不同版本之间本身就会有分歧,
这也是这道关卡被移除的另一半原因。

## 跑 sanitizer 套件

design.md 第 7 节把 ASan + UBSan 套在整个测试套件之上,作为约束 C4 的一部分。
这是一个独立的构建目录,不是普通构建上的一个开关:

```sh
cmake -B build-san -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDG_SANITIZE=ON
cmake --build build-san
ctest --test-dir build-san --output-on-failure
```

`DG_SANITIZE` 默认关闭。插桩后的二进制不是最终发布的产物,运行速度也会
慢上好几倍,所以这是一次刻意的额外运行,而不是日常那次。CI 有一个专门的
`sanitize` job,用 gcc 跑一遍上面这套流程。

推荐用 Debug 构建类型。UBSan 的检查是在优化之前插入的,优化器可能会删掉
它能证明不可达的那部分,所以 `-O0` 能看到的最多。一个经过 sanitize 的
Release 构建依然有用,依然带着行号,因为 `DG_SANITIZE` 无论
`CMAKE_BUILD_TYPE` 是什么都会加上 `-g`。

这些 flag 都挂在根 `CMakeLists.txt` 里的 `drawgui_sanitizers` 这个
interface target 上,和 `drawgui_warnings` 挨着,只应用于第一方 target。
Skia 是一个没有插桩的预编译归档文件;ASan 设计上就是要和未插桩代码链接
在一起的,所以这能工作,但也意味着 `libskia.a` 内部的缺陷在这里是不可见的。

编译行和链接行拿到的是同一份 flag 列表。一个用 `-fsanitize=address`
编译、却没有用它链接的 target 会因为未定义的 `__asan_*` 符号而失败——
读起来像一个莫名其妙的链接错误,其实就是这个配置问题本身。

## 读懂一次 sanitizer 失败

`ctest --output-on-failure` 会把报告直接打印出来。有两种形状。

**ASan** 第一行点名故障类型,然后给出两条堆栈——出问题的访问发生在哪里,
它碰到的那块内存又是从哪来的:

```
==33933==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x...
READ of size 1 at 0x... thread T0
    #0 ... in dg::RasterSurface::encode_png() const .../src/graphics/raster_surface.cpp:76
    #1 ... in render_scene .../tests/golden/golden_test.cpp:92
...
0x... is located 8 bytes after 756-byte region [0x...,0x...)
allocated by thread T0 here:
    #0 ... in operator new(unsigned long)
    #7 ... in dg::RasterSurface::encode_png() const .../src/graphics/raster_surface.cpp:73
```

先看第二条堆栈。"8 bytes after 一块在第 73 行分配的 756 字节区域"
指明了是哪块缓冲区;第一条堆栈的第 `#0` 帧指明了是哪次访问。下面那堆
shadow-byte dump 很少用得上。

**UBSan** 是一行说明操作和涉及的值,后面跟一条堆栈:

```
.../src/graphics/raster_surface.cpp:67:17: runtime error: signed integer
overflow: 320 * 2147483647 cannot be represented in type 'int'
    #0 ... in dg::RasterSurface::encode_png() const .../raster_surface.cpp:67
    #1 ... in render_scene .../tests/golden/golden_test.cpp:92
```

这条堆栈之所以在,是因为 `tests/CMakeLists.txt` 给插桩过的 CTest 条目
设置了 `UBSAN_OPTIONS=print_stacktrace=1`。没有它,UBSan 只打印第一行,
什么都不再多说——只点明了是哪个表达式,却不说是哪个调用方喂进了这个
坏值。在 `ctest` 之外手动跑一个测试二进制拿到的就是这个精简版本,所以
需要自己传这个变量:

```sh
UBSAN_OPTIONS=print_stacktrace=1 ./build-san/tests/drawgui_unit_test
```

**一次 UBSan 报告总是会让测试失败。** flag 集合里带着
`-fno-sanitize-recover=undefined` 正是为了这个。没有它,UBSan 打印同样的
诊断信息,然后让程序继续跑下去,进程退出码依然是 0——于是 CI 在存在
未定义行为的代码上依然保持绿色。在两个编译器上都测量过:同一个溢出,
不带这个 flag 退出码是 0,带上是 1。design.md 第 5.4.7 节和第 5.17.5 节
都选择了"大声失败"而不是"悄悄地错着",一个只会警告的 sanitizer 恰恰是
反过来的。

两个编译器之间有两处外观上的差异,都不影响结论:clang 会在 UBSan 报告末尾
多加一行 `SUMMARY:`,gcc 不会;clang 的堆栈除了行号还带列号。

## sanitizer 配置里刻意关闭/没有关闭的东西

有一项 UBSan 子检查是关闭的:**`vptr`**。关掉它是因为它在这里根本跑不通,
而不是因为它太吵。`-fsanitize=vptr` 在每次多态解引用时都会引用一个
`typeinfo for T`,而预编译的 Skia 归档文件是不带 RTTI 编译的,所以一次
经过 sanitize 的链接会直接失败,报
`undefined reference to 'typeinfo for SkCanvas'`。第一方代码里没有任何
虚函数、`dynamic_cast` 或 `typeid`——design.md 第 5.15.3 节把渲染对象的
状态存成纯字段——所以这项检查在我们自己的代码里根本没有可检查的东西。
如果 drawgui 未来长出了自己的多态类型,这一点需要重新考虑。

除此之外没有关闭任何东西,尤其是以下两项通常会被关掉的检查,在这里是
**开着的**:

| 检查 | 状态 | 原因 |
| --- | --- | --- |
| `detect_leaks`(LeakSanitizer) | 开 | 它是武装状态的——针对一个故意造出来的泄漏验证过——目前没有报告任何东西。Skia 惰性注册的 PNG 解码器和它其他一些进程生命周期的状态在退出时依然可达,所以 LSan 不会为此报警。 |
| `detect_container_overflow` | 开 | 关掉它的常见理由是 `std::vector` 跨越进了未插桩的代码,而这个项目确实这么做了——`golden_image.cpp` 把 `pixels.data()` 直接递给 Skia。这是测量出来的,不是假设的:libstdc++ 15 把它的容器标注功能挡在 `_GLIBCXX_SANITIZE_STD_ALLOCATOR` 这个宏后面,而这个构建没有定义它,一次故意读到 vector 的 `[size, capacity)` 富余区间的操作在两个编译器下都没有被报告出来。 |

如果未来某个工具链真的标注了容器,这个边界穿越就会开始产生误报,修法是给
`tests/CMakeLists.txt` 里插桩测试已经设置的 `ENVIRONMENT` 属性追加
`ASAN_OPTIONS=detect_container_overflow=0`——而不是加一个抑制文件,也不是
把 ASan 整个关掉。

目前没有任何抑制文件,因为没有什么真的需要被抑制。如果将来确实需要一个,
它只能指向第三方路径。把 `src/`、`include/` 或 `tests/` 里的发现藏在
一个抑制规则后面,就违背了跑这套检查的意义本身——那个发现本身就是产出。
