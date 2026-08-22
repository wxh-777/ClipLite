# ClipLite 代理工作指南

## 项目概览

ClipLite 是仅支持 Windows 的原生剪贴板历史工具。项目使用 C++17、CMake、Visual C++ 和 Win32 API，目标是低常驻内存、可靠的剪贴板格式处理和小体积发布。

技术边界来自 `PLAN.md`：保持原生 Win32/GDI 和系统 DLL 方案，不引入大型 UI 框架、WebView、Electron、WinUI、XAML、Qt、SQLite 或常驻 GPU 渲染资源。修改前先确认是否会破坏低内存、追加式存储或 Windows 权限边界。

## 目录结构

- `src/main.cpp`：Win32 入口、消息循环、单实例、托盘、快捷键、剪贴板监听、历史窗口、设置窗口和粘贴交互。
- `src/clip_store.cpp`：本地历史存储实现；负责追加式 v4 二进制记录、CRC 校验、截断恢复、删除、重建、搜索和 DPAPI 加密。
- `include/clip_store.h`：`ClipItem`、`ClipType`、`ClipStore` 以及数据目录和哈希函数的公共声明。
- `tests/store_tests.cpp`：单个原生测试程序，覆盖存储基本操作、格式恢复、Unicode 搜索、截断/CRC 损坏、DPAPI 和 10000 条压力场景。
- `resources/clipLite.rc`：Windows 文件版本和产品资源，当前版本由 CMake 项目版本和资源文件共同体现。
- `tools/measure.ps1`：启动时间、Working Set、Private Bytes 和可执行文件体积测量。
- `tools/stress-windows.ps1`：反复打开/关闭历史窗口的 Windows 生命周期压力脚本。
- `packaging/package.ps1`：复制 Release 可执行文件、README、CHANGELOG 并生成 SHA-256 校验文件。
- `README.md`：用户功能、构建、数据位置和快捷键说明。
- `PLAN.md`：阶段目标、已完成项、验证记录和未完成风险；完成阶段时应同步更新。
- `CHANGELOG.md`：面向发布的变更和验证记录。
- `build-x64/`：唯一允许使用的 CMake 构建输出目录，不是源代码；由 `.gitignore` 排除，不要直接修改生成文件。禁止为验证、绕过文件占用或其他原因新建 `build-*`、`build/`、`out/` 等旁路目录。

## 环境与构建

要求 Windows、Visual Studio 2019 或更高版本、CMake 3.15 或更高版本，并使用 x64 生成器。推荐从仓库根目录执行命令。

构建目录规则：始终复用 `build-x64/`。如果 `build-x64\Release\ClipLite.exe` 或相关目标文件被占用，先确认占用进程对应本仓库的 ClipLite 实例，关闭该进程后再重新构建；不得通过创建新的构建目录绕过文件占用。若确认是用户正在使用的实例，先提示用户，不得强制终止。

输出同步规则：每次修改源代码、资源或构建配置后，都必须使用 `build-x64/` 重新构建 Release，并确保 `build-x64\Release\ClipLite.exe` 是当前源码对应的最新输出程序；不能只完成编译验证而留下旧的正式输出文件。

首次配置测试构建：

```pwsh
cmake -S . -B build-x64 -A x64 -DBUILD_TESTING=ON
```

构建 Release：

```pwsh
cmake --build build-x64 --config Release
```

构建 Debug：

```pwsh
cmake --build build-x64 --config Debug
```

仅构建主程序：

```pwsh
cmake --build build-x64 --config Release --target ClipLite
```

仅构建测试程序：

```pwsh
cmake --build build-x64 --config Release --target ClipLiteStoreTests
```

运行全部 CTest：

```pwsh
ctest --test-dir build-x64 -C Release --output-on-failure
```

运行存储测试这个单独的 CTest 目标：

```pwsh
ctest --test-dir build-x64 -C Release -R "^ClipLiteStore$" --output-on-failure
```

也可以直接运行测试可执行文件：

```pwsh
.\build-x64\Release\ClipLiteStoreTests.exe
```

当前测试不是 GoogleTest/Catch2 等框架；`store_tests.cpp` 是一个 `main()`，通过返回码区分失败。因此没有按单个断言或函数筛选的命令。需要定位某个场景时，先运行上述单目标，再根据返回码和源码中的检查编号调试；新增测试应优先保持可独立重复运行。

生成便携包：

```pwsh
powershell -ExecutionPolicy Bypass -File packaging/package.ps1
```

资源与生命周期测量：

```pwsh
powershell -ExecutionPolicy Bypass -File tools/measure.ps1
powershell -ExecutionPolicy Bypass -File tools/stress-windows.ps1 -Iterations 100
```

脚本默认查找 `build-x64\Release\ClipLite.exe`，可用 `-ExePath` 覆盖。脚本会启动并终止测试进程，避免在用户正在使用的实例上运行。

## Lint 与格式化

仓库没有配置 `clang-format`、`clang-tidy`、`cppcheck` 或其他 lint 命令。提交前至少完成 Release 构建和 CTest；可用 MSVC 的 `/W4` 编译警告检查问题。不要擅自引入格式化工具配置或大范围重排无关代码。

## C++ 代码风格

