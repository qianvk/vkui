# QWindowKit

QWindowKit provides native-aware frameless windows for Qt Widgets and Qt Quick. It keeps window
movement, resizing, system menus, caption hit testing, and platform window controls connected to
the operating system while allowing application content to occupy the title-bar area.

## Supported Platforms

| Platform | Native backend | Window controls |
| --- | --- | --- |
| Windows 10/11 | Win32, DWM, `WM_NCHITTEST` | Optional QWindowKit-managed caption buttons |
| macOS 11+ | AppKit, `NSWindow` | Native traffic-light buttons |
| Linux | X11 or Wayland | Application-provided controls |

Platform source files are selected by CMake. Windows builds do not compile AppKit, X11, or
Wayland code; macOS builds do not compile Win32, X11, Wayland, or QWidget caption-button code.

## Modules

- `QWKCore`: native window contexts, frame hit testing, window movement, resizing, and shared
  system-button state.
- `QWKWidgets`: `QWidget` integration, multiple title-bar regions, Windows caption controls, and
  macOS traffic-light geometry.
- `QWKQuick`: Qt Quick integration over the same Core window contexts.

## Current Features

- Frameless content extending to the top of the native window.
- Multiple draggable title-bar widgets in one window.
- Explicit interactive regions, including sibling widgets such as `QSplitterHandle`.
- Native system movement and resizing.
- Windows 11 Snap Layout support through `HTMAXBUTTON` hit-test results.
- Framework-managed Windows minimize, maximize/restore, and close buttons fixed to the top-right
  corner.
- Native macOS traffic lights obtained from `NSWindow::standardWindowButton`.
- Adjustable macOS traffic-light placement without a placeholder widget in application code.
- Three system-button visibility policies: always visible, visible on hover, and always hidden.
- Optional Windows 10 top-border handling and platform style effects.

## Requirements

- CMake 3.19 or newer. The Notes demo requires CMake 3.21 or newer.
- C++20 for this source tree.
- Qt 5.15.2 or newer, or Qt 6.6.2 or newer.
- The Notes demo currently uses Qt 6.10.
- MSVC 2019/2022, a recent Apple Clang, or a recent GCC/Clang toolchain.

## Build

Clone the repository with its submodules, then configure and build it with CMake:

```sh
git clone --recursive https://github.com/stdware/qwindowkit.git
cd qwindowkit

cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/path/to/Qt \
  -DQWINDOWKIT_BUILD_WIDGETS=ON \
  -DQWINDOWKIT_BUILD_QUICK=OFF \
  -DQWINDOWKIT_BUILD_EXAMPLES=ON

cmake --build build --config Release
```

Important build options:

| Option | Default | Purpose |
| --- | --- | --- |
| `QWINDOWKIT_BUILD_STATIC` | `OFF` | Build static libraries instead of shared libraries |
| `QWINDOWKIT_BUILD_WIDGETS` | `ON` | Build `QWKWidgets` |
| `QWINDOWKIT_BUILD_QUICK` | `OFF` | Build `QWKQuick` |
| `QWINDOWKIT_BUILD_EXAMPLES` | `ON` | Build the Notes multi-titlebar demo |
| `QWINDOWKIT_ENABLE_WINDOWS_SYSTEM_BORDERS` | `ON` | Enable the Windows system-border integration |
| `QWINDOWKIT_ENABLE_STYLE_AGENT` | `ON` | Build platform style effects |
| `QWINDOWKIT_FORCE_QT_WINDOW_CONTEXT` | `OFF` | Use the portable Qt fallback context |

## Qt Widgets Quick Start

Create one agent for each top-level widget and install the platform controls:

```cpp
#include <QWKWidgets/widgetwindowagent.h>

auto *agent = new QWK::WidgetWindowAgent(window);
agent->setup(window);
agent->installSystemButtons();
```

On Windows, `installSystemButtons()` creates the minimize, maximize/restore, and close controls,
fixes them to the top-right corner, binds their window actions, and registers their native caption
roles. Applications should reserve `agent->systemButtonAreaGeometry()` in their top title-bar
layout. On macOS, AppKit continues to own and draw the native traffic lights.

