# WinShun local host repair

`winshun-0.20.0-choice-wrap.patch` targets WinShun v0.20.0, commit
`460f7be004e952e741c1663b8df26a0f09f031a8`.

Apply in a WinShun checkout using `git apply`, then build WinShun normally.
The patch adds ChoiceTabs.qml to the app's QML module and replaces PluginCard's
overflow menu with wrapping exclusive choices below the title. One provider
setting remains one value; no duplicate settings or model-service group is added.

`winshun-0.20.0-repair.patch` also restores stopped eager listeners when Reload
is pressed with an unchanged manifest. It includes production preview support
for opening a specific plugin's provider choices on an isolated profile.

The community plugin archive excludes this directory. The host executable and
plugin package are separate artifacts. A running host keeps its existing code
until the user exits and reopens it.

Validation: `python -B .host/test_choice_tabs.py` runs the actual new component
with Qt's offscreen platform, supplying the existing Theme tokens from a fixture.
It checks 320/400/560/1000-pixel widths, all ten choices, exclusivity, keyboard
selection and long labels. For development, install PySide6-Essentials into
`.test-output/qt`; those packages are not runtime dependencies.

The C++ host was built with MSVC and Qt 6.10.0. Production UI validation loads
the user's installed Qt 6.12.0 runtime, plugins and QML modules on a private
Windows desktop which is never switched to. `preview_host.py` runs the built
host and its tests there.

Host tests cover stopped eager reload, disabled-state preservation, re-enabling,
permissions, and the actual Python broker started through WinShunPluginHost.
Native hotkey tests cover registration, conflicts, replacement and WM_HOTKEY
dispatch. They use test keys on the private desktop, not user keyboard input.
