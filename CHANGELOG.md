# Changelog

All notable changes are documented here. This project follows Semantic Versioning once public
releases are tagged.

## Unreleased

- Removed the fixed dark lower-half overlay and default semantic outline from Liquid Glass surfaces,
  and replaced the static Gallery sample with scrollable content moving beneath fixed previews.
- Added a macOS-style Clear-to-Tinted Liquid Glass preference with a conditional live Gallery
  slider, targeted cache invalidation, and borderless Gallery title-bar glass controls that retain
  refraction and optical highlights.
- Added a process-wide Liquid Glass policy, a live Regular/Clear Gallery preview, and automatic
  Qt-rendered glass materials for `QMenu`, `VCombobox` popups, and `VPopover`, with efficient opaque
  fallbacks when disabled.
- Retuned liquid glass around low-blur, high-displacement lensing, added distinct Regular and Clear
  materials, lazy backdrop observation, and symmetric edge/specular rendering.
- Added cross-platform `VLiquidGlassBackdrop` and `VLiquidGlassSurface` with shared local backdrop
  capture, cached downsampled blur, rounded-edge refraction, adaptive tint, dispersion, rim
  highlights, and quality fallbacks; applied the material to both Gallery title-bar settings.
- Added live 12-level interface-text sizing with level 3 as Default, semantic `VTextStyle` roles,
  complete standard-widget font inheritance, responsive control/icon/spacing metrics,
  non-compounding baseline font resolution, and a Things-inspired discrete Gallery slider.
- Unified ComboBox and menu popup typography with their public owner/theme fonts, fixed coalesced
  metric invalidation during text-level changes, and connected Preferences to the real text-size
  setting instead of its obsolete percentage preview.
- Added centralized responsive QWidget typography, popup content propagation, real ComboBox-owner
  lookup, and view-owned tree fonts so every inherited Gallery control follows text-size changes.
- Replaced the disclosure-only tree base and Gallery-specific tree painting with `VTreeView`,
  `VTreeItemDelegate`, and `VFileTreeView`: shared styling, delegate-authoritative icon hit testing,
  interruptible icon-only expansion, reusable branch connectors, and view-owned structural motion.
- Removed the extra top inset from the Gallery navigation panel so its tree begins exactly below
  the full-content title-bar region.
- Made Gallery title bars genuinely transparent and let every page scroll beneath
  the fixed chrome until content reaches the full-content window edge.
- Added `VSplitter` with a one-pixel layout separator, Qt-expanded grab area, and accent-colored
  full-boundary hover and drag feedback.
- Removed the visible Gallery brand heading while retaining its semantic window title.
- Added the optional `Buffer`, `Interaction`, `Panel`, and `InteractionWidgets`
  package components for renderer-independent text storage, Neovim-like modal
  input, split-layout geometry, and standard QWidget block/panel projection.
- Added generation-scoped interaction/plugin leases, structured diagnostic
  sinks, immutable panel snapshots, constrained resize/navigation math, and
  dedicated unit and installed-package coverage for the interaction stack.
- Added token-group theme invalidation with color-only repainting and coalesced structural refresh.
- Split popup composition and theme refresh out of `VStyle`, and removed the unsupported QSS layer.
- Refactored combo boxes around the KDE Breeze delegate contract while leaving popup model/view,
  palette, selection, and scrolling behavior under Qt ownership.
- Added semantic application-command SVG symbols for save, reset, duplicate, media, focus, project,
  rename, reveal, and remove actions.
- Added named, theme- and palette-aware file SVGs with adaptive file-row metrics and
  Qt 6.6-compatible high-DPI rendering.
- Added the integrated `VkUI::Window` module, multi-title-bar gallery chrome,
  application-owned close-only windows, and self-contained safe message prompts.
- Made title-bar hit-test exclusions window-scoped so controls and full-height
  splitter handles register once across overlapping title-bar regions.
- Replaced the active migrated QWindowKit path with VkUI-owned, typed AppKit and Win32 backends;
  removed Qt private ABI coupling while retaining the previous source tree as reference material.
- Made native windows resizable with always-visible system buttons by default, and delegated
  traffic-light layout entirely to AppKit during full-screen title-bar transitions.
- Added configurable combo-box label elision and corrected content-aware width accounting.
- Made the Gallery natively resizable with borderless semantic split panels, and redesigned the
  collapsed `VCombobox` as an unframed label with a circular chevron surface that expands on hover.
- Removed the nested private-editor frame from spin boxes and editable combo boxes.

## 0.1.0 - 2026-07-12

- Added Core and Widgets CMake package components.
- Added automatic, light, and dark appearance resolution with semantic tokens.
- Added generation-aware, theme-colored SVG icons and unified motion specifications.
- Added the Fusion-backed `VStyle` for standard Qt Widgets.
- Added `VSwitch`, `VSegmentedControl`, and anchor-aware `VPopover`.
- Added English and Simplified Chinese catalogs, gallery, unit tests, documentation, and CI.
- Added eight selectable macOS-style accent colors with live Gallery controls.
- Refined Switch, segmented-control, focus, combo-box popup, and motion rendering.
- Corrected high-DPI Popover shadow alignment and reduced blur-cache memory and invalidation work.
- Added clangd/Neovim compilation-database and background-indexing support.
- Replaced ComboBox popup masks with antialiased translucent-window compositing and one background.
- Added Small, Regular, and Large sizing for switch, checkbox, and radio indicators.
- Added crisp white checkbox/radio marks and a wider, slower, better-fitted switch thumb.
- Added common toggle and macOS-style SVG symbols plus a standalone temporary icon selection tool.
- Added named 14/18/22-pixel control-size tokens and exact 8–128-pixel per-widget overrides.
- Added explicit Nerd glyph colors, open/closed file-tree folders, and minimalist branch painting.
- Made the Gallery icon grid scrollable with non-compressible symbol cells.
