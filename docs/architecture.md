# Architecture

## Preserve Qt Widgets

vkui is a visual foundation, not a second widget toolkit. Qt already provides mature controls with
years of work in keyboard interaction, focus, input methods, models and views, accessibility, and
platform integration. Wrapping each control would duplicate that behavior and make applications
harder to maintain. `VStyle` therefore paints supported standard widgets through Qt's style API
and delegates unsupported elements to its explicit Fusion base style.

Public widget subclasses are reserved for explicit behavior gaps. They retain the closest Qt base
class as their behavioral contract; for example, `VSplitter` customizes `QSplitter::createHandle()`
without replacing Qt's resizing, cursor, saved-state, or right-to-left behavior. Search fields,
navigation sidebars, cards, and settings rows remain compositions made from standard widgets in
application code.

## Module graph

The interaction stack is intentionally layered rather than folded into the visual Core:

```text
Buffer (pure C++ data plane)
   ^
   |
Interaction (modal grammar, commands, semantic windows)
   ^                         ^
   |                         |
Panel (split geometry)       |
   ^                         |
   +---- InteractionWidgets -+
              |
           Widgets
```

- `VkUI::Buffer` owns ordinary text, a bounded read-only provider reference, or an editable-session
  reference plus one immutable snapshot. It has no Qt dependency and no renderer state.
- `VkUI::Interaction` is the authoritative modal state machine. It consumes canonical semantic
  key values, owns buffers, branching edit history, clean points, and logical windows, and emits
  host actions. It does not synthesize or forward Qt key events. `bufferHistory()` exposes only an
  immutable availability/clean-state snapshot; `undo()`, `redo()`, and `setBufferModified()` remain
  the mutation boundary, so a QWidget host never maintains a competing undo stack. Native
  non-modal surfaces synchronize their UTF-16 anchor/active selection through
  `setViewSelectionAtOffsets()`; Core records both boundaries in the same undo node as the text
  delta, while modal Visual selection remains a separate Neovim state. Explicit document reloads
  call `resetBufferHistory()` even when text is byte-identical, so the host cannot silently retain
  a pre-reload branch.
- `VkUI::Panel` is a renderer-independent split tree. Stable IDs and immutable snapshots keep
  persistence, chooser overlays, resizing, and QWidget projection consistent.
- `VkUI::InteractionWidgets` is the only layer that knows QWidget. Its block/panel leases make
  plugin teardown deterministic, and its navigator adapts standard controls and model/view
  surfaces to semantic movement and activation.

Lua, JSON, or another configuration language is host policy. A host materializes validated
`MappingDefinition` and plugin specs through the public Interaction API; VkUI never embeds a
scripting runtime. Structured diagnostics follow the same inversion: VkUI emits records through
one optional sink while the host chooses storage and retention.

## External editable documents

`registerExternalSessionBuffer()` is the zero-copy integration boundary for a host document core.
The host's `IEditableTextSession` remains the sole mutable text, selection, revision, clean-point,
and undo/redo authority. VkCore retains only the session and an immutable `ITextSnapshot`; it does
not allocate `OwnedTextStorage`, a resident line-start vector, or a local `UndoHistory` for that
buffer. `bufferAuthority()` exposes these structural facts for diagnostics and regression tests.

Snapshot access is UTF-16 and revision-pinned. Scalar reads use a caller-bounded cache, range reads
are capped, and line queries are delegated to the snapshot. The private text and line-index facades
preserve the owned-buffer algorithms without manufacturing a contiguous external string.
Whole-document `buffer()` snapshots consequently remain available only for owned buffers. External
modal mutation is currently an explicit LF-only contract. The snapshot descriptor carries an O(1)
`modalEditingLfOnly` capability; registration rejects unknown, CR/CRLF, or mixed sources, and Core
rechecks every committed replacement snapshot before adopting it. This gate avoids an O(N)
registration scan while several transform/operator paths do not yet preserve original terminator
bytes. Line navigation itself can still describe those documents without a resident line-start
table.

Every external command crosses the mutable boundary exactly once. A transaction contains
sequential UTF-16 replacements, expected revision, direction-preserving selection before and after,
and an optional grouping identity. The session validates the complete evolving candidate and
publishes text, history, and selection atomically and returns that commit's immutable snapshot in
the same result. Visual Block propagation and dot replay use this batch boundary instead of
partially committing one row at a time. Undo and redo are delegated once and return their sequential
committed edits plus the post-replay snapshot, so VkCore can update marks, other windows, and host
edit events without comparing or copying the document. A rejected stale, read-only, invalid-range,
or invalid-selection transaction changes no VkCore projection state. If a host commits without a
usable snapshot, or a snapshot later violates its bounded read/line contract, Core discards the old
projection, emits `ExternalAuthorityDesynchronized` with the authority revision and size, and
refuses further commands for that buffer until the host re-registers it.

