# VkStyle

`VkStyle` is a `QProxyStyle` with an explicitly created Fusion base, providing predictable fallback
across platforms. Use `installVkUi(QApplication&)` once during application startup. The installer
initializes resources and theme state, installs the style, applies the palette, and enables private
state-transition observation.

The style overrides only relevant primitives, controls, complex controls, geometry, metrics,
hints, icons, palettes, and polish hooks. Unsupported elements delegate to Fusion. It preserves Qt
focus, keyboard, model/view, and accessibility contracts; native macOS menu-bar rendering remains
outside its scope.

Hover, press, focus, and selection interpolation is managed privately per polished widget. Updates
are scoped to the affected widget, dead widgets are removed, and disabled animation policy applies
the destination state immediately. No monolithic or animated QSS is used.

QSS is supported as an optional override layer, not as the theme engine. Call
`applyStyleSheetFile()` after `installVkUi()` only when an application provides an override file.
The loader avoids reapplying identical content, and runtime theme changes coalesce their widget
repolish on the next event-loop turn. Use palette roles in QSS so appearance changes do not require
file I/O, token substitution, or reparsing the stylesheet.

Keyboard focus remains visible through a restrained neutral border; mouse activation does not add a
blue focus box. Collapsed combo boxes use the macOS up/down chevron pair. Their popup container uses
a translucent top-level surface and one antialiased `QPainterPath`; no binary `QRegion` mask is used.
The item view and viewport stay transparent so the outer `elevatedBackground` remains the single
surface color. `cornerRadiusLarge` is 16 logical pixels on macOS and 10 on other platforms.

Checked checkbox and radio indicators use device-pixel-aligned outlines, accent-colored selected
edges, and white marks. `vkui::setControlSize()` gives these standard widgets Small, Regular, and
Large indicator metrics while preserving Qt's built-in semantics.

Combo-box width follows Qt's mature `QComboBox::SizeAdjustPolicy`: use `AdjustToContents` for small,
stable models and `AdjustToMinimumContentsLengthWithIcon` for large or frequently changing models.
When layout constraints are tighter than the preferred width, VkUI elides the current label on the
right by default; `setComboBoxElideMode()` selects left, middle, right, or no elision. The label is
always clipped before the chevron column. Spin boxes and editable combo boxes paint one outer frame;
their private line-edit child remains surface-less.
