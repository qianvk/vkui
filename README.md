# vkui

`vkui` is a macOS 15-inspired visual foundation and small component library for Qt Widgets.
It gives standard Qt controls a coherent, restrained appearance and adds only three controls that
Qt Widgets genuinely lacks: a switch, a segmented control, and an anchor-aware Popover.

The project uses C++20, Qt 6.6 or newer, and CMake 3.24 or newer. It supports macOS, Windows, and
Linux, including high-DPI and right-to-left environments. Its original artwork and implementation
are MIT licensed; it does not ship Apple fonts, SF Symbols, screenshots, or private resources.
The optional Gallery embeds an open-source Fira Code Nerd Font under its upstream licenses; library
targets remain font-free. See [third-party notices](THIRD_PARTY_NOTICES.md).

## Design boundaries

- `VkUI::Core` contains appearance resolution, immutable semantic tokens, theme-aware SVG icons,
  and motion specifications. It has no QWidget subclasses.
- `VkUI::Widgets` contains `VkStyle`, `VkSwitch`, `VkSegmentedControl`, and `VkPopover`.
- Standard controls remain standard Qt Widgets. `VkStyle` preserves their interaction,
  accessibility, focus, and keyboard behavior.
- Fixed-proportion controls use Small/Regular/Large extent tokens with an optional exact
  logical-pixel override; updates remain local to the affected widget.
- Painting and animation drivers are private implementation details. Public APIs expose policy and
  behavior rather than a second framework.

## Build the gallery and tests

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

The main options are `VKUI_BUILD_SHARED`, `VKUI_BUILD_EXAMPLES`, `VKUI_BUILD_TESTS`,
`VKUI_INSTALL`, `VKUI_ENABLE_WARNINGS`, and `VKUI_ENABLE_SANITIZERS`.

Top-level builds also export and refresh `compile_commands.json` for clangd by default. See the
[clangd and Neovim development guide](docs/development.md) for indexing and navigation setup.

## Use from an installed package

```cmake
find_package(VkUI CONFIG REQUIRED COMPONENTS Core Widgets)

target_link_libraries(my_app PRIVATE VkUI::Widgets)
```

## Embed with `add_subdirectory`

```cmake
set(VKUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VKUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/vkui)

target_link_libraries(my_app PRIVATE VkUI::Widgets)
```

Install the visual foundation once, after constructing `QApplication`:

```cpp
#include <vkui/Widgets.h>

QApplication application(argc, argv);
vkui::installVkUi(application);
```

`installVkUi()` initializes resources and theme state, installs a Fusion-backed `VkStyle`, and
applies the resolved palette. Change appearance later through `VkThemeManager`; the style is not
recreated.

VkUI uses no QSS by default. Applications that need a narrow override layer can call
`applyStyleSheetFile()` after installation; palette-based QSS follows runtime appearance changes
without regenerating the stylesheet.

## Status

The project starts at version 0.1.0. Source compatibility is treated carefully, but ABI stability
is not promised before 1.0.0. See [the architecture guide](docs/architecture.md), the subsystem
documents in `docs/`, and [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT. See [LICENSE](LICENSE).
