# Windowing

`VkUI::Window` is the optional native-window module. It owns frameless setup,
move and resize behavior, system-button policy, and multi-title-bar hit
testing. It is built from source inside VkUI and has no QWindowKit package,
submodule, qmsetup, or runtime dependency.

## Public boundary

Applications use `vkui::VkWindowAgent`. The wrapper deliberately keeps the
migrated implementation and its private headers out of the public ABI:

```cpp
auto* agent = new vkui::VkWindowAgent(window);
if (agent->setup(window)) {
    agent->addTitleBar(navigationTitleBar);
    agent->addTitleBar(contentTitleBar);
    agent->setHitTestVisible(searchField, true);
    agent->installSystemButtons();
}
```

Title bars may be transparent and may be siblings. Each interactive child must
be excluded from drag hit testing. macOS uses AppKit traffic lights, Windows
uses native-compatible caption controls, and the portable context keeps the
same application interaction contract on Linux.

Qt private platform APIs are required by native window contexts. Shared and
static applications must use the same Qt patch release as the VkUI window
module. `VKUI_WINDOW_FORCE_QT_CONTEXT` selects the portable Qt behavior at
runtime, but the current implementation still compiles against Qt private APIs
and therefore does not remove that build- and package-time version coupling.

## Dialog primitives

`VkFramelessDialog` provides close-only, full-content chrome, a transparent
title bar, native system controls with a VkUI fallback, optional resizing, and
host-relative placement. Applications compose their own content through
`contentLayout()`.

`VkMessageDialog` adds severity, platform-ordered buttons, explicit default
and Escape actions, and selected-text-safe message rendering.
`confirmDestructive()` always makes Cancel both the default button and the
Escape action. A destructive operation therefore requires explicit activation
of its button; pressing Enter can never delete data, even after focus moves to
the destructive button.

## Preferences guidance

VkUI intentionally does not own Vault, plugin, account, or settings models.
Those are application concepts. A reliable Preferences implementation should:

1. derive its chrome from the shared frameless primitive;
2. keep navigation and pages in the application;
3. register title-bar-safe controls through `VkWindowAgent`;
4. use `positionForHost(host, QSizeF(0.8, 0.8))` for a host-centered window;
5. route destructive actions through `VkMessageDialog`.

This shares lifecycle, accessibility, native controls, and safety policy
without freezing one application's page model into a general UI library.

## Qt Quick

The complete migrated Qt Quick implementation is retained for future work, but
VkUI does not yet promise a public Quick facade or package ABI. It is therefore
not installed, exported, or advertised as a `find_package` component.

Maintainers can compile-check the internal `vkui_window_quick` target with
`VKUI_BUILD_WINDOW_QUICK=ON` and `VKUI_INSTALL=OFF`. Shipping applications
should use the supported `VkUI::Window` Widgets facade until a stable Quick API
is introduced.
