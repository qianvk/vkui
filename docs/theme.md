# Theme and semantic tokens

`VkThemeManager` is a process-wide QObject created on first use. Set its requested appearance to
`Light`, `Dark`, or `Auto`. Auto observes `QStyleHints::colorScheme` and reacts when the platform
scheme changes. A resolved `VkTheme` is immutable and contains color, metric, typography, and motion
tokens plus an effective appearance and monotonic generation.

`setAccentColor()` selects the macOS-style semantic accent family: Blue, Purple, Pink, Red, Orange,
Yellow, Green, or Graphite. The manager resolves appearance-appropriate accent, hover, pressed, and
focus colors; controls continue to consume the existing semantic `accent` tokens rather than a
component-specific color. Blue is the default.

`installVkUi()` applies a palette derived from the resolved color tokens. Subsequent theme changes
update that palette and coalesce one deferred repolish of existing widgets. Deferring avoids
reentrant mutation when an appearance combo-box popup is still dispatching its selection event;
child-first unpolish and parent-first polish also rebuild inherited and stylesheet-resolved
palettes in a deterministic order. Generation-keyed icon invalidation keeps rendered symbols in
sync without retaining stale pixmaps.

VkUI does not install QSS by default. `applyStyleSheetFile()` optionally layers an application QSS
file over `VkStyle`; missing files leave the default style untouched. Prefer semantic Qt palette
expressions such as `palette(base)`, `palette(text)`, and `palette(highlight)` instead of literal
light/dark colors. This lets the same parsed stylesheet follow runtime appearance changes, while
static colors remain intentional application overrides.

Color names describe semantic intent—such as `contentBackground`, `textSecondary`, `accent`, and
`focusRing`—rather than components. Metrics are logical device-independent pixels. Typography uses
the platform system font and never bundles proprietary typefaces. Application code may use the
same tokens for its own compositions.

All theme-manager and QWidget-facing calls belong on the Qt GUI thread.
