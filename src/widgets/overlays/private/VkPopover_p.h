// SPDX-License-Identifier: MIT

#pragma once

#include "../../animation/private/VkWidgetAnimation_p.h"
#include "VkPopoverPlacementEngine_p.h"
#include "VkPopoverShadowCache_p.h"

#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtGui/QPainterPath>

class QEvent;
class QKeyEvent;
class QPaintEvent;
class QScreen;
class QWidget;

namespace vkui {

class VkPopover;

struct VkPopoverGeometryMetrics final {
    qreal contentPadding = 0.0;
    qreal cornerRadius = 0.0;
    qreal arrowWidth = 0.0;
    qreal arrowDepth = 0.0;
    qreal screenMargin = 0.0;
    qreal anchorGap = 0.0;
    qreal shadowRadius = 0.0;
    qreal shadowOffset = 0.0;

    friend bool operator==(const VkPopoverGeometryMetrics&,
                           const VkPopoverGeometryMetrics&) = default;
};

class VkPopoverPrivate final : public QObject {
  public:
    enum class State {
        Closed,
        Opening,
        Open,
        Closing,
    };

    explicit VkPopoverPrivate(VkPopover* popover);
    ~VkPopoverPrivate() override;

    void setContentWidget(QWidget* content);
    [[nodiscard]] QWidget* contentWidget() const noexcept;

    void setPreferredPlacement(VkPopoverPlacement placement);
    void setClosePolicy(VkPopoverClosePolicy policy) noexcept;

    void openFor(QWidget* anchor, const QRect& anchorRectInAnchor);
    void closeAnimated();
    void closeImmediately();
    void shutdown();

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool handleEvent(QEvent* event);
    [[nodiscard]] bool handleKeyPress(QKeyEvent* event);
    void paint(QPaintEvent* event);

    VkPopoverPlacement preferredPlacement = VkPopoverPlacement::Automatic;
    VkPopoverClosePolicy closePolicy =
        VkPopoverClosePolicyFlag::OutsideClick | VkPopoverClosePolicyFlag::EscapeKey |
        VkPopoverClosePolicyFlag::AnchorDestroyed | VkPopoverClosePolicyFlag::WindowDeactivated;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void attachOpenFilters();
    void detachOpenFilters();
    void removeAnchorFilters();
    void reconnectWindowAndScreen();
    void setObservedScreen(QScreen* screen);
    void queueReposition();
    [[nodiscard]] bool repositionNow();
    [[nodiscard]] QRectF anchorGlobalRect() const;
    [[nodiscard]] QSizeF desiredContentSize() const;
    [[nodiscard]] QScreen* screenForAnchor(const QRectF& globalAnchor) const;

    void applyOpacityFrame(qreal opacity);
    void startOpenAnimation();
    void finishOpening();
    void finishClosing();
    void handleAnchorDestroyed();

    VkPopover* q = nullptr;
    QPointer<QWidget> content;
    QPointer<QWidget> anchor;
    QPointer<QWidget> anchorWindow;
    QRect anchorLocalRect;
    QPointer<QScreen> observedScreen;

    QMetaObject::Connection themeChangedConnection;
    QMetaObject::Connection contentDestroyedConnection;
    QMetaObject::Connection anchorDestroyedConnection;
    QMetaObject::Connection windowScreenConnection;
    QMetaObject::Connection screenAvailableConnection;
    QMetaObject::Connection screenGeometryConnection;

    State state = State::Closed;
    bool filtersAttached = false;
    bool repositionQueued = false;
    bool internalHide = false;
    qreal currentOpacity = 1.0;
    VkPopoverGeometryMetrics geometryMetrics;

    VkPopoverPlacementResult finalPlacement;
    QPainterPath finalPath;
    VkWidgetAnimation animation;
    VkPopoverShadowCache shadowCache;
};

} // namespace vkui
