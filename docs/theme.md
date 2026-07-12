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
update that palette and repaint affected UI; they do not install a global QSS document or recreate
the style. Palette propagation, generation-keyed icon invalidation, and targeted custom-widget
updates replace explicit traversal of every application widget.

Color names describe semantic intent—such as `contentBackground`, `textSecondary`, `accent`, and
`focusRing`—rather than components. Metrics are logical device-independent pixels. Typography uses
the platform system font and never bundles proprietary typefaces. Application code may use the
same tokens for its own compositions.

All theme-manager and QWidget-facing calls belong on the Qt GUI thread.
