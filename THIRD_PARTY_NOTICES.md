# Shared UI licenses

## 0.4.0 原生包的运行库与字体

插件代码采用 MIT。包内 `runtime/` 中的 Qt 6.10.0 动态库采用 LGPL v3；Qt 的著作权属于
The Qt Company Ltd. 和相应贡献者。Qt 源码未作修改，使用官方 MSVC x64 构建；
`windeployqt` 仅按标准部署流程调整安装路径。

- 对应源码：https://download.qt.io/archive/qt/6.10/6.10.0/single/qt-everywhere-src-6.10.0.tar.xz
- 模块源码与原始许可：https://github.com/qt/qtbase/tree/v6.10.0 、https://github.com/qt/qtdeclarative/tree/v6.10.0 、https://github.com/qt/qtsvg/tree/v6.10.0
- 完整许可文本位于包内 `licenses/qt/`；Qt 官方提供的模块 SPDX 清单位于 `licenses/qtbase-6.10.0.spdx.json` 等文件，包含第三方组件来源和著作权。
- 本插件通过动态链接使用 Qt，允许按适用许可替换兼容的 Qt 动态库，并调试相应修改。
  管理员副本由 WinShun 校验；修改后需要由使用者重新确认。

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
