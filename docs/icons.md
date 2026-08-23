# Icons

Call `vkui::icon(VkSymbol, VkIconRole)` to obtain a theme-aware `QIcon`. The project ships one
original SVG per symbol. `resources/icons/manifest.json` is the single source of truth: CMake
validates it and generates the public `VkSymbol` enum, the constant-time resource registry, the Qt
resource collection, and the Gallery's curated descriptor table. Adding a symbol does not require
editing parallel C++ switches or resource lists.

Only data required by those consumers is retained. A normal entry stores `id` and `asset`. A
Gallery-curated entry additionally stores `collection`, a display `label`, and the upstream
`sourceName`. Font code points, glyph advances, baselines, pack names, and conversion-tool element
IDs are deliberately excluded because the runtime SVG pipeline does not use them. Configuration
fails on unknown fields, duplicate IDs or assets, missing SVGs, and unsupported literal colors.

Literal `#000001` and `#000002` colors in SVG sources represent the primary and secondary semantic
channels. The private icon engine converts those channels into a color-independent, antialiased
geometry mask once per symbol and physical raster size, then applies the requested theme, palette,
or explicit colors without parsing the SVG again.

The engine honors `QIcon::Mode` and `QIcon::State`. Rendered pixmaps are keyed by symbol, role,
logical size, device-pixel ratio, mode, state, and color generation. A color change therefore cannot
return a stale light- or dark-appearance rendering, while metric and motion changes retain valid
pixmaps. Rendering at the requested DPR keeps vector artwork crisp without parallel asset
directories.

Icon-only controls still need a meaningful accessible name or visible label relationship.

The built-in MIT-licensed SVG set includes navigation, files, projects, save, reset, duplicate,
image/background, focus, rename, reveal, remove, toggles, sidebar, grid/list, edit, trash,
download/upload, lock, and visibility symbols. These application-command icons share a 24-by-24
view box, rounded outline geometry, semantic two-channel color, and deterministic metrics across
platforms.

File-system surfaces use the same SVG pipeline. Path mapping and layout metrics remain separate
from the rendering engine:

```cpp
const auto metrics = vkui::fileIconMetrics(tree->font());
tree->setIconSize(metrics.iconSize);
item->setIcon(vkui::icon(vkui::VkSymbol::FileFolderClosed,
                         vkui::VkIconRole::Accent));
```

The public set covers closed/open folders and generic, text, source, image, PDF, archive, and book
files. `fileSymbolForPath()` maps extensions without filesystem I/O. `icon()` accepts a live
semantic `VkIconRole`, a live application `QPalette::ColorRole`, or an explicit `QColor`.

The curated filled set adds `GearFilled`, `CloudFilled`, and `CloseCircleFilled`. The selected
`fa-folder`, `fa-folder_open`, and `fa-file` outlines are shared with `FileFolderClosed`,
`FileFolderOpen`, and `FileGeneric`; duplicate enum values and duplicate SVG files are deliberately
avoided. Every promoted outline uses a canonical 24-by-24 view box and records its Nerd Fonts source
symbol in the SVG metadata comment.

SVG source bytes and intrinsic metadata are initialized once per `VkSymbol` and shared by all
`QIconEngine` clones. A bounded mask cache reuses color-independent raster geometry across theme
changes, and a second bounded cache stores final colored pixmaps. Color generation, explicit color
identity, and palette cache keys prevent stale results, while the engine normalizes the Qt 6.6/6.7
high-DPI `scaledPixmap()` convention.

`fileIconMetrics()` has no process-wide cache. Recompute it for `QEvent::FontChange` and
`QEvent::ApplicationFontChange`. The stable square icon slot prevents file-tree columns from moving
when a folder opens or a file type changes.