Search uses the same ICU regular-expression engine for owned and external buffers, so accepted
syntax and Unicode behavior do not depend on the storage representation. Its `UText` provider
receives fixed-size UTF-16 chunks (including safe surrogate-pair boundaries); external search never
creates a whole-document subject string. Unsupported/invalid ICU patterns produce an explicit
`InputError` for both authority modes.

## Core, Widgets, and Window

`VkUI::Core` has no QWidget subclasses and depends only on Qt Core, Gui, and Svg. It owns resolved
appearance, semantic token value types, icon rendering, and motion policy. Non-widget code can use
this layer without taking a Qt Widgets dependency.

`VkUI::Widgets` publicly links Core and depends on Qt Widgets. Its three distinct subsystems are:

- `VStyle`, which integrates with Qt's standard style contracts;
- controls, which add missing input behaviors while building on Qt button semantics; and
- overlays, where `VPopover` owns independent top-level-window and anchor-monitoring behavior.

The Popover is not owned by `VStyle`: opening, placement, focus, and close policy are component
behavior rather than style behavior. Keeping it independent also allows downstream applications to
use it without making style installation part of its lifetime.

`VkUI::Window` is a separate optional boundary. Applications compose a
`VWindowAgent` into any top-level `QWidget` or `QDialog`; the module does not
impose a `QMainWindow` content model. Native full-content behavior uses public
Qt window contracts plus per-window AppKit or Win32 integration. Its typed
controller owns title-bar policy and native-handle lifecycle, while one
platform backend owns only the corresponding `NSWindow` or `HWND`. No global
native-method replacement or Qt private ABI is used. Keeping this module
outside `VkUI::Widgets` lets ordinary controls remain portable and free of
platform framework dependencies.

## Public policy, private mechanisms

Semantic tokens and motion specifications are public because application UI needs the same values
as the library. Animation drivers are private because their QObject ownership, interruption, and
frame scheduling are implementation details. Painting helpers are private for the same reason:
exposing geometry primitives would freeze rendering internals and encourage controls to bypass the
stable theme API.

`VSwitch` inherits `QAbstractButton`, so checked state, signals, mouse activation, Space-key use,
focus, and accessible button semantics have one source of truth. `VSegmentedControl` composes
private checkable `QAbstractButton` children in an exclusive `QButtonGroup`; it does not reimplement
generic button behavior or expose the child type. `VSplitter` inherits `QSplitter` and replaces
only its handle painter; Qt continues to own hit testing and expands the one-pixel layout handle
over adjacent panels to provide a practical grab area without a gutter.

## Theme generations and caches

`VkThemeManager` publishes an immutable resolved theme. Whenever effective appearance or resolved
tokens change, it increments a monotonic generation and emits `themeChanged` with flags identifying
the changed token groups. Color-only changes apply the palette and update visible surfaces without
repolishing the entire widget tree. Metric and typography changes use a coalesced structural refresh;
motion changes remain local to animation policy.

Semantic icon pixmaps include the narrower color generation in their cache key. Non-color token
changes therefore retain valid rendered symbols, while a palette change makes stale renderings
naturally unreachable without flushing unrelated cache entries. Menu composition and theme refresh
coordination are private collaborators rather than responsibilities accumulated in `VStyle`.

`VCombobox` follows the same ownership boundary as KDE Breeze: Qt continues to own the popup
model/view, menu delegate, current index, selection, scrolling, geometry, input, and accessibility
behavior. VkUI selects Qt's menu-popup path and handles the corresponding style options; it never
replaces an application delegate. Translucent composition is enabled only on a `VCombobox` private
popup container, which is required for antialiased top-level corners and does not alter the view
contract. Plain `QComboBox` instances remain on the proxy style's Fusion fallback path.

VkUI does not support application-wide QSS on top of `VStyle`. Painting, geometry, hit testing, and
animation remain a single typed `QStyle`/`QPainter` pipeline; application compositions consume the
same semantic tokens and palette roles.

## Header boundary

Installed headers live under `include/vkui`. `Core.h`, `Widgets.h`, `Window.h`, `Buffer.h`,
`Interaction.h`, `Panel.h`, and `InteractionWidgets.h` are convenience umbrellas;
the headers beneath `core`, `widgets/style`, `widgets/controls`, and `widgets/overlays` are the
public API. Headers beneath any `src/**/private` directory are implementation details, may change
without notice, and are never installed. No ABI stability is promised before 1.0.0.
