# 贡献指南

English: [CONTRIBUTING.en.md](CONTRIBUTING.en.md)

感谢关注 ClipLite。项目面向 Windows，优先保证低常驻内存、剪贴板格式可靠性、数据可恢复性和原生交互稳定性。

## 提交问题

提交 Issue 前请确认问题仍存在于最新版本，并提供：

- ClipLite 版本、Windows 版本和系统架构。
- 可重复的操作步骤和实际结果。
- 预期结果，以及是否只在管理员权限或特定 DPI 下出现。
- 相关日志中的必要片段。请先删除剪贴板正文、令牌、路径和其他敏感信息。

## 开发环境

需要 Windows、Visual Studio 2019 或更高版本、CMake 3.15 或更高版本。使用 x64 构建，并始终复用 `build-x64/`：

```pwsh
cmake -S . -B build-x64 -A x64 -DBUILD_TESTING=ON
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure
```

图片滚动基准使用自包含临时数据，不会读取真实历史：

```pwsh
build-x64\Release\ClipLite.exe --benchmark-image-scroll
```

该命令在 `%TEMP%` 下创建进程专属目录，生成 100 张 `1024 x 1024` 图片，并在日志中输出 P50、P95、最慢帧和 GDI 对象数。

代码使用 C++17、Win32 API 和 GDI/GDI+。不要引入 Electron、WebView、WinUI、XAML、Qt、SQLite 或常驻 GPU 资源。不要把用户历史、日志、崩溃转储或构建产物提交到仓库。

## 修改要求

- 存储格式修改必须覆盖成功、边界、损坏、截断和重启恢复场景。
- 存储测试必须使用 `CLIPLITE_TEST_DATA_DIR` 指向 `%TEMP%` 下的隔离目录，不得访问真实 `%LOCALAPPDATA%\\ClipLite`。
- 用户可见行为变更需要同步更新 `README.md` 和 `CHANGELOG.md`。
- 资源或构建配置变更后必须重新构建 Release，确保 `build-x64\\Release\\ClipLite.exe` 与源码一致。
- UI 变更需要检查 DPI、主题、圆角和滚动边界，并使用 PNG、JPEG、GIF 或 WebP 保存截图。
- 提交前运行 `git diff --check`、Release 构建和 CTest。

## 许可证

提交代码即表示你有权提交该内容，并同意其在 [PolyForm Noncommercial License 1.0.0](LICENSE.md) 下提供。商业使用、商业集成和收费分发需要另行取得版权所有者的书面授权。
