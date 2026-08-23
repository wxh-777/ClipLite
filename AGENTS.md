# ClipLite 代理工作指南

## 项目目标

ClipLite 是面向 Windows 10 和 Windows 11 的原生剪贴板历史工具。项目优先级如下：

1. 剪贴板数据可靠性和用户数据安全
2. 界面文字、DPI 和图形质量
3. 后台低内存和低 CPU 占用
4. 快捷键和窗口交互稳定
5. 发布体积和部署便利性

最低运行版本为 Windows 10 1903，主要验证 Windows 10 22H2 和 Windows 11。项目不再以 Windows 7、Windows 8 或 Windows 8.1 兼容为目标。

## 技术边界

- 使用 C++17、CMake、Visual C++ 和 Win32 API。
- 自定义界面使用 Direct2D 1.1、DirectWrite 和 WIC。
- 使用 Direct2D 的硬件渲染路径，并允许系统级 WARP 软件渲染回退。
- 使用 Win32 原生窗口、菜单、托盘、文件对话框和必要的输入控件。
- 不引入 WebView、Electron、WinUI、XAML、Qt、SQLite 或大型 UI 框架。
- 不使用 DirectComposition；不创建常驻的大型 GPU 纹理、整窗截图或无界图片缓存。
- 本地历史继续使用追加式二进制存储、CRC、临时文件原子替换和可选 DPAPI。

Direct2D、DirectWrite、WIC、DWM 和 DPAPI 均视为 Windows 系统能力，不增加第三方运行时依赖。

## 当前基线

当前回退节点已提交并推送：`f506adf`，提交主题为“完善设置存储与快捷键生命周期”。

现有功能包括剪贴板捕获、历史存储、搜索、分类、置顶、粘贴、设置页、托盘、英文/简体中文和基础快捷键。Direct2D/DirectWrite 渲染迁移尚未开始，Win+V 拦截器仍需要独立重构和边界验证。

## 目录职责

- `src/main.cpp`：Win32 消息协调和业务状态；渲染核心、剪贴板监听、快捷键、生命周期及窗口资源已拆到独立模块，后续继续降低入口职责。
- `src/app_lifecycle.cpp`：COM/DPI 之外的单实例句柄生命周期。
- `src/clipboard_monitor.cpp`：剪贴板监听注册、注销和更新回调。
- `src/hotkey_manager.cpp`：RegisterHotKey 和独立 Win+V 低级钩子状态机。
- `src/render_context.cpp`：共享 Direct2D 1.1/DirectWrite/WIC Factory，窗口级 D3D11 设备、DXGI swap chain、DeviceContext 和渲染资源。
- `src/history_window.cpp`、`src/settings_window.cpp`：历史窗口和设置窗口的渲染资源边界。
- `src/clip_store.cpp`：追加式历史存储、CRC、重建、删除、搜索和 DPAPI 相关逻辑。
- `include/clip_store.h`：存储公共数据结构和接口。
- `tests/store_tests.cpp`：存储、损坏恢复、Unicode、DPAPI 和压力测试。
- `resources/clipLite.rc`：Windows 版本和程序资源。
- `tools/measure.ps1`：启动时间、Working Set、Private Bytes 和文件体积测量。
- `tools/stress-windows.ps1`：窗口生命周期和单实例压力测试。
- `tools/stress-clipboard.ps1`：剪贴板捕获压力测试。
- `packaging/package.ps1`：便携包、安装包和校验文件生成。
- `README.md`：面向用户的功能、限制、构建和数据说明。
- `PLAN.md`：当前技术改造计划、验收标准和阶段记录。
- `CHANGELOG.md`：面向发布的变更记录。
- `build-x64/`：唯一构建输出目录，禁止创建旁路构建目录。

目标模块结构为 `app_lifecycle`、`clipboard_monitor`、`hotkey_manager`、`render_context`、`history_window`、`settings_window` 和 `clip_store`。核心模块已建立，模块拆分未完成前不要继续向 `main.cpp` 添加大型独立功能块。

## 构建规则

始终复用 `build-x64/`：

```pwsh
cmake -S . -B build-x64 -A x64 -DBUILD_TESTING=ON
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure
```

可选验证：

