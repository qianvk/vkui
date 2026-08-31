# vkui

`vkui` is a macOS 15-inspired visual foundation and small component library for Qt Widgets.
It gives standard Qt controls a coherent, restrained appearance and adds focused components only
where a style alone cannot provide the required behavior.

The project uses C++20, Qt 6.6 or newer, and CMake 3.24 or newer. It supports macOS, Windows, and
Linux, including high-DPI and right-to-left environments. Its original artwork and implementation
are MIT licensed; it does not ship Apple fonts, SF Symbols, screenshots, or private resources.
Core uses SVG exclusively for named symbols, including file-system icons. See
[third-party notices](THIRD_PARTY_NOTICES.md) for the temporary converted catalog and derived file
symbol artwork.

## Design boundaries

- `VkUI::Core` contains appearance resolution, immutable semantic tokens, theme-aware SVG and
  named file icons, and motion specifications. It has no QWidget subclasses.
- `VkUI::Widgets` contains `VStyle` plus focused controls, views, effects, and overlays such as
  `VSwitch`, `VSegmentedControl`, `VSplitter`, `VPanelManager`, `VTreeView`,
  `VLiquidGlassSurface`, and `VPopover`.
- `VkUI::Buffer` is the renderer-independent owned/provider-backed text data plane.
- `VkUI::Interaction` owns canonical key input, modal grammar, commands, registers, buffers,
  semantic windows, and trusted interaction-plugin lifecycle. It never depends on QWidget.
- `VkUI::Panel` owns the immutable split-tree snapshots, resize math, visibility, and spatial
  navigation used by any renderer.
- `VkUI::InteractionWidgets` projects interaction intents onto standard Qt item views, controls,
  blocks, and mounted panels without synthesizing `QKeyEvent` objects.
- `VkUI::Window` is an optional, per-window composed full-content module with native move/resize,
  multiple transparent title bars, system-button policy, and unified dialog chrome.
- Standard controls remain standard Qt Widgets. `VStyle` preserves their interaction,
  accessibility, focus, and keyboard behavior.
- Fixed-proportion controls use Small/Regular/Large extent tokens with an optional exact
  logical-pixel override. Presets follow the interface text scale; exact values remain absolute.
- Painting and animation drivers are private implementation details. Public APIs expose policy and
  behavior rather than a second framework.

## Build the gallery and tests

The Gallery and `VkUI::Window` are available on macOS and Windows. Linux builds
Core/Widgets with both options disabled by default.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.6-or-newer \
    -DVKUI_BUILD_EXAMPLES=ON -DVKUI_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

With Ninja and a discoverable Qt installation, the checked-in presets are also available:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The main options are `VKUI_BUILD_SHARED`, `VKUI_BUILD_EXAMPLES`, `VKUI_BUILD_ICON_CHOSEN`,
`VKUI_BUILD_TESTS`, `VKUI_BUILD_WINDOW`, `VKUI_BUILD_INTERACTION`, `VKUI_INSTALL`,
`VKUI_ENABLE_WARNINGS`, and `VKUI_ENABLE_SANITIZERS`. `VKUI_BUILD_WINDOW` and
`VKUI_BUILD_EXAMPLES` default to on only on macOS and Windows. The temporary `icon-chosen` tool
follows `VKUI_BUILD_EXAMPLES` by default and remains isolated from the Gallery and installed
libraries.

Top-level builds also export and refresh `compile_commands.json` for clangd by default. See the
[clangd and Neovim development guide](docs/development.md) for indexing and navigation setup.

## Use from an installed package

```cmake
find_package(VkUI CONFIG REQUIRED COMPONENTS
    Core Widgets Window Buffer Interaction Panel InteractionWidgets)

target_link_libraries(my_app PRIVATE
    VkUI::Widgets VkUI::Window VkUI::InteractionWidgets)
```

## Embed with `add_subdirectory`

```cmake
set(VKUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VKUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/vkui)

target_link_libraries(my_app PRIVATE VkUI::Widgets VkUI::Window)
```

Install the visual foundation once, after constructing `QApplication`:

```cpp
#include <vkui/Widgets.h>

QApplication application(argc, argv);
vkui::installVkUi(application);
```

`installVkUi()` initializes resources and theme state, installs a Fusion-backed `VStyle`, and
applies the resolved palette. Change appearance later through `VkThemeManager`; the style is not
recreated.

Interface typography can be adjusted live through one coalesced structural refresh:

```cpp
vkui::VkThemeManager::instance()->setTextSizeLevel(6); // Levels 1–12; level 3 is Default.
vkui::setTextStyle(*sectionTitle, vkui::VTextStyle::Title);
```

Qt propagates the scaled body font and invalidates affected layouts. Semantic text roles,
predefined control extents, style icon metrics, spacing, and radii resolve from the same theme;
explicit per-widget control or icon dimensions remain authoritative.

`VkUI::Window` uses VkUI-owned, per-window AppKit and Win32 backends and depends only on public Qt
APIs. The previous QWindowKit source remains in the repository for reference but is not compiled or
linked. Applications include `<vkui/Window.h>` and use `VWindowAgent` for application windows or
`VMessageDialog` for prompts. See the [windowing guide](docs/windowing.md).

VkUI renders standard controls through `QStyle` and `QPainter`, backed by semantic theme tokens.
Application-wide QSS is intentionally unsupported because it bypasses parts of the custom-style
contract and makes runtime theme invalidation less predictable. Compose application-specific UI
with palette roles, tokens, and ordinary widget properties instead.

`VTreeView` keeps model data, row rendering, and structural animation separate. Custom row layouts
derive from `VTreeItemDelegate`; their leading-icon geometry automatically remains the expansion
hit target. See the [tree-view guide](docs/tree-view.md) and [file-tree guide](docs/file-tree.md).

`VLiquidGlassBackdrop` and `VLiquidGlassSurface` provide shared, local backdrop sampling for
cross-platform glass compositions without replacing child-widget behavior. The cached QPainter
renderer is shared by every platform. `VkThemeManager::setLiquidGlassEnabled()` switches all
supported surfaces between the optical material and their opaque semantic fallback without
rebuilding the theme. `setLiquidGlassTintLevel()` exposes the system-style Clear-to-Tinted
preference without leaking renderer parameters. Public `regular()`, `clear()`, `control()`, and
`popup()` material presets keep applications from duplicating optical recipes. See the
[liquid-glass guide](docs/liquid-glass.md).

## Status

The project starts at version 0.1.0. Source compatibility is treated carefully, but ABI stability
is not promised before 1.0.0. See [the architecture guide](docs/architecture.md), the subsystem
documents in `docs/`, and [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT. See [LICENSE](LICENSE).
