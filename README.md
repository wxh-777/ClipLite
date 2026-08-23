# ClipLite

ClipLite 是面向 Windows 10 和 Windows 11 的原生剪贴板历史工具，最低运行版本为 Windows 10 1903，主要验证 Windows 10 22H2 和 Windows 11。

## 产品目标

- 后台低内存、低 CPU 占用。
- 剪贴板数据可靠保存，支持损坏恢复和按需读取。
- Direct2D/DirectWrite 提供清晰文字、稳定 DPI 和高质量图形。
- 历史窗口和设置窗口保持原生 Win32 的启动速度与交互响应。
- 不使用 WebView、Electron、WinUI、XAML、Qt、SQLite 或大型 UI 框架。

## 当前功能

- Windows 原生单实例后台监听和系统托盘。
- `Alt+V` 打开剪贴板历史窗口。
- 可选 Win+V 拦截模式，当前仍处于重构和边界验证阶段。
- 历史窗口出现在鼠标附近，并自动避开屏幕边缘。
- 文本、HTML、文件列表、DIB 和 DIBV5 图片的捕获与恢复。
- 文本搜索、类型筛选、置顶、删除、清空和按需图片预览。
- 右键选择自动粘贴、纯文本粘贴或富文本粘贴。
- 设置窗口支持通用、快捷键、存储管理、安全与隐私、关于 ClipLite 页面。
- 设置自动保存，数字项支持失焦校验和键盘导航。
- 支持英文、简体中文、自动语言、浅色、深色和高对比度颜色路径。
- 支持最大记录数、磁盘空间、保留天数、单条内容上限和暂停监听。
- 支持文本、图片、文件分类限制与独立清理。
- 支持来源应用过滤、敏感内容过期和 Windows DPAPI 用户级加密。
- 支持自定义缓存目录、历史迁移和诊断日志。

## 渲染改造方向

当前回退节点为 `f506adf`。后续改造目标是：

- Direct2D 1.1 负责圆角、图标、路径、阴影、透明混合和图片缩放。
- DirectWrite 负责自绘文字、中文字体回退、文本布局和高 DPI。
- WIC 负责图片解码和格式转换。
- WARP 作为 Direct2D 的系统软件渲染回退。
- Direct2D/DirectWrite Factory 全进程共享，窗口资源按需创建和释放。
- 不创建常驻整窗截图、大型 GPU 纹理或无界缩略图缓存。

当前主窗口已经使用 Direct2D/DirectWrite 绘制，图片可见行使用 WIC 按需缩放，Win+V 使用独立低级钩子模块。设置页输入、开关、快捷键、按钮和语言选择使用标准 Win32 控件；多版本实机验收仍在进行，`PLAN.md` 是实施计划和验收依据。

## 发布方式

- 普通安装包：支持选择安装目录、开始菜单/桌面快捷方式和 Windows 卸载入口。
- 便携包：复制整个目录即可迁移，数据保存在包内 `data\` 目录。
- `packaging/package.ps1` 生成便携包；检测到 Inno Setup 时同时生成安装包。

## 构建

需要 Windows、Visual Studio 2019 或更高版本、CMake 3.15 或更高版本，并使用 x64 生成器。

```pwsh
cmake -S . -B build-x64 -A x64 -DBUILD_TESTING=ON
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure
```

性能和压力验证：

```pwsh
powershell -ExecutionPolicy Bypass -File tools/measure.ps1
powershell -ExecutionPolicy Bypass -File tools/stress-windows.ps1 -Iterations 100
powershell -ExecutionPolicy Bypass -File tools/stress-clipboard.ps1 -Iterations 10000
```

程序输出为 `build-x64/Release/ClipLite.exe`。

## 数据和隐私

- 普通安装模式历史：`%LOCALAPPDATA%\ClipLite\history.bin`
- 普通安装模式设置：`%LOCALAPPDATA%\ClipLite\settings.ini`
- 诊断日志：`%LOCALAPPDATA%\ClipLite\cliplite.log`
- 便携模式数据：便携目录下的 `data\`
- 自定义缓存目录设置后，历史文件和临时文件位于自定义目录。

ClipLite 不把剪贴板正文写入诊断日志。启用 DPAPI 后，历史内容只能由同一 Windows 用户恢复。敏感内容过期默认关闭，启用后只处理明确的密码、Token、API key、secret 或私钥标记。

粘贴到管理员权限、任务管理器或受保护窗口可能受到 Windows UIPI 限制，ClipLite 不绕过系统权限。Win+V 低级钩子也不能保证覆盖安全桌面和所有高权限窗口。

## 快捷键

- `Alt+V`：打开历史窗口。
- `ClipLite.exe --history`：启动并打开历史窗口。
- `ClipLite.exe --settings`：启动并打开设置窗口。
- `ClipLite.exe --exit`：退出已运行实例。
- `Enter`：粘贴当前项目。
- 鼠标单击：粘贴当前项目。
- `Ctrl+Shift+V`：粘贴为纯文本。
- `Ctrl+Shift+R`：粘贴为富文本。
- `Delete`：删除当前项目。
- `F10`：打开设置。
- `Ctrl+0`：清除历史筛选。
- `Esc`：关闭历史窗口。

Win+V 是否启用、拦截状态和权限限制将在快捷键设置页中明确显示。
