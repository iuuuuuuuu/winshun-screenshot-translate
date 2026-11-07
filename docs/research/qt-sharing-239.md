# WinShun #239 的 Qt 复用边界

核对时间：2026-11-07。

官方讨论：<https://github.com/LingCore/WinShun/issues/239>。
LingCore 在 2026-11-07 的发布回复中说明，0.22.0 已提供 `runtime.qt: "host"` 与 `runtime.qtVersion: "6.12"`。
同一回复的限制原文为：

> 首版支持普通权限的 MSVC x64 Release 插件，拒绝与 `elevated` 同用。

本插件使用 `elevated` 读取管理员 Codex 等程序的选区，因此正式分发包按官方契约自带 Qt。
用户本机旧的 0.21.0 开发候选仍使用其已有启动器和宿主运行库进行兼容测试；这不等于官方允许管理员共享 Qt。

官方文档：<https://github.com/LingCore/WinShun/blob/v0.22.0/docs/plugins.md>。
