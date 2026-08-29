# Splitter

`VSplitter` is a theme-aware `QSplitter` for borderless panel layouts. Use it exactly like
`QSplitter`:

```cpp
#include <vkui/widgets/controls/VSplitter.h>

auto* splitter = new vkui::VSplitter(Qt::Horizontal, parent);
splitter->setChildrenCollapsible(false);
splitter->addWidget(navigationPanel);
splitter->addWidget(contentPanel);
```

The handle owns a five-logical-pixel interaction surface but paints only its center pixel. This is
the same practical grab extent as Qt's tiny-handle mode without overlapping either adjacent panel.
The handle observes mouse events on its own top-level `QWindow` and derives hover and cursor state
from its exact geometry and the current pointer position. It deliberately does not use a queued
mouse event's historical coordinates for cursor ownership. While the pointer is inside, the
splitter also supplies the same inherited cursor to adjacent alien widgets, preventing a transient
receiver change from applying Arrow. The previous cursor is restored immediately on exit. This
avoids stale enter/leave behavior without widening the response area or installing an
application-wide filter. Hover paints a crisp one-pixel separator across the complete panel
boundary using the current accent color, and pressing the handle uses the accent pressed token.

Dragging, opaque resize, keyboard behavior, right-to-left layout, and saved splitter state remain
owned by `QSplitter`. The handle selects Qt's standard horizontal or vertical splitter cursor.

`handleClicked(int, Qt::MouseButton)` is emitted for a primary or secondary click released without
crossing the platform drag threshold. `VPanelManager` maps a primary click to collapsing the
physically preceding panel (left for a horizontal splitter, top for a vertical splitter) and a
secondary click to its proportional layout chooser. Resize behavior remains entirely with Qt.
The click signal is queued after mouse release, allowing Qt to finish mouse-grab and platform-cursor
reconciliation before an application changes splitter geometry. Cursor updates remain inside Qt's
public `QWindow` abstraction. `VSplitter` does not install application filters, poll pointer
geometry, or call platform cursor APIs.
