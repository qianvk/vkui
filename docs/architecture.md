# Architecture

## Preserve Qt Widgets

vkui is a visual foundation, not a second widget toolkit. Qt already provides mature controls with
years of work in keyboard interaction, focus, input methods, models and views, accessibility, and
platform integration. Wrapping each control would duplicate that behavior and make applications
harder to maintain. `VkStyle` therefore paints supported standard widgets through Qt's style API
and delegates unsupported elements to its explicit Fusion base style.

Only three public widgets fill clear gaps: `VkSwitch`, `VkSegmentedControl`, and `VkPopover`.
Search fields, navigation sidebars, cards, and settings rows remain compositions made from standard
widgets in application code.

## Core and Widgets

`VkUI::Core` has no QWidget subclasses and depends only on Qt Core, Gui, and Svg. It owns resolved
appearance, semantic token value types, icon rendering, and motion policy. Non-widget code can use
this layer without taking a Qt Widgets dependency.

`VkUI::Widgets` publicly links Core and depends on Qt Widgets. Its three distinct subsystems are:

- `VkStyle`, which integrates with Qt's standard style contracts;
- controls, which add missing input behaviors while building on Qt button semantics; and
- overlays, where `VkPopover` owns independent top-level-window and anchor-monitoring behavior.

The Popover is not owned by `VkStyle`: opening, placement, focus, and close policy are component
behavior rather than style behavior. Keeping it independent also allows downstream applications to
use it without making style installation part of its lifetime.

## Public policy, private mechanisms

Semantic tokens and motion specifications are public because application UI needs the same values
as the library. Animation drivers are private because their QObject ownership, interruption, and
frame scheduling are implementation details. Painting helpers are private for the same reason:
exposing geometry primitives would freeze rendering internals and encourage controls to bypass the
stable theme API.

`VkSwitch` inherits `QAbstractButton`, so checked state, signals, mouse activation, Space-key use,
focus, and accessible button semantics have one source of truth. `VkSegmentedControl` composes
private checkable `QAbstractButton` children in an exclusive `QButtonGroup`; it does not reimplement
generic button behavior or expose the child type.

## Theme generations and caches

`VkThemeManager` publishes an immutable resolved theme. Whenever effective appearance or resolved
tokens change, it increments a monotonic generation and emits `themeChanged`. Icon pixmaps include
that generation in their cache key. Painting code reads the current theme during updates, so stale
renderings naturally become unreachable without rebuilding the application or recreating its
style.

## Header boundary

Installed headers live under `include/vkui`. `Core.h` and `Widgets.h` are convenience umbrellas;
the headers beneath `core`, `widgets/style`, `widgets/controls`, and `widgets/overlays` are the
public API. Headers beneath any `src/**/private` directory are implementation details, may change
without notice, and are never installed. No ABI stability is promised before 1.0.0.

