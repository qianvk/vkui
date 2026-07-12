// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QFlags>
#include <QtCore/QMetaType>
#include <QtCore/QRect>
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
 * always a top-level Qt::Popup window.
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

    void setPreferredPlacement(VkPopoverPlacement placement);
    [[nodiscard]] VkPopoverPlacement preferredPlacement() const noexcept;

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
Q_DECLARE_METATYPE(vkui::VkPopoverPlacement)
Q_DECLARE_METATYPE(vkui::VkPopoverClosePolicyFlag)
Q_DECLARE_METATYPE(vkui::VkPopoverClosePolicy)
