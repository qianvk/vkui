# VStyle

`VStyle` is a `QProxyStyle` with an explicitly created Fusion base, providing predictable fallback
across platforms. Use `installVkUi(QApplication&)` once during application startup. The installer
initializes resources and theme state, installs the style, applies the palette, and enables private
state-transition observation.

The style overrides only relevant primitives, controls, complex controls, geometry, metrics,
hints, icons, palettes, and polish hooks. Unsupported elements delegate to Fusion. It preserves Qt
focus, keyboard, model/view, and accessibility contracts; native macOS menu-bar rendering remains
outside its scope.

Hover, press, focus, and selection interpolation is managed privately per polished widget. Updates
are scoped to the affected widget, dead widgets are removed, and disabled animation policy applies
the destination state immediately.

Application-wide QSS is intentionally unsupported. Qt does not guarantee the behavior of a custom
`QStyle` combined with style sheets, and complex controls require every sub-control to be styled as
one unit. VkUI therefore keeps geometry, painting, hit testing, and state transitions in one typed
style implementation. Applications should use semantic palette roles and theme tokens for custom
compositions.

Runtime theme refresh is change-aware. Color-only changes update the application palette and
visible top-level surfaces without traversing or repolishing every widget. Application typography
uses Qt's native font propagation and size-hint invalidation; metrics-only changes use a coalesced
structural refresh. Motion changes are consumed by animation drivers without rebuilding widget
geometry. Popup-surface setup and refresh coordination live in separate private components so
`VStyle` remains focused on Qt style contracts.

Keyboard focus remains visible through a restrained neutral border; mouse activation does not add a
blue focus box. `VCombobox` is the opt-in macOS-style combo box and uses the macOS up/down chevron
pair. A plain `QComboBox` retains the Fusion fallback behavior. `VCombobox` still inherits Qt's
model, view, popup lifecycle, keyboard behavior, and accessibility implementation. It enables Qt's
menu-popup path on every platform, which uses Qt's own menu delegate and aligns the current row with
the collapsed control. An application-owned delegate remains authoritative and is never replaced.

The popup model, view palette, frame, selection, current index, and scrolling remain Qt-owned. The
only private-container seam is the one also used by Breeze: the top-level popup requests a
translucent backing surface and reserves a small content margin so `VStyle` can paint antialiased
corners without a binary `QRegion` mask. A non-editable collapsed control is unframed at rest: only
its label and a double-chevron on a circular, borderless surface are visible. Hover expands that
same surface color into one borderless rounded rectangle and suppresses the separate circle.
Editable controls retain an input frame. The collapsed hover surface and editable frame use the
dedicated `comboBoxCornerRadius` metric (6 logical pixels), while popup curvature comes from
`comboBoxPopupCornerRadius` (16 logical pixels on macOS and 10 elsewhere). `QMenu` uses
`menuCornerRadius`, while `popoverCornerRadius` remains exclusive to `VPopover`. `PE_PanelMenu`
paints one device-pixel, partially transparent outline and the private `QFrame` is disabled, so the
surface never composites a second border. These surfaces are painted through `QStyle`, not QSS.

`VCombobox` popup items follow macOS menu semantics. Qt's menu delegate supplies the current item as
`checked`; VStyle renders it as a leading checkmark and reserves the same leading state column for
every row. The active row uses the resolved theme accent as a solid background with automatically
contrasting text and checkmark colors. Item icons share the state column, preserving one stable text
origin without per-row geometry or model scans. The state column, checkmark stroke, row height, and
margins scale from the active font and icon size. The compact gutter owns its spacing once; Qt's
menu-delegate icon padding is not added again on top of VkUI's explicit column gap.

Checked checkbox and radio indicators use device-pixel-aligned outlines, accent-colored selected
edges, and white marks. `vkui::setControlSize()` gives these standard widgets Small, Regular, and
Large indicator metrics while preserving Qt's built-in semantics. Named sizes follow the global
interface-text scale; `setControlExtent()` is the deliberate absolute-size escape hatch. Standard
button/combo icons and automatic tree icons use responsive semantic metrics, but explicit Qt icon
sizes remain unchanged.

Combo-box width follows Qt's mature `QComboBox::SizeAdjustPolicy`: use `AdjustToContents` for small,
stable models and `AdjustToMinimumContentsLengthWithIcon` for large or frequently changing models.
The popup asks Qt to grow to its longest full item, then Qt clamps that geometry to the available
screen. `CT_ComboBox` and popup painting consume the same horizontal-layout budget, so ordinary
labels are fully visible and elision occurs only after the available screen becomes the real
constraint. `VCombobox::setElideMode()` selects left, middle, right, or no elision for both popup
rows and the constrained collapsed label; right elision is the macOS-style default for general
labels, while middle elision is useful for file paths. The collapsed label is always clipped before
the chevron column. Spin boxes and editable `VCombobox` instances paint one outer frame; their
private line-edit child remains surface-less.

The control shown before the menu opens is the **collapsed** (or closed) combo box. For a
non-editable `VCombobox`, it is not a second child widget: Qt paints `CC_ComboBox` and
`CE_ComboBoxLabel` on the `VCombobox` itself. Only an editable combo box owns a child `QLineEdit`.
Layouts should therefore consume `minimumSizeHint()` or `sizeHint()` instead of assigning a fixed
width. `VCombobox` uses Qt's `QSizePolicy::Minimum` horizontally, making its dynamic `sizeHint()` a
layout floor while still allowing the control to grow. VkUI's `CT_ComboBox` measurement reserves
the larger of the popup state-column budget and the collapsed double-chevron budget, so values such
as “System” and “Automatic” remain complete at the reported content width. Applications that need a
bounded large-model control should select `AdjustToMinimumContentsLengthWithIcon` and set
`minimumContentsLength()`; constrained controls then use `elideMode()` by design.
