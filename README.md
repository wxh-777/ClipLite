# ClipLite

一款小而全的 Windows 剪贴板历史工具。

复制过的文字、图片、文件和网页内容，之后都可以从历史窗口中搜索并再次粘贴。ClipLite 常驻系统托盘，但尽量不占用本来就有限的桌面资源。

当前正式版本：**1.5.4（Windows x64）**

![Windows](https://img.shields.io/badge/Windows-10%2B-0078D6?style=flat-square&logo=windows&logoColor=white)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)
![Architecture](https://img.shields.io/badge/architecture-x64-444444?style=flat-square)
![License](https://img.shields.io/badge/license-PolyForm%20Noncommercial-orange?style=flat-square)

[下载和运行](#下载和运行) · [完整使用说明](USAGE.md) · [更新日志](CHANGELOG.md) · [English](README.en.md)

官方网站：https://moyu888.top/clip-lite

## 小而全

ClipLite 保留剪贴板历史所需的常用功能，同时控制程序体积和后台占用。它使用原生 Win32 实现，不需要额外安装运行时；图片和完整内容也只会在需要时读取。

| 项目 | 参考值 |
| --- | --- |
| 主程序体积 | 约 `862 KB` |
| 空闲内存 | 任务管理器显示约 `0.2 MB+` |
| 启动时间 | 约 `55 ms` |
| 运行方式 | 原生 Win32，不需要额外运行时 |
| 内容读取 | 图片和完整内容按需读取，不启动时加载全部历史 |

实际数值会受到 Windows 版本、DPI、历史数据和运行状态影响。

## 技术特点

项目采用 Windows 原生实现，主要技术方案如下：

| 方向 | 方案 |
| --- | --- |
| 语言和构建 | C++17、CMake、Visual Studio，固定构建目标为 Windows x64 |
| 界面和绘制 | Win32 API、原生控件、GDI/GDI+，不依赖 WebView、Electron、WinUI 或 Qt |
| 历史存储 | 追加式 v4 二进制记录，包含长度限制、CRC 校验、截断恢复和按需读取 |
| 后台处理 | 剪贴板归档、内容读取、清理和压缩通过短任务工作线程执行，避免阻塞窗口操作 |
| 数据保护 | 默认关闭 DPAPI；启用后由同一 Windows 用户恢复历史正文 |
| 缓存策略 | 图片和完整内容按需读取，缩略图单独缓存并可回收，不把完整历史正文常驻内存 |

## 看起来简单，用起来完整

### 历史记录

- 按 `Alt+V` 打开历史窗口，搜索文字、图片、文件和网页内容。
- 支持按类型、时间范围、内容长度、来源应用和排序方式组合筛选。
- 收藏和置顶相互独立，历史窗口可以记住上次打开的分类。
- 历史列表支持紧凑、标准和自适应三种高度模式。
- 历史窗口支持跟随鼠标、记住上次位置或按屏幕坐标固定弹出位置。
- 可以限制历史条数、磁盘空间、保留天数和单条内容大小，也可以暂时暂停监听。

### 预览、编辑和粘贴

- 图片支持悬停预览、滚轮缩放、拖拽和按需加载原图；长文本支持滚动查看。
- 支持纯文本粘贴和富文本粘贴，也可以将文本或 HTML 记录编辑为纯文本。
- 编辑结果可以替换原记录、另存为新记录，或保存后直接粘贴。
- 历史记录支持备注、收藏、置顶和批量清理。
- 文件路径失效时会在列表中提示，并在粘贴前再次确认。

### 外观和系统集成

- 提供浅色、深色、护眼绿、护眼夜间、纸张暖色和午夜蓝主题，也支持创建自定义主题。
- 可以跟随 Windows 亮色和暗色模式切换主题，系统高对比度模式也有对应配色。
- 支持简体中文和英文界面，运行在系统托盘中。
- Explorer 重启或任务栏刷新后会自动恢复托盘图标。
- 可以将打开历史窗口的快捷键改为 `Win+V`，具体行为受 Windows 和权限级别限制。

## 界面预览

<table>
  <tr>
    <td align="center"><strong>历史窗口</strong><br><img src="https://raw.githubusercontent.com/wxh-777/ClipLite/main/docs/screenshots/history.png" alt="ClipLite 历史窗口" width="100%"></td>
    <td align="center"><strong>设置窗口</strong><br><img src="https://raw.githubusercontent.com/wxh-777/ClipLite/main/docs/screenshots/settings.png" alt="ClipLite 设置窗口" width="100%"></td>
  </tr>
  <tr>
    <td colspan="2" align="center"><strong>外观设置</strong><br><img src="https://raw.githubusercontent.com/wxh-777/ClipLite/main/docs/screenshots/appearance.png" alt="ClipLite 外观设置" width="70%"></td>
  </tr>
</table>

## 下载和运行

正式发布提供两种 Windows x64 版本：

| 版本 | 文件 | 适用方式 |
| --- | --- | --- |
| 安装版 | `ClipLite-Setup-1.5.4-x64.exe` | 安装到系统，支持开始菜单、桌面快捷方式和标准卸载 |
| 便携版 | `ClipLite-1.5.4-portable-win-x64.zip` | 解压整个目录后运行，适合放在 U 盘或其他便携目录 |

便携版的数据保存在程序目录内的 `data\\` 文件夹。更新便携版时请保留该文件夹，不要只复制新的 `ClipLite.exe`。

下载后可以使用便携包中的 `SHA256SUM.txt` 校验 `ClipLite.exe`。请确认下载来源后再运行程序。

## 常用快捷键

| 快捷键 | 功能 |
| --- | --- |
| `Alt+V` | 打开剪贴板历史 |
| `Enter` 或鼠标单击 | 粘贴当前记录 |
| `Ctrl+Shift+V` | 粘贴为纯文本 |
| `Delete` | 删除当前记录 |
| `Esc` | 关闭历史窗口 |
| `F10` | 打开设置 |
| `Ctrl+0` | 清除历史筛选 |
| `Ctrl+F` | 聚焦搜索框 |
| `Ctrl+Shift+F` | 打开历史筛选 |
| `Ctrl+E` | 编辑选中的文本记录 |
| `Ctrl+Alt+P` | 置顶或取消置顶历史窗口 |
| `Ctrl+Alt+F` | 切换收藏记录的跨分类显示 |
| `F2` | 按键预览开启时，按住查看当前记录的预览 |

快捷键可以在设置页修改。重复组合键会被拒绝写入；如果系统或其他软件已经占用某个全局快捷键，设置页会提示冲突。

## 更新、权限和隐私

- 关于页支持手动检查更新，也可以开启后台自动检查；发现新版本后通过托盘通知提示。
- 安装版升级会保留“随 Windows 启动”的设置。便携版和安装版会匹配各自对应的更新包。
- 支持按来源应用忽略内容、敏感文本自动过期和 Windows DPAPI 用户级加密。
- 可以选择以管理员权限运行。首次授权后，后续启动通过当前用户的最高权限计划任务完成。
- 管理员权限不能绕过 Windows UIPI、安全桌面或受保护窗口的限制。

安装版默认将数据保存在：

```text
%LOCALAPPDATA%\\ClipLite\\history.bin
%LOCALAPPDATA%\\ClipLite\\settings.ini
%LOCALAPPDATA%\\ClipLite\\cliplite.log
```

便携版使用程序目录内的 `data\\` 文件夹，也可以在设置中指定其他数据目录。ClipLite 不会把剪贴板正文写入诊断日志。卸载安装版时，用户数据默认保留；卸载完成后可以选择删除历史、缓存、设置和日志。

## 构建

需要 Windows、Visual Studio 2019 或更高版本，以及 CMake 3.15 或更高版本。构建目标固定为 x64。

```pwsh
cmake -S . -B build-x64 -A x64 -DBUILD_TESTING=ON
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure
```

生成便携包和可选安装包：

```pwsh
powershell -ExecutionPolicy Bypass -File packaging/package.ps1
```

输出位于 `build-x64\\Release\\` 和 `out\\`。开发细节见 [CONTRIBUTING.md](CONTRIBUTING.md)，安全问题请阅读 [SECURITY.md](SECURITY.md)。

<details>
<summary>命令行参数</summary>

```text
ClipLite.exe --history    启动并打开历史窗口
ClipLite.exe --settings   启动并打开设置窗口
ClipLite.exe --exit       请求已运行实例退出
```

</details>

## 许可证

ClipLite 使用 [PolyForm Noncommercial License 1.0.0](LICENSE.md) 授权，允许个人、教育、研究和其他非商业用途使用、学习、修改和分发源码或程序，但禁止销售、收费分发、商业集成和其他商业用途。

这是一份 source-available（公开源码）许可证，不是 OSI 认可的开源许可证。商业使用或商业分发请先联系版权所有者取得书面授权。第三方依赖和资源可能有独立许可证或权利声明，使用时请分别遵守。
