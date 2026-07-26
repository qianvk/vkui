# Icons

Call `vkui::icon(VkSymbol, VkIconRole)` to obtain a theme-aware `QIcon`. The project ships one
original SVG per symbol. Literal `#000001` and `#000002` colors in those sources represent the
primary and secondary semantic channels; the private icon engine substitutes resolved theme colors
when it renders.

The engine honors `QIcon::Mode` and `QIcon::State`. Rendered pixmaps are keyed by symbol, role,
logical size, device-pixel ratio, mode, state, and theme generation. A theme change therefore cannot
return a stale light- or dark-appearance rendering. Rendering at the requested DPR keeps vector
artwork crisp without parallel asset directories.

Icon-only controls still need a meaningful accessible name or visible label relationship.

The built-in MIT-licensed SVG set includes navigation, files, projects, save, reset, duplicate,
image/background, focus, rename, reveal, remove, toggles, sidebar, grid/list, edit, trash,
download/upload, lock, and visibility symbols. These application-command icons share a 24-by-24
view box, rounded outline geometry, semantic two-channel color, and deterministic metrics across
platforms.

`VkUI::Core` also bundles Fira Code Nerd Font for dense file-system surfaces. Applications use
`VkFileGlyph`, never a private code point:

```cpp
const auto metrics = vkui::fileIconMetrics(tree->font(), tree->devicePixelRatioF());
tree->setIconSize(metrics.glyphSlotSize);
item->setIcon(vkui::fileIcon(vkui::VkFileGlyph::FolderClosed,
                             vkui::VkIconRole::Accent));
```

The public set covers closed/open folders and generic, text, source, image, PDF, and archive files.
The font is registered process-locally on first use after `QGuiApplication` construction. Repeated
initialization is idempotent, and the operating system font collection is never modified.

`fileIcon()` accepts a live semantic `VkIconRole`, a live application `QPalette::ColorRole`, or an
explicit `QColor`. Theme generation and palette cache keys participate in raster caching, while the
icon engine normalizes the Qt 6.6/6.7 high-DPI `scaledPixmap()` convention. For a delegate with a
widget-specific palette, call `drawFileGlyph()` with the current palette during painting.

`fileIconMetrics()` has no process-wide font or DPR cache. Recompute it for `QEvent::FontChange`,
`QEvent::ApplicationFontChange`, and device-pixel-ratio changes; it returns both logical and physical
slot sizes. The stable maximum glyph slot prevents file-tree columns from moving when a folder
opens or a file type changes.

File glyphs remain separate from primary desktop commands. Owned SVG assets are still preferred for
buttons, menus, and general application actions because they provide more predictable geometry and
semantic two-channel rendering. See `THIRD_PARTY_NOTICES.md` for the bundled font's upstream
licenses.
