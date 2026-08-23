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
Color-only updates repaint visible surfaces and never trigger a global widget-tree repolish.
Typography updates use Qt's application-font propagation, which already delivers font changes,
invalidates size hints, and activates affected layouts. A text-scale update therefore does not
walk `QApplication::allWidgets()` or repolish every control. A future metrics-only change is
coalesced to the next event-loop turn and rebuilds widget style geometry deterministically.

Semantic icon cache keys use `colorGeneration()` rather than the broader theme generation, so a
future motion or metric update cannot evict unchanged rendered symbols. Application-wide QSS is not
part of the supported architecture; use semantic Qt palette roles, theme tokens, and widget
properties for application-specific compositions.

Color names describe semantic intent—such as `contentBackground`, `textSecondary`, `accent`, and
`focusRing`—rather than components. Metrics are logical device-independent pixels. Typography uses
the platform system font and never bundles proprietary typefaces. Application code may use the
same tokens for its own compositions.

## Interface text scale

`VkThemeManager::setTextScale()` controls one process-wide interface reading scale. Values are
clamped to 80–160 percent and snapped to five-percent increments; 100 percent restores the
platform baseline. Five percent is fine enough to avoid conspicuous jumps while limiting layout
and glyph-cache churn during live slider interaction. Ten-percent page steps provide useful
keyboard navigation. The 80-percent floor keeps macOS's 13-logical-pixel body baseline close to
the platform's 10-pixel minimum, while 160 percent provides a substantial readability range
without turning a desktop surface into a touch-layout zoom.

The scale is responsive rather than a uniform transform:

- typography scales directly by `textScale`;
- standard control heights use `1 + (textScale - 1) × 0.65`;
- meaningful symbols, checkbox/radio extents, and switch geometry use
  `1 + (textScale - 1) × 0.80`;
- layout spacing and small component radii use `sqrt(textScale)`;
- one-device-pixel borders and platform-owned window, menu, combo-popup, and popover radii remain
  stable.

The manager captures the unscaled application font once when it attaches to `QGuiApplication`.
Every slider value is resolved again from that baseline, so repeated or reversed adjustments never
compound rounding or prior scale. Qt then propagates the body font efficiently. Use
`setTextStyle(widget, VTextStyle::Title)` (or another semantic role) for explicitly styled text;
the lightweight binding listens only for typography changes. Direct per-widget fonts remain an
application-owned override.

Small, Regular, and Large `VControlSize` presets resolve through the responsive symbol metrics.
An exact `setControlExtent(widget, logicalPixels)` value remains absolute by design. Likewise,
standard style icon metrics and automatic tree/file icons scale, while an explicit Qt `iconSize`
remains authoritative.

Surface metrics remain component-scoped where their geometry is intentionally independent:
`popoverCornerRadius`, `menuCornerRadius`, `comboBoxCornerRadius`, and
`comboBoxPopupCornerRadius` must not be substituted for one another even when two defaults currently
resolve to the same numeric value.

All theme-manager and QWidget-facing calls belong on the Qt GUI thread.