- 使用 C++17；遵循现有 4 空格缩进、K&R 花括号、每行适度换行和 ASCII 源文件风格。
- 头文件使用现有 include guard；标准库头在 Windows 头之后按现有文件习惯排列，项目头使用双引号。
- include 只放实际依赖；新增声明放 `include/clip_store.h`，实现放对应 `src/*.cpp`，不要在多个文件复制同一逻辑。
- 类型和类使用 `PascalCase`，函数和变量使用 `camelCase`，私有成员使用尾随下划线，例如 `maxItems_`；常量使用 `kPascalCase`。
- 优先使用 `std::size_t`、固定宽度整数和已有的 `enum class`；Windows API 边界按 API 要求使用 `DWORD`、`HWND` 等类型。
- 避免裸拥有指针和不必要的全局状态；Windows 句柄、GDI 对象、文件和加密缓冲区必须在所有失败路径释放。
- 用 `const`、引用和移动语义表达所有权与不可变性；不要为了“优化”默认加入 `std::move`、缓存或线程。
- 使用早返回处理参数无效、索引越界、文件打不开、API 失败和校验失败；公共存储操作返回 `bool`，不要跨现有边界抛异常。
- 文件格式字段必须进行魔数、版本、长度、上限和 CRC 检查；外部或剪贴板数据不能直接信任。
- 文本转换明确 UTF-8/UTF-16 边界；不要用本地代码页替代已有的 Unicode 路径。
- 当前仍处于开发阶段，磁盘格式只维护当前 v4；不要为尚未发布的旧格式增加兼容读取、迁移或重复数据结构。格式变更必须补充损坏、截断和重启读取测试。
- 出来的界面禁止有锯齿出现
- 所有用户可见的圆点、圆角、圆弧、旋钮、图标和装饰线必须使用 GDI+ 抗锯齿绘制；禁止在最终 UI 路径使用原生 `Ellipse`、`Arc`、`RoundRect`、`Polygon`、`MoveToEx`/`LineTo` 手绘这些元素。
- UI 绘制替换后必须删除旧的绘制函数、旧版原型和无调用代码；不得保留会被误用的重复实现。兼容回退路径也必须经过实际截图验证，不能以“当前通常走 GDI+”代替检查。

## 设置页交互与性能约束

- 设置页不提供“保存”按钮；开关、下拉框和输入框修改后必须自动同步到内存和设置文件，连续输入使用短延迟合并写入，关闭窗口前必须立即刷新未写入变更。
- 设置页滚动不得触发窗口界面闪烁；优先使用双缓冲、禁止无意义的背景擦除，滚动时不得反复销毁/创建或无条件隐藏/显示子控件。
- 固定标题区必须遮挡正文；任何开关、输入框和按钮滚动到标题区时应隐藏或裁剪，不得露出标题区上方。
- 设置窗口右侧和下侧不得出现非设计稿中的多余线条；完成 UI 修改后必须实际截图检查客户区边界、圆角和滚动边界。
- 完成 UI 修改后必须实际检查设置页、历史弹窗和所有主题/强调色状态；重点放大检查曲线边缘、选中环、开关旋钮和导航图标，不得只凭代码判断无锯齿。
- 性能、用户体验和内存占用优先于装饰效果；保持原生控件数量有限，不为自绘历史列表逐项创建窗口，不增加常驻缓存或 GPU 资源。
- 动态颜色和状态变化使用短时缓动过渡；优先即时颜色插值和现有窗口定时器，不保留整窗截图或新增常驻动画缓存。

## Windows 与存储约束

- 历史数据位于 `%LOCALAPPDATA%\ClipLite`；不要把用户剪贴板正文、`history.bin` 或 `settings.ini` 写入仓库。
- 存储只在内存保留元数据、预览、偏移和长度；图片及完整 payload 应按需从磁盘读取。
- 重建或重新加密使用临时文件后原子替换；失败时清理临时文件并保留原文件可恢复性。
- DPAPI 加密默认关闭，启用时必须确保同一 Windows 用户可恢复；不要记录敏感 payload 到日志。
- 剪贴板打开使用有限重试；粘贴到高权限或受保护窗口受 UIPI 限制，不能声称应用可以绕过系统权限。
- 新增 UI、快捷键、钩子或窗口资源时，检查 Explorer 重启、失焦关闭、退出清理、DPI 和多显示器行为。

## 测试与协作规则

- 新增存储行为时同步扩展 `tests/store_tests.cpp`，覆盖成功路径、边界值、损坏输入和重启恢复。
- 测试禁止访问、修改或清理当前用户的 `%LOCALAPPDATA%\ClipLite` 数据。存储测试必须设置 `CLIPLITE_TEST_DATA_DIR`，使用 `%TEMP%` 下按进程隔离的开发测试目录，并在测试结束删除该目录。
- 未经用户明确授权，不得用真实本地历史作为测试数据；需要验证剪贴板行为时使用隔离测试目录或单独的开发数据文件。
- UI 和快捷键改动除构建/CTest 外，必要时运行 `stress-windows.ps1` 并记录结果；资源目标变化时运行 `measure.ps1`。
- 提交前检查 `git status`，不要提交 `build*/`、`out/`、用户数据、崩溃转储或日志。
- 保持变更聚焦；更新用户可见行为时同步修改 `README.md` 和 `CHANGELOG.md`，更新阶段状态或验证时同步修改 `PLAN.md`。
- 不要修改生成目录来“修复”问题；应修改 CMake、源代码、资源或脚本后重新配置/构建。
