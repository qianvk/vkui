# VkSegmentedControl

`VkSegmentedControl` is a generic, single-selection group. Add text-only, icon-only, or mixed
segments, enable or disable individual items, and read or set `currentIndex`. Changes emit
`currentIndexChanged`; direct activation also emits `segmentActivated`.

Private checkable button children and an exclusive `QButtonGroup` retain normal focus, keyboard,
and accessible button behavior. Left and Right arrow keys move to the next enabled segment and
respect right-to-left layout. Insertions and removals rebuild connected geometry while keeping a
valid single selection. Use `setSegmentAccessibleName()` for icon-only segments; explicit names
remain stable when segment artwork or text changes. Version 0.1 deliberately has no multi-select or
momentary mode.

Hover does not alter the segments. The animated selected indicator is a borderless, solid semantic
accent fill, with contrast-selected text and standard `QIcon::Selected` handling. Press and keyboard
feedback remain available without introducing an accent-colored outline.
