# 更新日志

## [未发布]

### 技术方案重构

- 建立共享 Direct2D、DirectWrite 和 WIC 渲染核心，设置窗口与历史窗口分别管理窗口级渲染资源，并处理尺寸、DPI 和设备目标重建。
- 设置窗口和历史窗口的主界面文字、卡片、筛选条、统计信息和历史摘要改用 DirectWrite/Direct2D；可见图片记录使用 WIC 按需缩放。
- WIC 图片路径覆盖常见 24/32 位 DIB、反向扫描行和 DIBV5 位域输入，暂不解码未知压缩位图。
- 新增应用生命周期、剪贴板监听、快捷键、历史窗口和设置窗口模块，Win+V 状态机不再依赖设置窗口焦点。
- 移除 GDI+ 运行时和链接依赖；当前仍需继续删除迁移期间禁用的旧绘制源码。
- Release、CTest、窗口生命周期压力 `100/100` 和剪贴板压力 `10000/10000` 已通过；当前设置场景资源测量为 Working Set `37.9 MB`、Private Bytes `25.8 MB`。
- 平台目标调整为 Windows 10 1903 及以上，主要验证 Windows 10 22H2 和 Windows 11。
- 后续自绘界面统一迁移到 Direct2D 1.1、DirectWrite 和 WIC。
- 使用 WARP 作为 Direct2D 软件渲染回退，不再维护重复的旧绘制路径。
- 规划渲染、窗口、剪贴板监听、快捷键和存储模块拆分，降低 `main.cpp` 的职责密度。
- 保留追加式历史存储、CRC、按需读取、DPAPI 和隔离测试数据策略。
- Win+V 规划为独立低级键盘拦截器，并明确普通窗口、高权限窗口和安全桌面的边界。

### 回退节点

- Direct2D/DirectWrite 改造前回退提交：`f506adfb7bd7045981c97e60882f2bfa8e36a99c`。
- 回退节点已推送到 `origin/main`，后续改造可以独立验证并随时恢复。

### 验收方向

- 验证 DirectWrite 文本质量、CJK 字体回退和 125%/150%/200% DPI 布局。
- 验证 Direct2D 设备失效、窗口重建、主题切换和多显示器切换。
- 验证 Private Bytes、Working Set、Commit、GPU Dedicated Memory、GPU Shared Memory 和 CPU 时间。
- 验证 Win+V、Alt+V、普通 Win 快捷键、管理员窗口和任务管理器边界。
- 验证 Windows 10 1903、Windows 10 22H2 和 Windows 11 的安装、升级、卸载和数据迁移。
