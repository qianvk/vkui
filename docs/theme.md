# Theme and semantic tokens

`VkThemeManager` is a process-wide QObject created on first use. Set its requested appearance to
`Light`, `Dark`, or `Auto`. Auto observes `QStyleHints::colorScheme` and reacts when the platform
scheme changes. A resolved `VkTheme` is immutable and contains color, metric, typography, and motion
tokens plus an effective appearance, a monotonic generation, and a color-only generation used by
render caches.

`setAccentColor()` selects the macOS-style semantic accent family: Blue, Purple, Pink, Red, Orange,
Yellow, Green, or Graphite. The manager resolves appearance-appropriate accent, hover, pressed, and
focus colors; controls continue to consume the existing semantic `accent` tokens rather than a
component-specific color. Blue is the default.

`installVkUi()` applies a palette derived from the resolved color tokens. `themeChanged()` reports
the exact changed groups through `VkThemeChanges`: `Colors`, `Metrics`, `Typography`, and `Motion`.
Color-only updates repaint visible surfaces and never trigger a global widget-tree repolish. A
central typography controller resolves the body font for polished widgets that have no application
font override. This is required because platform class fonts can bypass ordinary application-font
inheritance. A text-scale change also changes responsive metrics, so the font and geometry work is
combined in one queued, coalesced structural pass rather than separate widget traversals.

Semantic icon cache keys use `colorGeneration()` rather than the broader theme generation, so a
future motion or metric update cannot evict unchanged rendered symbols. Application-wide QSS is not
part of the supported architecture; use semantic Qt palette roles, theme tokens, and widget
properties for application-specific compositions.

Color names describe semantic intent—such as `contentBackground`, `textSecondary`, `accent`, and
`focusRing`—rather than components. Metrics are logical device-independent pixels. Typography uses
the platform system font and never bundles proprietary typefaces. Application code may use the
same tokens for its own compositions.

## Interface text scale

`VkThemeManager::setTextSizeLevel()` controls one process-wide interface reading size. It follows
the current Things for Mac control: 12 discrete levels, numbered 1–12, with level 3 as Default.
Each step represents approximately one point around the macOS 13-point body reference, producing
an internal relative range of 11/13 through 22/13. The public integer level avoids false precision,
limits live layout work to 12 states, and gives pointer, keyboard, and accessibility input exactly
the same granularity.

The scale is responsive rather than a uniform transform:

- typography scales directly by `textScale`;
- standard control heights use `1 + (textScale - 1) × 0.65`;
- meaningful symbols, checkbox/radio extents, and switch geometry use
  `1 + (textScale - 1) × 0.80`;
- layout spacing and small component radii use `sqrt(textScale)`;
- one-device-pixel borders and platform-owned window, menu, combo-popup, and popover radii remain
  stable.

The manager captures the unscaled application font once when it attaches to `QGuiApplication`.
Every level is resolved again from that baseline, so repeated or reversed adjustments never
compound rounding or prior scale. `VStyle` enables `WA_WindowPropagation` at QWidget window
boundaries and normalizes inherited fonts through one private controller. `QLabel`, `QGroupBox`,
buttons, editors, item views, menus, popovers, and other descendants therefore use the same body
size even when a platform class default would otherwise win. No
VkUI-specific replacements for standard textual widgets are necessary. Use
`setTextStyle(widget, VTextStyle::Title)` (or another semantic role) for explicitly styled text;
the lightweight binding listens only for typography changes. Direct per-widget fonts remain an
application-owned override.

Things intentionally treats this as content typography rather than indiscriminate window zoom: its
Settings chrome stays stable, and its sidebar growth is dampened at large values. VkUI provides a
global interface policy because it is a library-level accessibility primitive. An application that
wants Things-style scope should apply its own content level at a content-root boundary and leave
preferences/window chrome on the inherited application font; it still does not need `VLabel` or
`VGroupBox` subclasses.

Small, Regular, and Large `VControlSize` presets resolve through the responsive symbol metrics.
An exact `setControlExtent(widget, logicalPixels)` value remains absolute by design. Likewise,
standard style icon metrics and automatic tree/file icons scale, while an explicit Qt `iconSize`
remains authoritative.

Ticked sliders use the discrete macOS presentation: a neutral two-pixel track, one dot per value,
and a subtle elevated capsule handle. Continuous sliders retain the accent-filled track and round
handle, so semantic appearance follows Qt's existing `tickPosition` contract rather than another
widget subclass or style property.

A level change updates typography and responsive geometry together. Font inheritance remains owned
by Qt after the controller resolves platform font exceptions. One queued, coalesced structural
refresh updates inherited fonts and invalidates style-derived metrics, including platform popup
internals. The 12 discrete levels bound this O(widget-count) work; color-only changes stay on the
cheap top-level repaint path and never trigger it.

Surface metrics remain component-scoped where their geometry is intentionally independent:
`popoverCornerRadius`, `menuCornerRadius`, `comboBoxCornerRadius`, and
`comboBoxPopupCornerRadius` must not be substituted for one another even when two defaults currently
resolve to the same numeric value.

All theme-manager and QWidget-facing calls belong on the Qt GUI thread.