```pwsh
powershell -ExecutionPolicy Bypass -File tools/measure.ps1
powershell -ExecutionPolicy Bypass -File tools/stress-windows.ps1 -Iterations 100
powershell -ExecutionPolicy Bypass -File tools/stress-clipboard.ps1 -Iterations 10000
powershell -ExecutionPolicy Bypass -File packaging/package.ps1
```

如果 `build-x64\Release\ClipLite.exe` 被占用，先确认进程路径属于本仓库，再按 PID 终止该进程；不得按进程名批量终止，不得创建新构建目录绕过占用。

每次修改源代码、资源或构建配置后，必须重新构建 Release，确保正式输出对应当前源码。修改文档不要求重复构建。

## 渲染规则

- Direct2D Factory 和 DirectWrite Factory 全进程共享。
- 每个自绘窗口独立管理 RenderTarget、DeviceContext、Bitmap 和文本资源。
- `EndDraw` 返回设备失效时释放窗口级资源并重建，不得继续使用失效对象。
- DirectWrite 负责所有用户可见的自绘文字、文本布局、字体回退和 DPI 适配。
- Direct2D 负责圆角、图标、路径、边框、阴影、透明混合和图片缩放。
- WIC 只解码可视区域需要的图片，缩略图缓存必须有明确硬上限。
- 透明窗口使用适合透明表面的抗锯齿模式；不透明文字按显示质量选择 ClearType 或灰度抗锯齿。
- 不保留旧的重复自绘后端。迁移完成后删除无调用实现和旧版原型文案。
- 使用 Windows 10/11 的 DPI API；多显示器和 DPI 切换必须重建窗口级尺寸和渲染资源。

## 快捷键规则

- `Alt+V`、设置和暂停快捷键使用 `RegisterHotKey`。
- `Win+V` 使用独立的 `WH_KEYBOARD_LL` 拦截器模块，不依赖设置窗口焦点。
- 拦截器必须区分 Win+V、普通 Win 键和 Win+其他组合，不得吞掉无关按键。
- 按键状态必须覆盖正常释放、异常顺序、窗口切换、钩子卸载和进程退出。
- 不通过注册表重映射 Win+V，不声称可以绕过 UAC、管理员窗口或安全桌面限制。
- 钩子安装失败、权限受限或系统快捷键冲突时，必须显示明确状态并保留 `Alt+V`。

## 存储和隐私规则

- 历史正文、`history.bin`、`settings.ini` 和日志禁止写入仓库。
- 内存只保存索引、预览、偏移、长度和必要的状态；完整正文按需读取。
- 任何记录必须检查魔数、版本、长度、类型、上限和 CRC。
- 重建、删除、重新加密使用临时文件并原子替换；失败时保留原文件。
- 日志不记录剪贴板正文、payload、密码、Token 或密钥。
- 存储测试必须使用 `%TEMP%` 下按进程隔离的测试目录，禁止访问真实用户历史。
- 粘贴到高权限或受保护窗口可能受 UIPI 限制，相关限制必须在文档和界面中说明。

## 性能目标

目标不是单一的 Working Set 数字，而是分别测量 Private Bytes、Working Set、Commit、GPU Dedicated Memory、GPU Shared Memory 和 CPU 时间。

- 后台空闲 Private Bytes：目标不超过 `4 MB`。
- 后台空闲 Working Set：目标不超过 `12 MB`。
- 打开设置或文本历史窗口 Private Bytes：目标不超过 `8 MB`。
- 打开设置或文本历史窗口 Working Set：目标不超过 `24 MB`。
- 图片预览允许短时峰值，但关闭窗口后必须回落到后台基线附近。
- 不加载全部历史正文，不启动无界队列，不创建常驻大图缓存。

## 测试和协作

- 新增存储行为时同步扩展 `tests/store_tests.cpp`。
- UI、渲染、窗口和快捷键改动必须完成 Release、CTest 和窗口生命周期压力测试。
- 渲染改动必须实际检查历史窗口、设置窗口、主题、DPI、图片和高对比度状态。
- 性能改动必须重新执行 `measure.ps1`，并记录 CPU、内存和 GPU 指标。
- 使用现有 4 空格缩进、C++17、ASCII 源文件和早返回风格。
- 句柄、渲染资源、文件、线程和钩子必须在所有失败路径释放。
- 更新用户可见行为时同步修改 `README.md` 和 `CHANGELOG.md`；更新阶段计划时同步修改 `PLAN.md`。
- 提交前检查 `git status`，不得提交构建产物、用户数据、日志或崩溃转储。
