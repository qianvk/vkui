# Window-scoped panels

`VPanelManager` is the QWidget runtime adapter for one top-level window. It owns semantic panel
identity, numbering, expanded state, last fully-expanded proportions, `VSplitter` bindings, and
the built-in layout chooser. It is deliberately an instance member rather than a global registry:
independent windows can therefore use the same panel IDs without sharing state or lifetime.

```cpp
#include <vkui/widgets/controls/VSplitter.h>
#include <vkui/widgets/panels/VPanelManager.h>

class WorkspaceWindow final : public QWidget {
public:
    WorkspaceWindow()
        : panelManager_(*this) {
        auto* splitter = new vkui::VSplitter(Qt::Horizontal, this);
        navigationPanel_ = new QWidget(splitter);
        contentPanel_ = new QWidget(splitter);
        splitter->setChildrenCollapsible(true);
        splitter->addWidget(navigationPanel_);
        splitter->addWidget(contentPanel_);
        splitter->setSizes({240, 760});

        panelManager_.setLayoutRoot(splitter);
        panelManager_.registerPanel("navigation", tr("Navigation"), navigationPanel_, 1);
        panelManager_.registerPanel("content", tr("Content"), contentPanel_, 2);
    }

private:
    QWidget* navigationPanel_ = nullptr;
    QWidget* contentPanel_ = nullptr;
    vkui::VPanelManager panelManager_;
};
```

A secondary-button click on any managed splitter handle opens a frameless transient chooser at 60%
of its owner window. It uses Qt's exclusive popup input grab, so other application controls cannot
be operated while it is open and any real outside click closes it. A primary click collapses the
panel physically before the handle (left for a horizontal splitter, top for a vertical splitter),
while dragging remains a normal Qt resize. The chooser preserves the layout root's aspect ratio and
uses the last geometry where every panel was expanded, so collapsed panels keep their proper
position and proportion in the complete-layout preview. Splitter input surfaces are projected to
their centerlines, so the preview contains only the visual separator and no interaction gutter.
Each preview contains only its stable number centered in the panel geometry; expanded and collapsed
states remain distinguishable through their surface treatment rather than labels. Clicking a
preview calls the same manager toggle API used by application commands and closes the chooser. Its
client area is the panel diagram: there is no title bar, heading, outer canvas, or layout margin.

The manager never hides registered widgets. It projects visibility into `QSplitter::setSizes()`
with a zero final extent for collapsed panels, preserving the splitter topology and handle. Toggle
transitions use the current theme's emphasized enter and exit motion. A repeated toggle retargets
from the currently rendered splitter sizes, so an interrupted transition remains continuous.
Disabling animations through the global theme policy applies the target sizes immediately. At
least one panel remains expanded; toggling the sole visible panel swaps visibility to another
registered panel. Rebinding an existing semantic ID after a UI reconstruction preserves its
number, proportion, and collapse state.

Each direct splitter panel is hosted by a private clipping slot. During ordinary layout and manual
resize the slot reports the panel's original minimum size, preserving normal `QSplitter`
constraints. Only while a toggle transition is running does the slot report a zero minimum. This
makes every extent between zero and the application minimum reachable without modifying the
registered widget, eliminating `QSplitter`'s minimum-size-to-zero snap in both directions.

The manager installs one client-area handle along the window's left edge. Following vkery's
interaction pattern, its 20-pixel inner hit band stays visually empty until hover, then reveals a
short 6-by-40-pixel primary-color pill that follows the pointer vertically. A primary click expands
the collapsed panel nearest that location; a secondary click opens the chooser. This ordinary
QWidget child does not inspect, widen, replace, or intercept the platform's native resize border.
When a collapsed splitter handle occupies the same geometry, the left-edge affordance owns the
interaction, so the short pill replaces the splitter's full-height one-pixel hover separator.

If the host also uses `VWindowAgent` title bars, register `windowEdgeHandles()` as ordinary
hit-test-visible children. This prevents the left handle's title-bar segment from being interpreted
as drag input; it does not change native resize behavior:

```cpp
for (QWidget* handle : panelManager_.windowEdgeHandles()) {
    windowAgent_.setHitTestVisible(handle, true);
}
```

`VPanelManager` complements the renderer-independent `VkUI::Panel` split-tree model. Applications
that only need QWidget panel lifecycle and a chooser can use `VPanelManager` alone; interaction
hosts may project their richer split tree through the same window-scoped adapter.
