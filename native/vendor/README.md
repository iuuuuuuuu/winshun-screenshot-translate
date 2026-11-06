These MIT sources are copied from LingCore/WinShun commit
`ef57fb8` (origin/main, fetched 2026-11-06), without using the modified host
checkout. See LICENSE.

PinWindow, PinTools, Geometry and SmartRedact implement WinShun's physical-pixel
layered pins. Protocol reads JSON-RPC. PhosphorGlyph renders WinShun's icons.
Shared QML controls and icon font are covered by ../../THIRD_PARTY_NOTICES.md.
HoverTip.qml and ListScrollBar.qml use the clean v0.20.0 controls, matching
the existing plugin theme. The scrollbar's pill radius is written inline.
The native entry point, selector, system OCR and service implementation belong
to this plugin. The WinHTTP request lifecycle in ../Http.cpp is adapted from
the clean upstream translate-plugin/Http.cpp at ef57fb8 under the same MIT
license. Windows supplies HTTPS and system proxy support, so no Qt TLS plugin
is required. No capture plugin identifiers or private host exports are used.