Register one or more title-bar widgets:

```cpp
agent->addTitleBar(folderTitleBar);
agent->addTitleBar(listTitleBar);
agent->addTitleBar(editorTitleBar);
```

Mark every control that must receive pointer input inside a draggable title-bar region:

```cpp
agent->setHitTestVisible(folderTitleBar, addButton, true);
agent->setHitTestVisible(folderTitleBar, splitter->handle(1), true);
agent->setHitTestVisible(listTitleBar, splitter->handle(1), true);
```

The explicit title-bar overload accepts both descendants and sibling controls as long as they
belong to the same top-level window. This lets a visually narrow `QSplitterHandle` keep Qt's normal
drag behavior inside the title-bar band.

## System Button Visibility

The same API controls native macOS traffic lights and registered QWidget caption buttons:

```cpp
agent->setSystemButtonVisibility(QWK::WindowAgentBase::AlwaysVisible);
agent->setSystemButtonVisibility(QWK::WindowAgentBase::VisibleOnHover);
agent->setSystemButtonVisibility(QWK::WindowAgentBase::AlwaysHidden);
```

On Windows, hidden hover-mode buttons retain their native hit-test geometry so the maximize button
continues to return `HTMAXBUTTON` and Snap Layout remains available.

## macOS Traffic-Light Geometry

macOS builds expose a geometry setter that positions the native AppKit buttons without requiring a
dummy `QWidget`:

```cpp
#ifdef Q_OS_MAC
agent->setSystemButtonAreaGeometry(QRect(2, 10, 72, 32));
#endif
```

QWindowKit centers the native buttons in this rectangle. The placement is replayed after AppKit's
initial window layout and after native window updates, so the first displayed frame uses the
requested geometry.

The older widget and callback placement APIs remain available for applications that need geometry
tied to a live widget or calculated from the native title-bar size.

## Notes Multi-Titlebar Demo

`notes-multi-titlebar-demo` demonstrates three draggable pane headers, splitter interaction inside
the title-bar band, folder-panel collapse behavior, and live system-button visibility controls.

The demo is divided into:

- `mainwindow.cpp`: platform-neutral Notes layout and behavior.
- `mainwindow_mac.cpp`: traffic-light placement controls and macOS chrome metrics.
- `mainwindow_win.cpp`: Windows layout reservations for framework-owned caption buttons.
- `mainwindow_generic.cpp`: fallback desktop metrics.
- `demostyle.cpp`: the shared macOS-inspired visual style.

CMake compiles exactly one `mainwindow_<platform>.cpp` file.

## Integration Notes

- Call `WidgetWindowAgent::setup()` before creating native child widgets or applying final size
  constraints.
- Set `Qt::AA_DontCreateNativeWidgetSiblings` before constructing `QApplication` when the
  application may contain native child widgets.
- Do not change top-level window flags after agent setup unless the window is recreated.
- Keep interactive title-bar controls registered with `setHitTestVisible()`.
- A maximize button must remain registered as `WindowAgentBase::Maximize` for Windows Snap Layout.

## Platform References

- [Apple: `NSWindow::standardWindowButton`](https://developer.apple.com/documentation/appkit/nswindow/standardwindowbutton%28_%3A%29)
- [Apple: `NSWindow::layoutIfNeeded`](https://developer.apple.com/documentation/appkit/nswindow/layoutifneeded%28%29)
- [Apple: `NSTrackingArea`](https://developer.apple.com/documentation/appkit/nstrackingarea)
- [Microsoft: Support Snap Layouts for desktop apps](https://learn.microsoft.com/windows/apps/desktop/modernize/ui/apply-snap-layout-menu)
- [Microsoft: `WM_NCHITTEST`](https://learn.microsoft.com/windows/win32/inputdev/wm-nchittest)
- [Qt: `QSplitter`](https://doc.qt.io/qt-6/qsplitter.html)
- [Qt: `QWindow`](https://doc.qt.io/qt-6/qwindow.html)

## License

QWindowKit is licensed under the [Apache License 2.0](LICENSE).
