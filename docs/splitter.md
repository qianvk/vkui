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

The handle consumes one logical pixel of layout space. Qt automatically expands the input area
of a zero- or one-pixel splitter handle over the adjacent widgets, preserving a comfortable resize
target without introducing a gutter. While the pointer is inside that grab area, `VSplitter`
paints a crisp one-pixel separator across the complete panel boundary using the current accent
color. Pressing the handle uses the accent pressed token.

Dragging, cursor selection, opaque resize, keyboard behavior, right-to-left layout, and saved
splitter state remain owned by `QSplitter`.
