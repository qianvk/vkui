# VkSwitch

`VkSwitch` is an always-checkable `QAbstractButton`. Use inherited APIs such as `isChecked()`,
`setChecked()`, `toggle()`, `clicked()`, and `toggled()`. Mouse activation and the Space key retain
normal Qt behavior. `VkControlSize` selects Small, Regular, or Large design tokens. They resolve to
14, 18, and 22 logical pixels by default.

The track, thumb, hover, pressed, disabled, and focus-ring visuals derive from semantic tokens. Only
paint interpolation changes during animation; layout geometry remains stable. Retargeting begins at
the current thumb position, right-to-left layout mirrors the direction, and high-DPI painting uses
logical coordinates.

The regular track is intentionally short, and its moving thumb is a compact horizontal oval that
occupies at least half the track. Checked movement uses a slower, visible state transition. Mouse clicks do
not leave a blue ring; keyboard navigation receives a subtle neutral focus indication.

The control normally displays no text. Pair it with a `QLabel` buddy or set an accessible name.

The same size classes can be applied to standard fixed-proportion indicators without subclassing:

```cpp
vkui::setControlSize(checkBox, vkui::VkControlSize::Small);
vkui::setControlSize(radioButton, vkui::VkControlSize::Large);
```

For precise control, set an integer logical-pixel visual extent. Qt exposes checkbox and radio
indicator geometry through integer `QStyle::PixelMetric` values, so an integer DIP is the lossless
cross-platform unit for all three controls:

```cpp
vkui::setControlExtent(checkBox, 19);
vkui::setControlExtent(radioButton, 19);
switchControl.setControlExtent(19);

vkui::resetControlExtent(checkBox); // Restores the selected size token.
```

Exact extents are clamped to 8–128 logical pixels. The selected preset remains stored underneath
the override, and selecting another preset clears the override. Only the affected widget receives
`updateGeometry()` and `update()`; no object-tree traversal or global style repolish occurs.
