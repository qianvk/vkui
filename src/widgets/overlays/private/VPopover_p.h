// SPDX-License-Identifier: MIT

#pragma once

#include "../../animation/private/VkWidgetAnimation_p.h"
#include "../../effects/private/VkShadowCache_p.h"
#include "VPopoverPlacementEngine_p.h"

#include <QtCore/QMargins>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtGui/QFont>
#include <QtGui/QPainterPath>

class QEvent;
class QKeyEvent;
class QPaintEvent;
class QScreen;
class QWidget;

namespace vkui {

class VPopover;
class VLiquidGlassBackdrop;
class VLiquidGlassSurface;

struct VPopoverGeometryMetrics final {
    qreal contentPadding = 0.0;
    qreal cornerRadius = 0.0;
    qreal arrowWidth = 0.0;
    qreal arrowDepth = 0.0;
    qreal screenMargin = 0.0;
    qreal anchorGap = 0.0;
    qreal shadowRadius = 0.0;
    qreal shadowOffset = 0.0;

    friend bool operator==(const VPopoverGeometryMetrics&,
                           const VPopoverGeometryMetrics&) = default;
};

class VPopoverPrivate final : public QObject {
  public:
    enum class State {
        Closed,
        Opening,
        Open,
        Closing,
    };

    explicit VPopoverPrivate(VPopover* popover);
    ~VPopoverPrivate() override;

    void setContentWidget(QWidget* content);
    [[nodiscard]] QWidget* contentWidget() const noexcept;
    void setPreferredContentSize(const QSize& size);
    [[nodiscard]] QSize preferredContentSizeValue() const noexcept;
    void setContentMargins(const QMargins& margins);
    [[nodiscard]] QMargins contentMargins() const noexcept;
    void refreshGeometry();

    void setPreferredPlacement(VPopoverPlacement placement);
    [[nodiscard]] VPopoverPlacement resolvedPlacementValue() const noexcept;
    void setCrossAxisAlignment(VPopoverCrossAxisAlignment alignment);
    void setBoundaryWidget(QWidget* boundary);
    [[nodiscard]] QWidget* boundaryWidgetValue() const noexcept;
    void setBoundaryPlacements(VPopoverBoundaryPlacements placements);
    [[nodiscard]] VPopoverBoundaryPlacements boundaryPlacementsValue() const noexcept;
    void setClosePolicy(VPopoverClosePolicy policy) noexcept;

    void openFor(QWidget* anchor, const QRect& anchorRectInAnchor);
    [[nodiscard]] bool toggleFor(QWidget* anchor, const QRect& anchorRectInAnchor);
    void closeAnimated();
    void closeImmediately();
    void shutdown();

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool handleEvent(QEvent* event);
    [[nodiscard]] bool handleKeyPress(QKeyEvent* event);
    void paint(QPaintEvent* event);

    VPopoverPlacement preferredPlacement = VPopoverPlacement::Automatic;
    VPopoverCrossAxisAlignment crossAxisAlignment = VPopoverCrossAxisAlignment::Center;
    VPopoverClosePolicy closePolicy =
        VPopoverClosePolicyFlag::OutsideClick | VPopoverClosePolicyFlag::EscapeKey |
        VPopoverClosePolicyFlag::AnchorDestroyed | VPopoverClosePolicyFlag::WindowDeactivated;
    VPopoverBoundaryPlacements boundaryPlacements = VPopoverBoundaryPlacementFlag::All;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void attachOpenFilters();
    void detachOpenFilters();
    void removeAnchorFilters();
    void reconnectWindowAndScreen();
    void setObservedScreen(QScreen* screen);
    void syncTypography();
    void syncLiquidGlassSurface();
    void queueReposition();
    [[nodiscard]] bool repositionNow();
    [[nodiscard]] QRectF anchorGlobalRect() const;
    [[nodiscard]] QSizeF desiredContentSize() const;
    [[nodiscard]] QScreen* screenForAnchor(const QRectF& globalAnchor) const;

    void applyOpacityFrame(qreal opacity);
    void setClosingInputTransparent(bool transparent);
    void startOpenAnimation();
    void finishOpening();
    void finishClosing();
    void handleAnchorDestroyed();

    VPopover* q = nullptr;
    QPointer<QWidget> contentViewport;
    QPointer<QWidget> content;
    QPointer<QWidget> anchor;
    QPointer<QWidget> suppressedToggleAnchor;
    QPointer<QWidget> anchorWindow;
    QPointer<QWidget> boundaryWidget;
    QPointer<VLiquidGlassBackdrop> glassBackdrop;
    QPointer<VLiquidGlassSurface> glassSurface;
    QRect anchorLocalRect;
    QPointer<QScreen> observedScreen;

    QMetaObject::Connection themeChangedConnection;
    QMetaObject::Connection liquidGlassEnabledConnection;
    QMetaObject::Connection liquidGlassTintConnection;
    QMetaObject::Connection contentDestroyedConnection;
    QMetaObject::Connection anchorDestroyedConnection;
    QMetaObject::Connection windowScreenConnection;
    QMetaObject::Connection screenAvailableConnection;
    QMetaObject::Connection screenGeometryConnection;

    State state = State::Closed;
    bool filtersAttached = false;
    bool repositionQueued = false;
    bool internalHide = false;
    bool managesFont = true;
    bool hasAppliedFont = false;
    bool managesContentFont = false;
    bool hasAppliedContentFont = false;
    qreal currentOpacity = 1.0;
    QFont appliedFont;
    QFont appliedContentFont;
    QSize preferredContentSize;
    QMargins contentMarginOverride{-1, -1, -1, -1};
    VPopoverGeometryMetrics geometryMetrics;

    VPopoverPlacementResult finalPlacement;
    QPainterPath finalPath;
    VkWidgetAnimation animation;
    VkShadowCache shadowCache;
};

} // namespace vkui
