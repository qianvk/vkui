# Changelog

All notable changes are documented here. This project follows Semantic Versioning once public
releases are tagged.

## Unreleased

- Added coalesced runtime repolish so existing widgets and QSS overlays switch appearance fully.
- Added optional palette-aware QSS file overlays without making QSS the default theme engine.
- Added semantic application-command SVG symbols for save, reset, duplicate, media, focus, project,
  rename, reveal, and remove actions.
- Added configurable combo-box label elision and corrected content-aware width accounting.
- Removed the nested private-editor frame from spin boxes and editable combo boxes.

## 0.1.0 - 2026-07-12

- Added Core and Widgets CMake package components.
- Added automatic, light, and dark appearance resolution with semantic tokens.
- Added generation-aware, theme-colored SVG icons and unified motion specifications.
- Added the Fusion-backed `VkStyle` for standard Qt Widgets.
- Added `VkSwitch`, `VkSegmentedControl`, and anchor-aware `VkPopover`.
- Added English and Simplified Chinese catalogs, gallery, unit tests, documentation, and CI.
- Added eight selectable macOS-style accent colors with live Gallery controls.
- Refined Switch, segmented-control, focus, combo-box popup, and motion rendering.
- Corrected high-DPI Popover shadow alignment and reduced blur-cache memory and invalidation work.
- Added clangd/Neovim compilation-database and background-indexing support.
- Replaced ComboBox popup masks with antialiased translucent-window compositing and one background.
- Added Small, Regular, and Large sizing for switch, checkbox, and radio indicators.
- Added crisp white checkbox/radio marks and a wider, slower, better-fitted switch thumb.
- Added common toggle and macOS-style symbols plus a Gallery-only Fira Code Nerd Font showcase.
- Added named 14/18/22-pixel control-size tokens and exact 8–128-pixel per-widget overrides.
- Added explicit Nerd glyph colors, open/closed file-tree folders, and minimalist branch painting.
- Made the Gallery icon grid scrollable with non-compressible symbol cells.
