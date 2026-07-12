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

Nerd Font glyphs remain a Gallery-only option for dense file-type or developer-tool surfaces. They
are not used for primary desktop commands: font fallback, private-codepoint mapping, baseline
alignment, and glyph-version drift make them less predictable than owned SVG assets for buttons,
menus, accessibility states, and high-DPI rendering. The font is not part of either library target.
See `THIRD_PARTY_NOTICES.md` for its upstream licenses.

Gallery Nerd glyph icons support either a live semantic `VkIconRole` or an explicit `QColor`. The
Icons page includes an interactive folder-color picker. The file-system tree uses the Nerd Font
closed/open folder glyphs and a small `QTreeView::drawBranches()` override to render continuous
leading and terminal connector lines without native disclosure arrows, frames, selection chrome,
or scroll bars.
