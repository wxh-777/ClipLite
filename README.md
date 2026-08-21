# ClipLite

ClipLite 是一个仅面向 Windows 的轻量剪贴板历史工具。

设计目标：后台常驻内存优先，其次是交互流畅度，最后是程序体积。它不使用 WebView、Electron、WinUI 或大型第三方框架，历史内容保存在 `%LOCALAPPDATA%\\ClipLite`，启动时只加载轻量索引。

## 当前功能

- 双击程序后打开设置窗口，关闭窗口后继续驻留系统托盘
- `Alt+V` 打开剪贴板历史窗口
- 可选强制替换 `Win+V`
- 历史窗口出现在鼠标附近，并自动避开屏幕边缘
- 单击历史项目后粘贴到打开面板前的活动窗口
- 文本、HTML、文件列表、DIB 和 DIBV5 图片的捕获与恢复
- 文本搜索
- 历史窗口支持按全部、文本、图片、文件、HTML、其他和置顶直接点击筛选
- 历史窗口标题区域支持拖拽移动，筛选标签支持横向滚动，顶部清空按钮用于清除搜索
- 鼠标指针会按搜索、拖拽、筛选、历史项、置顶、删除和设置控件等场景切换，可点击区域提供 hover 反馈
- 设置页语言下拉框使用自绘圆角列表，支持选中态、hover 和展开/收起动画
- 设置页数字和多行输入框保留原生编辑能力，聚焦使用四周阴影，边框颜色保持稳定
- 剪贴板内容按格式自动分类，不需要手动维护分类名称
- 历史记录保存剪贴板来源进程，并在卡片中显示 Chrome、VS Code、Word 等友好名称
- 置顶、删除、清空历史
- Windows 原生单实例后台监听
- 程序、设置窗口、历史弹窗和系统托盘统一使用 ClipLite 工具图标
- 英文和简体中文基础界面
- 黑色和白色主题设置
- 可配置最大记录数、最大磁盘空间、保留天数、单条内容上限和暂停监听
- 支持按来源名称忽略应用，支持可选的敏感内容自动过期
- 可选使用 Windows DPAPI 按当前用户加密历史内容
- 追加式磁盘存储，不在内存中保存完整历史正文，当前格式为 v4 并兼容 v1/v2/v3

## 构建

需要 Windows、Visual Studio 2019 或更高版本、CMake 3.15 或更高版本。

```pwsh
cmake -S . -B build-x64 -A x64 -DBUILD_TESTING=ON
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure

# 可选：测量启动后的进程资源
powershell -ExecutionPolicy Bypass -File tools/measure.ps1

# 可选：重复打开和关闭历史窗口
powershell -ExecutionPolicy Bypass -File tools/stress-windows.ps1 -Iterations 100

# 可选：执行 10000 次文本剪贴板捕获压力测试，并在结束后恢复原文本剪贴板
powershell -ExecutionPolicy Bypass -File tools/stress-clipboard.ps1 -Iterations 10000
```

程序输出为 `build-x64/Release/ClipLite.exe`。

生成便携发布目录：

```pwsh
powershell -ExecutionPolicy Bypass -File packaging/package.ps1
```

## 数据位置

- 历史记录：`%LOCALAPPDATA%\\ClipLite\\history.bin`
- 设置：`%LOCALAPPDATA%\\ClipLite\\settings.ini`

ClipLite 不记录剪贴板正文日志。历史内容默认保存在当前用户目录；启用 DPAPI 后，历史 payload 只能由同一 Windows 用户恢复。清空历史会删除本地历史文件，暂停监听可用于临时避免保存敏感内容。敏感内容过期默认关闭，启用后只对包含明确 `password`、`token`、`api_key`、`secret` 或私钥标记的文本生效。

粘贴到管理员权限或受保护窗口可能受 Windows UIPI 限制；此时 ClipLite 不能绕过系统权限，用户需要在目标应用中手动粘贴。

## 快捷键

- `Alt+V`：打开历史窗口
- `ClipLite.exe --history`：启动并打开历史窗口
- `ClipLite.exe --settings`：启动并打开设置窗口
- `ClipLite.exe --exit`：退出已运行的 ClipLite 实例
- 可在设置中尝试启用 `Win+V`。如果 Windows 或其他程序已经注册该快捷键，ClipLite 会保留 `Alt+V`。
- `Enter`：粘贴当前项目
- 鼠标单击：粘贴当前项目
- `Delete`：删除当前项目
- `F10`：打开设置
- `Esc`：关闭历史窗口
- 系统托盘右键：打开历史、设置或退出
- 语言默认跟随 Windows 首选 UI 语言，也可以手动选择英文或简体中文
