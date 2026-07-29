// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QFlags>
#include <QtCore/QMargins>
#include <QtCore/QMetaType>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtWidgets/QWidget>
#include <memory>
#include <vkui/VkUiGlobal.h>

class QKeyEvent;
class QPaintEvent;

namespace vkui {

/** The physical side of an anchor on which a popover is placed. */
enum class VkPopoverPlacement {
    Automatic,
    Below,
    Above,
    Right,
    Left,
};

/** Directions to which an optional widget boundary is applied. */
enum class VkPopoverBoundaryPlacementFlag {
    None = 0x00,
    Below = 0x01,
    Above = 0x02,
    Right = 0x04,
    Left = 0x08,
    All = 0x0F,
};
Q_DECLARE_FLAGS(VkPopoverBoundaryPlacements, VkPopoverBoundaryPlacementFlag)

/**
 * Alignment on the axis perpendicular to the placement direction.
 *
 * Start and End are layout-direction aware. For a popover above or below its
 * anchor, Start keeps the leading body edge stable while content width
 * changes; the arrow remains aimed independently at the anchor.
 */
enum class VkPopoverCrossAxisAlignment {
    Center,
    Start,
    End,
};

/** Independent reasons for dismissing an open popover. */
enum class VkPopoverClosePolicyFlag {
    None = 0x00,
    OutsideClick = 0x01,
    EscapeKey = 0x02,
    AnchorDestroyed = 0x04,
    WindowDeactivated = 0x08,
};
Q_DECLARE_FLAGS(VkPopoverClosePolicy, VkPopoverClosePolicyFlag)

class VkPopoverPrivate;

/**
 * A top-level, anchor-aware popup with automatic edge avoidance.
 *
 * VkPopover must be created and used on the GUI thread. The optional parent is
 * used for QObject lifetime and transient-window association; the popover is
 * always a fixed-size, frameless popup window. Outside dismissal is handled by
 * VkPopover itself so the clicked widget still receives the original event.
 */
class VKUI_WIDGETS_EXPORT VkPopover final : public QWidget {
    Q_OBJECT

  public:
    explicit VkPopover(QWidget* parent = nullptr);
    ~VkPopover() override;

    /**
     * Installs the widget displayed by the popover.
     *
     * The popover takes ownership by reparenting content. Replacing the
     * content deletes the previously installed widget. Passing nullptr clears
     * the popover.
     */
    void setContentWidget(QWidget* content);
    [[nodiscard]] QWidget* contentWidget() const noexcept;

    /**
     * Sets the content size requested before screen and boundary constraints.
     *
     * The final content geometry may be smaller. An empty size restores the
     * content widget's size hints.
     */
    void setPreferredContentSize(const QSize& size);
    [[nodiscard]] QSize preferredContentSize() const noexcept;

    /**
     * Overrides the padding between the body and content.
     *
     * Negative values restore the theme default on the corresponding edge.
     */
    void setContentMargins(const QMargins& margins);
    [[nodiscard]] QMargins contentMargins() const noexcept;

    /**
     * Recalculates an open popover after direct changes to content size hints
     * or constraints. setPreferredContentSize() schedules this automatically.
     */
    void refreshGeometry();

    /**
     * Sets the first placement direction to try.
     *
     * The direction is a preference rather than a hard constraint. VkPopover
     * flips to another edge before shrinking content against its boundary.
     */
    void setPreferredPlacement(VkPopoverPlacement placement);
    [[nodiscard]] VkPopoverPlacement preferredPlacement() const noexcept;

    /** Returns the direction selected by the latest successful layout. */
    [[nodiscard]] VkPopoverPlacement resolvedPlacement() const noexcept;

    void setCrossAxisAlignment(VkPopoverCrossAxisAlignment alignment);
    [[nodiscard]] VkPopoverCrossAxisAlignment crossAxisAlignment() const noexcept;

    /**
     * Restricts placement to a widget's current global rectangle.
     *
     * The boundary is resolved whenever either widget moves or resizes.
     * Passing nullptr restores the screen's available geometry.
     */
    void setBoundaryWidget(QWidget* boundary);
    [[nodiscard]] QWidget* boundaryWidget() const noexcept;

    /**
     * Selects which resolved directions use boundaryWidget().
     *
     * All directions are constrained by default. This allows controls near a
     * panel edge to constrain Below while still flipping Above on screen.
     */
    void setBoundaryPlacements(VkPopoverBoundaryPlacements placements);
    [[nodiscard]] VkPopoverBoundaryPlacements boundaryPlacements() const noexcept;

    /**
     * Sets the enabled dismissal reasons. All four reasons are enabled by
     * default; VkPopoverClosePolicyFlag::None leaves dismissal to the caller.
     */
    void setClosePolicy(VkPopoverClosePolicy policy);
    [[nodiscard]] VkPopoverClosePolicy closePolicy() const noexcept;

    /** Returns true while the popover is opening or fully open. */
    [[nodiscard]] bool isOpen() const noexcept;

  public Q_SLOTS:
    /** Opens for the complete rectangle of anchor. */
    void openFor(QWidget* anchor);

    /**
     * Opens for a sub-rectangle expressed in anchor-local coordinates.
     * An empty rectangle selects the complete anchor rectangle.
     */
    void openFor(QWidget* anchor, const QRect& anchorRectInAnchor);

    /**
     * Atomically toggles this popover for an anchor or anchor sub-rectangle.
     *
     * Repeated pointer clicks are handled correctly even when the platform
     * hides a native popup before forwarding the anchor button's click.
     * Returns true when the resulting logical state is open.
     */
    bool toggleFor(QWidget* anchor);
    bool toggleFor(QWidget* anchor, const QRect& anchorRectInAnchor);

    void closeAnimated();
    void closeImmediately();

  Q_SIGNALS:
    void aboutToOpen();
    void opened();
    void aboutToClose();
    void closed();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

  private:
    Q_DISABLE_COPY_MOVE(VkPopover)

    std::unique_ptr<VkPopoverPrivate> d;
};

} // namespace vkui

Q_DECLARE_OPERATORS_FOR_FLAGS(vkui::VkPopoverClosePolicy)
Q_DECLARE_OPERATORS_FOR_FLAGS(vkui::VkPopoverBoundaryPlacements)
Q_DECLARE_METATYPE(vkui::VkPopoverPlacement)
Q_DECLARE_METATYPE(vkui::VkPopoverBoundaryPlacementFlag)
Q_DECLARE_METATYPE(vkui::VkPopoverBoundaryPlacements)
Q_DECLARE_METATYPE(vkui::VkPopoverCrossAxisAlignment)
Q_DECLARE_METATYPE(vkui::VkPopoverClosePolicyFlag)
Q_DECLARE_METATYPE(vkui::VkPopoverClosePolicy)
