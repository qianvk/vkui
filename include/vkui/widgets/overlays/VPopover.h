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
enum class VPopoverPlacement {
    Automatic,
    Below,
    Above,
    Right,
    Left,
};

/** Directions to which an optional widget boundary is applied. */
enum class VPopoverBoundaryPlacementFlag {
    None = 0x00,
    Below = 0x01,
    Above = 0x02,
    Right = 0x04,
    Left = 0x08,
    All = 0x0F,
};
Q_DECLARE_FLAGS(VPopoverBoundaryPlacements, VPopoverBoundaryPlacementFlag)

/**
 * Alignment on the axis perpendicular to the placement direction.
 *
 * Start and End are layout-direction aware. For a popover above or below its
 * anchor, Start keeps the leading body edge stable while content width
 * changes; the arrow remains aimed independently at the anchor.
 */
enum class VPopoverCrossAxisAlignment {
    Center,
    Start,
    End,
};

/** Independent reasons for dismissing an open popover. */
enum class VPopoverClosePolicyFlag {
    None = 0x00,
    OutsideClick = 0x01,
    EscapeKey = 0x02,
    AnchorDestroyed = 0x04,
    WindowDeactivated = 0x08,
};
Q_DECLARE_FLAGS(VPopoverClosePolicy, VPopoverClosePolicyFlag)

class VPopoverPrivate;

/**
 * A top-level, anchor-aware popup with automatic edge avoidance.
 *
 * VPopover must be created and used on the GUI thread. The optional parent is
 * used for QObject lifetime and transient-window association; the popover is
 * always a fixed-size, frameless popup window. Outside dismissal is handled by
 * VPopover itself so the clicked widget still receives the original event.
 */
class VKUI_WIDGETS_EXPORT VPopover final : public QWidget {
    Q_OBJECT

  public:
    explicit VPopover(QWidget* parent = nullptr);
    ~VPopover() override;

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
     * The direction is a preference rather than a hard constraint. VPopover
     * flips to another edge before shrinking content against its boundary.
     */
    void setPreferredPlacement(VPopoverPlacement placement);
    [[nodiscard]] VPopoverPlacement preferredPlacement() const noexcept;

    /** Returns the direction selected by the latest successful layout. */
    [[nodiscard]] VPopoverPlacement resolvedPlacement() const noexcept;

    void setCrossAxisAlignment(VPopoverCrossAxisAlignment alignment);
    [[nodiscard]] VPopoverCrossAxisAlignment crossAxisAlignment() const noexcept;

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
     * All directions are constrained by default. Direction selection still
     * uses the screen's available geometry; the boundary only constrains the
     * final popup geometry after a direction has been selected. This allows a
     * panel to keep Below as the preferred direction and scroll its content,
     * while still flipping Above when the popup would cross the screen.
     */
    void setBoundaryPlacements(VPopoverBoundaryPlacements placements);
    [[nodiscard]] VPopoverBoundaryPlacements boundaryPlacements() const noexcept;

    /**
     * Sets the enabled dismissal reasons. All four reasons are enabled by
     * default; VPopoverClosePolicyFlag::None leaves dismissal to the caller.
     */
    void setClosePolicy(VPopoverClosePolicy policy);
    [[nodiscard]] VPopoverClosePolicy closePolicy() const noexcept;

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
    Q_DISABLE_COPY_MOVE(VPopover)

    std::unique_ptr<VPopoverPrivate> d;
};

} // namespace vkui

Q_DECLARE_OPERATORS_FOR_FLAGS(vkui::VPopoverClosePolicy)
Q_DECLARE_OPERATORS_FOR_FLAGS(vkui::VPopoverBoundaryPlacements)
Q_DECLARE_METATYPE(vkui::VPopoverPlacement)
Q_DECLARE_METATYPE(vkui::VPopoverBoundaryPlacementFlag)
Q_DECLARE_METATYPE(vkui::VPopoverBoundaryPlacements)
Q_DECLARE_METATYPE(vkui::VPopoverCrossAxisAlignment)
Q_DECLARE_METATYPE(vkui::VPopoverClosePolicyFlag)
Q_DECLARE_METATYPE(vkui::VPopoverClosePolicy)
