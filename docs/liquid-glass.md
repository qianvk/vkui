# Liquid glass

VkUI exposes liquid glass as two cooperating QWidget types rather than a graphics effect attached
to an arbitrary control:

- `VLiquidGlassBackdrop` observes and locally captures one explicit drawing source.
- `VLiquidGlassSurface` samples that provider and paints the material behind ordinary child
  widgets.

The source and surfaces should be overlapping siblings. A glass surface must not be a descendant of
its source because that would recursively capture the surface itself. This provider/surface boundary
is the same retained-layer pattern used by mature backdrop renderers, including
[AndroidLiquidGlass](https://github.com/Kyant0/AndroidLiquidGlass), while VkUI's renderer is an
independent C++/Qt implementation.

```cpp
auto* backdrop = new vkui::VLiquidGlassBackdrop(contentLayer, host);

auto* glass = new vkui::VLiquidGlassSurface(overlayLayer);
glass->setBackdrop(backdrop);
glass->setGlassStyle(vkui::VLiquidGlassStyle::regular());

auto* layout = new QHBoxLayout(glass);
layout->setContentsMargins(10, 3, 5, 3);
layout->addWidget(new QLabel(tr("Appearance"), glass));
layout->addWidget(appearanceComboBox);
```

The renderer captures only the surface rectangle plus the pixels required by blur and refraction.
It downsamples that region according to `VLiquidGlassQuality`, applies a one-to-three-pass separable
blur while retaining a sharp optical component, and adds rounded-rectangle edge refraction,
optional chromatic dispersion, saturation, adaptive tint, and symmetric rim/specular layers.
`VLiquidGlassStyle::regular()` and `clear()` provide balanced and clearer optical presets without
changing the rendering backend.

Captures and final material images remain cached until the source, surface geometry, device scale,
style, or theme changes. Source observation starts lazily on the first capture and paint events are
coalesced to one queued invalidation, so several surfaces can share one provider without
continuously polling the window. The entire path uses QPainter and public Qt APIs on every platform.

`Automatic` resolves to the balanced path. `Reduced` is suitable for low-power or large-area
surfaces, while `High` retains more source pixels. Applications with drawing state that changes
without producing a QWidget paint event should call `VLiquidGlassBackdrop::invalidate()` after
committing that state.

`VLiquidGlassSurface::setGlassEnabled(false)` disables all material painting while preserving the
container and its child-widget behavior. A missing or temporarily unavailable source instead uses
the theme-aware translucent fallback.
