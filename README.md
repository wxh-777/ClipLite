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
- 置顶、删除、清空历史
- Windows 原生单实例后台监听
- 英文和简体中文基础界面
- 黑色和白色主题设置
- 可配置最大记录数、最大磁盘空间、保留天数和暂停监听
- 可选使用 Windows DPAPI 按当前用户加密历史内容
- 追加式磁盘存储，不在内存中保存完整历史正文

## 构建

需要 Windows、Visual Studio 2019 或更高版本、CMake 3.15 或更高版本。

```pwsh
cmake -S . -B build-x64 -A x64 -DBUILD_TESTING=ON
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure

# 可选：测量启动后的进程资源
powershell -ExecutionPolicy Bypass -File tools/measure.ps1
```

程序输出为 `build-x64/Release/ClipLite.exe`。

生成便携发布目录：

```pwsh
powershell -ExecutionPolicy Bypass -File packaging/package.ps1
```

## 数据位置

- 历史记录：`%LOCALAPPDATA%\\ClipLite\\history.bin`
- 设置：`%LOCALAPPDATA%\\ClipLite\\settings.ini`

## 快捷键

- `Alt+V`：打开历史窗口
- 可在设置中尝试启用 `Win+V`。如果 Windows 或其他程序已经注册该快捷键，ClipLite 会保留 `Alt+V`。
- `Enter`：粘贴当前项目
- 鼠标单击：粘贴当前项目
- `Delete`：删除当前项目
- `F10`：打开设置
- `Esc`：关闭历史窗口
- 系统托盘右键：打开历史、设置或退出
- 语言默认跟随 Windows 首选 UI 语言，也可以手动选择英文或简体中文
