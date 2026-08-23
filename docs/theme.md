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
Structural metric or typography changes are coalesced to the next event-loop turn and rebuild
widgets child-first for unpolish and parent-first for polish. This avoids reentrant mutation while
preserving deterministic inherited geometry and fonts.

Semantic icon cache keys use `colorGeneration()` rather than the broader theme generation, so a
future motion or metric update cannot evict unchanged rendered symbols. Application-wide QSS is not
part of the supported architecture; use semantic Qt palette roles, theme tokens, and widget
properties for application-specific compositions.

Color names describe semantic intent—such as `contentBackground`, `textSecondary`, `accent`, and
`focusRing`—rather than components. Metrics are logical device-independent pixels. Typography uses
the platform system font and never bundles proprietary typefaces. Application code may use the
same tokens for its own compositions.

Surface metrics remain component-scoped where their geometry is intentionally independent:
`popoverCornerRadius`, `menuCornerRadius`, `comboBoxCornerRadius`, and
`comboBoxPopupCornerRadius` must not be substituted for one another even when two defaults currently
resolve to the same numeric value.

All theme-manager and QWidget-facing calls belong on the Qt GUI thread.
