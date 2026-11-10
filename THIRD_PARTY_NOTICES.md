# Shared UI licenses

## ECDICT 离线词库

来源：skywind3000/ECDICT，固定提交 `39cfb9672c0906b63569e0453c2e6abfb9ab182a`，采用 MIT 许可。
原始数据为该提交的 `ecdict.csv`；`native/build_ecdict.py` 校验 SHA-256 后生成按词排序的本地索引。
数据来源、原文件及索引的校验值和词条数量记录在 `data/ecdict.json`；完整许可随包保存在 `licenses/ECDICT-LICENSE.txt`。
索引保留词库释义，未添加在线查询依赖；不含用户文本。

## 0.4.7 共享 Qt 与字体

插件代码采用 MIT。本版动态链接 WinShun 提供的 Qt 6.12.x，不再分发 Qt DLL、平台插件或 QML 运行库。
Qt 的著作权属于 The Qt Company Ltd. 和相应贡献者；Qt 运行库来源、SPDX 与适用许可请查看 WinShun 的发布包。
插件保留 `licenses/qt/` 中的许可文本，允许按适用许可替换兼容动态库及调试修改。
管理员插件自身文件变动后仍需要由使用者重新确认。

Microsoft Visual C++ x64 可分发运行库为 Microsoft Corporation 版权所有的软件组件，
从构建工具的 `VC/Redist/MSVC` 目录随应用分发，不适用本项目的 MIT 许可。
阿里巴巴普惠体 3.0 Regular 来自 WinShun 的公开字体资源，保持字体不变；其完整法律声明
位于 `licenses/Alibaba-LICENSE.txt`。Phosphor 图标和 WinShun 代码的 MIT 声明如下。

许可目录为所用 Qt 模块提供的原始许可文本集合；各组件具体适用项以 SPDX 清单及对应源码声明为准。

## WinShun

MIT License

Copyright (c) 2026 LingCore

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


## Phosphor Icons

MIT License

Copyright (c) 2020-2021 Phosphor Icons

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


FlatButton.qml and InputBox.qml are copied from LingCore/WinShun tag v0.20.0 (460f7be). ChoiceTabs.qml is the wrapping host control from the local host repair. Theme.qml uses the same host color and radius tokens. Glyph rendering adapts WinShun's Phosphor outline renderer for PySide. The screenshot and service pages are plugin-specific. No host settings data or service credentials are bundled.
